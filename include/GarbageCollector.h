#ifndef GARBAGE_COLLECTOR_H
#define GARBAGE_COLLECTOR_H

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "GCObject.h"
#include "IRootsRegistry.h"
#include "GCObjectRootPtr.h"
#include "GCObjectWeakPtr.h"

namespace cppgc
{
	namespace detail
	{
		inline constexpr size_t minimumThresholdGrowth = 1024;

		class PointerRegistry
		{
		public:
			bool insert(GCObjectPtr pointer)
			{
				if (!pointer)
					return false;
				ensureInsertCapacity();

				const size_t mask = slots.size() - 1;
				size_t index = hashPointer(pointer) & mask;
				size_t firstDeleted = slots.size();
				for (;;)
				{
					switch (states[index])
					{
					case SlotState::empty:
						if (firstDeleted != slots.size())
							index = firstDeleted;
						slots[index] = pointer;
						states[index] = SlotState::occupied;
						++objectCount;
						if (firstDeleted != slots.size())
							--deletedCount;
						return true;
					case SlotState::occupied:
						if (slots[index] == pointer)
							return false;
						break;
					case SlotState::deleted:
						if (firstDeleted == slots.size())
							firstDeleted = index;
						break;
					}
					index = (index + 1) & mask;
				}
			}

			bool erase(GCObjectPtr pointer) noexcept
			{
				if (!pointer || slots.empty())
					return false;

				const size_t mask = slots.size() - 1;
				size_t index = hashPointer(pointer) & mask;
				for (;;)
				{
					if (states[index] == SlotState::empty)
						return false;
					if (states[index] == SlotState::occupied && slots[index] == pointer)
					{
						slots[index] = nullptr;
						states[index] = SlotState::deleted;
						--objectCount;
						++deletedCount;
						return true;
					}
					index = (index + 1) & mask;
				}
			}

			bool contains(GCObjectPtr pointer) const noexcept
			{
				if (!pointer || slots.empty())
					return false;

				const size_t mask = slots.size() - 1;
				size_t index = hashPointer(pointer) & mask;
				for (;;)
				{
					if (states[index] == SlotState::empty)
						return false;
					if (states[index] == SlotState::occupied && slots[index] == pointer)
						return true;
					index = (index + 1) & mask;
				}
			}

			void clear() noexcept
			{
				slots.clear();
				states.clear();
				objectCount = 0;
				deletedCount = 0;
			}

		private:
			enum class SlotState : uint8_t
			{
				empty,
				occupied,
				deleted
			};

			static constexpr size_t initialCapacity = 8;

			static size_t hashPointer(GCObjectPtr pointer) noexcept
			{
				size_t value = std::hash<GCObjectPtr>{}(pointer);
				if constexpr (sizeof(size_t) >= sizeof(uint64_t))
				{
					value ^= value >> 33;
					value *= static_cast<size_t>(0xff51afd7ed558ccdULL);
					value ^= value >> 33;
					value *= static_cast<size_t>(0xc4ceb9fe1a85ec53ULL);
					value ^= value >> 33;
				}
				else
				{
					value ^= value >> 16;
					value *= static_cast<size_t>(0x7feb352dU);
					value ^= value >> 15;
					value *= static_cast<size_t>(0x846ca68bU);
					value ^= value >> 16;
				}
				return value;
			}

			void ensureInsertCapacity()
			{
				if (slots.empty())
					return rehash(initialCapacity);

				const size_t usedSlots = objectCount + deletedCount;
				const size_t maximumUsedSlots = slots.size() - slots.size() / 4;
				if (usedSlots + 1 <= maximumUsedSlots)
					return;

				if (objectCount + 1 <= maximumUsedSlots)
					return rehash(slots.size());

				if (slots.size() > std::numeric_limits<size_t>::max() / 2)
					throw std::length_error("managed-object registry is too large");
				rehash(slots.size() * 2);
			}

			void rehash(size_t capacity)
			{
				std::vector<GCObjectPtr> newSlots(capacity, nullptr);
				std::vector<SlotState> newStates(capacity, SlotState::empty);
				const size_t mask = capacity - 1;

				for (size_t index = 0; index < slots.size(); ++index)
				{
					if (states[index] != SlotState::occupied)
						continue;

					GCObjectPtr pointer = slots[index];
					size_t newIndex = hashPointer(pointer) & mask;
					while (newStates[newIndex] == SlotState::occupied)
						newIndex = (newIndex + 1) & mask;
					newSlots[newIndex] = pointer;
					newStates[newIndex] = SlotState::occupied;
				}

				slots.swap(newSlots);
				states.swap(newStates);
				deletedCount = 0;
			}

			std::vector<GCObjectPtr> slots;
			std::vector<SlotState> states;
			size_t objectCount = 0;
			size_t deletedCount = 0;
		};

		inline size_t calculateNextCollectionThreshold(
			size_t configuredMinimumThreshold, size_t liveObjects) noexcept
		{
			if (configuredMinimumThreshold == 0)
				return 0;

			const size_t growth = std::max(liveObjects / 2, minimumThresholdGrowth);
			const size_t maximum = std::numeric_limits<size_t>::max();
			const size_t grownThreshold = liveObjects > maximum - growth
				? maximum
				: liveObjects + growth;
			return std::max(configuredMinimumThreshold, grownThreshold);
		}
	}

	class GarbageCollector : public IRootsRegistry
	{
	public:
		explicit GarbageCollector(size_t collectionThreshold = 0)
			: configuredMinimumThreshold(collectionThreshold),
			nextCollectionThreshold(collectionThreshold),
			ownerThread(std::this_thread::get_id())
		{}

		GarbageCollector(const GarbageCollector&) = delete;
		GarbageCollector& operator=(const GarbageCollector&) = delete;
		GarbageCollector(GarbageCollector&&) = delete;
		GarbageCollector& operator=(GarbageCollector&&) = delete;

		~GarbageCollector() override
		{
			state = State::destroying;
			for (auto root : roots)
				root->detachRegistry(this);
			roots.clear();

			for (auto weak : weaks)
				weak->detachRegistry(this);
			weaks.clear();

			auto objects = std::move(allocated);
			allocatedRegistry.clear();
			for (auto ptr : objects)
			{
				ptr->collectorIdentity = nullptr;
				delete ptr;
			}
		}

		void addRoot(GCObjectRootPtrBase* root) override
		{
			ensureOwnerThread();
			ensureIdle("register a root");
			roots.insert(root);
		}

		void removeRoot(GCObjectRootPtrBase* root) override
		{
			ensureOwnerThread();
			roots.erase(root);
		}

		void addWeak(GCObjectWeakPtrBase* weak) override
		{
			ensureOwnerThread();
			ensureIdle("register a weak pointer");
			weaks.insert(weak);
		}

		void removeWeak(GCObjectWeakPtrBase* weak) override
		{
			ensureOwnerThread();
			weaks.erase(weak);
		}

		bool owns(const GCObject* object) const override
		{
			ensureOwnerThread();
			GCObjectPtr target = const_cast<GCObject*>(object);
			return isAllocated(target)
				|| (state == State::sweeping && sweepingDeadRegistry.contains(target));
		}

		bool acceptsWeakTarget(const GCObject* object) const override
		{
			ensureOwnerThread();
			GCObjectPtr target = const_cast<GCObject*>(object);
			if (state == State::sweeping && sweepingDeadRegistry.contains(target))
				return false;
			if (!isAllocated(target))
				throw std::invalid_argument("object is not owned by this collector");
			return true;
		}

		template<class T, class... Args>
		T* createInstance(Args&&... args)
		{
			static_assert(std::is_base_of_v<GCObject, T>, "managed type must derive from GCObject");
			static_assert(std::is_nothrow_destructible_v<T>, "managed type must have a non-throwing destructor");
			ensureOwnerThread();
			ensureIdle("allocate an object");

			if (nextCollectionThreshold && allocated.size() >= nextCollectionThreshold)
				collect();

			auto object = std::make_unique<T>(std::forward<Args>(args)...);
			T* ptr = object.get();
			ptr->collectorIdentity = this;

			if (!allocatedRegistry.insert(ptr))
				throw std::logic_error("duplicate managed object address");

			try
			{
				allocated.push_back(ptr);
				validateDirectEdges(ptr);
			}
			catch (...)
			{
				if (!allocated.empty() && allocated.back() == ptr)
					allocated.pop_back();
				allocatedRegistry.erase(ptr);
				ptr->collectorIdentity = nullptr;
				throw;
			}

			object.release();
			return ptr;
		}

		void collect()
		{
			ensureOwnerThread();
			ensureIdle("start collection");
			state = State::collecting;

			try
			{
				advanceEpoch();
				markReachableRoots();

				clearDeadWeakPointers();
				sweepingDeadRegistry.clear();
				for (auto ptr : allocated)
				{
					if (ptr->markEpoch != currentEpoch)
						sweepingDeadRegistry.insert(ptr);
				}
				state = State::sweeping;

				size_t liveCount = 0;
				for (auto ptr : allocated)
				{
					if (ptr->markEpoch == currentEpoch)
					{
						allocated[liveCount++] = ptr;
					}
					else
					{
						allocatedRegistry.erase(ptr);
						ptr->collectorIdentity = nullptr;
						delete ptr;  // Destructor callbacks observe State::sweeping.
					}
				}
				allocated.resize(liveCount);
				sweepingDeadRegistry.clear();

				updateNextCollectionThreshold();
			}
			catch (...)
			{
				sweepingDeadRegistry.clear();
				state = State::idle;
				throw;
			}

			state = State::idle;
		}

		void set_collection_threshold(size_t threshold)
		{
			ensureOwnerThread();
			ensureIdle("change the collection threshold");
			configuredMinimumThreshold = threshold;
			nextCollectionThreshold = threshold;
		}

		size_t get_collection_threshold() const
		{
			ensureOwnerThread();
			return configuredMinimumThreshold;
		}

		size_t get_next_collection_threshold() const
		{
			ensureOwnerThread();
			return nextCollectionThreshold;
		}

		size_t get_objects_count() const
		{
			ensureOwnerThread();
			return allocated.size();
		}

	private:
		enum class State
		{
			idle,
			collecting,
			sweeping,
			destroying
		};

		void ensureOwnerThread() const
		{
			if (std::this_thread::get_id() != ownerThread)
				throw std::logic_error("garbage collector used from a non-owner thread");
		}

		void ensureIdle(const char* operation) const
		{
			if (state != State::idle)
				throw std::logic_error(std::string("cannot ") + operation + " while collection is active");
		}

		void advanceEpoch()
		{
			if (currentEpoch == std::numeric_limits<uint64_t>::max())
			{
				for (auto ptr : allocated)
					ptr->markEpoch = 0;
				currentEpoch = 1;
			}
			else
			{
				++currentEpoch;
			}
		}

		void updateNextCollectionThreshold() noexcept
		{
			nextCollectionThreshold = detail::calculateNextCollectionThreshold(
				configuredMinimumThreshold, allocated.size());
		}

		struct ValidationTraceContext
		{
			const GarbageCollector* collector;
		};

		static void validateManagedEdge(void* opaqueContext, GCObjectPtr pointer)
		{
			auto& context = *static_cast<ValidationTraceContext*>(opaqueContext);
			if (pointer && !context.collector->isAllocated(pointer))
			{
				throw std::invalid_argument("managed object has an edge outside its collector");
			}
		}

		static void ignoreRawEdgeDuringValidation(void*, GCObjectPtr) noexcept
		{}

		void validateDirectEdges(GCObjectPtr object) const
		{
			ValidationTraceContext context{ this };
			TraceVisitor visitor{ &context, validateManagedEdge, ignoreRawEdgeDuringValidation };
			object->trace(visitor);
		}

		struct MarkTraceContext
		{
			GarbageCollector* collector;
			GCPointerList* pending;
		};

		static void markManagedEdge(void* opaqueContext, GCObjectPtr child)
		{
			auto& context = *static_cast<MarkTraceContext*>(opaqueContext);
			if (!child)
				return;
			if (!context.collector->isAllocated(child))
			{
				throw std::invalid_argument("managed edge points outside its collector");
			}
			if (child->markEpoch == context.collector->currentEpoch)
				return;
			child->markEpoch = context.collector->currentEpoch;
			context.pending->push_back(child);
		}

		static void markRawEdge(void* opaqueContext, GCObjectPtr child)
		{
			auto& context = *static_cast<MarkTraceContext*>(opaqueContext);
			if (!context.collector->isAllocated(child) ||
				child->markEpoch == context.collector->currentEpoch)
			{
				return;
			}
			child->markEpoch = context.collector->currentEpoch;
			context.pending->push_back(child);
		}

		void markReachableRoots()
		{
			GCPointerList pending;
			MarkTraceContext context{ this, &pending };
			TraceVisitor visitor{ &context, markManagedEdge, markRawEdge };

			for (auto root : roots)
			{
				GCObjectPtr object = root->get();
				if (!isAllocated(object) || object->markEpoch == currentEpoch)
					continue;
				object->markEpoch = currentEpoch;
				pending.push_back(object);
			}

			while (!pending.empty())
			{
				GCObjectPtr node = pending.back();
				pending.pop_back();
				node->trace(visitor);
			}
		}

		void clearDeadWeakPointers() noexcept
		{
			for (auto weak : weaks)
			{
				GCObjectPtr object = weak->get();
				if (object && object->markEpoch != currentEpoch)
					weak->reset();
			}
		}

		bool isAllocated(GCObjectPtr object) const noexcept
		{
			return allocatedRegistry.contains(object);
		}

		size_t configuredMinimumThreshold;
		size_t nextCollectionThreshold;
		std::thread::id ownerThread;
		State state = State::idle;
		uint64_t currentEpoch = 0;
		std::unordered_set<GCObjectRootPtrBase*> roots;
		std::unordered_set<GCObjectWeakPtrBase*> weaks;
		std::vector<GCObjectPtr> allocated;
		detail::PointerRegistry allocatedRegistry;
		detail::PointerRegistry sweepingDeadRegistry;
	};
}

#endif // GARBAGE_COLLECTOR_H
