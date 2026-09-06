#ifndef GARBAGE_COLLECTOR_H
#define GARBAGE_COLLECTOR_H

#include <algorithm>
#include <cassert>
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
		inline constexpr size_t minimumThresholdGrowthBytes = 16 * 1024;

		class PointerRegistry
		{
		public:
			bool insert(GCObjectPtr pointer)
			{
				if (!pointer || pointer == deletedSlot())
					return false;
				ensureInsertCapacity();

				const size_t mask = slots.size() - 1;
				size_t index = hashPointer(pointer) & mask;
				size_t firstDeleted = slots.size();
				for (;;)
				{
					GCObjectPtr slot = slots[index];
					if (slot == nullptr)
					{
						if (firstDeleted != slots.size())
							index = firstDeleted;
						slots[index] = pointer;
						++objectCount;
						if (firstDeleted != slots.size())
							--deletedCount;
						return true;
					}
					if (slot == deletedSlot())
					{
						if (firstDeleted == slots.size())
							firstDeleted = index;
					}
					else if (slot == pointer)
					{
						return false;
					}
					index = (index + 1) & mask;
				}
			}

			bool erase(GCObjectPtr pointer) noexcept
			{
				if (!pointer || pointer == deletedSlot() || slots.empty())
					return false;

				const size_t mask = slots.size() - 1;
				size_t index = hashPointer(pointer) & mask;
				for (;;)
				{
					GCObjectPtr slot = slots[index];
					if (slot == nullptr)
						return false;
					if (slot == pointer)
					{
						slots[index] = deletedSlot();
						--objectCount;
						++deletedCount;
						return true;
					}
					index = (index + 1) & mask;
				}
			}

			bool contains(GCObjectPtr pointer) const noexcept
			{
				if (!pointer || pointer == deletedSlot() || slots.empty())
					return false;

				const size_t mask = slots.size() - 1;
				size_t index = hashPointer(pointer) & mask;
				for (;;)
				{
					GCObjectPtr slot = slots[index];
					if (slot == nullptr)
						return false;
					if (slot == pointer)
						return true;
					index = (index + 1) & mask;
				}
			}

			void clear() noexcept
			{
				slots.clear();
				objectCount = 0;
				deletedCount = 0;
			}

		private:
			static constexpr size_t initialCapacity = 8;

			static GCObjectPtr deletedSlot() noexcept
			{
				return reinterpret_cast<GCObjectPtr>(
					static_cast<uintptr_t>(alignof(GCObject)));
			}

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
				const size_t maximumUsedSlots = slots.size() / 2 + slots.size() / 5;
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
				// Capacity is always a power of two because probing wraps with a mask.
				assert(capacity > 0 && (capacity & (capacity - 1)) == 0);
				std::vector<GCObjectPtr> newSlots(capacity, nullptr);
				const size_t mask = capacity - 1;

				for (size_t index = 0; index < slots.size(); ++index)
				{
					GCObjectPtr pointer = slots[index];
					if (pointer == nullptr || pointer == deletedSlot())
						continue;

					size_t newIndex = hashPointer(pointer) & mask;
					while (newSlots[newIndex] != nullptr)
						newIndex = (newIndex + 1) & mask;
					newSlots[newIndex] = pointer;
				}

				slots.swap(newSlots);
				deletedCount = 0;
			}

			std::vector<GCObjectPtr> slots;
			size_t objectCount = 0;
			size_t deletedCount = 0;
		};

		inline size_t calculateNextCollectionThreshold(
			size_t configuredMinimumThreshold, size_t liveBytes) noexcept
		{
			if (configuredMinimumThreshold == 0)
				return 0;

			const size_t growth = std::max(liveBytes / 2, minimumThresholdGrowthBytes);
			const size_t maximum = std::numeric_limits<size_t>::max();
			const size_t grownThreshold = liveBytes > maximum - growth
				? maximum
				: liveBytes + growth;
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
			sweepingDeadRegistry.clear();
			for (const auto& object : objects)
			{
				GCObjectPtr ptr = object.pointer;
				if (ptr)
				{
					ptr->collectorIdentity = nullptr;
					delete ptr;
				}
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
			ensureNotDestroying("remove a root");
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
			ensureNotDestroying("remove a weak pointer");
			weaks.erase(weak);
		}

		bool owns(const GCObject* object) const override
		{
			ensureOwnerThread();
			ensureNotPoisoned();
			ensureNotDestroying("query ownership");
			GCObjectPtr target = const_cast<GCObject*>(object);
			if (!target)
				return false;
			if (state == State::sweeping && sweepingDeadRegistry.contains(target))
				return false;
			return isAllocated(target);
		}

		bool acceptsWeakTarget(const GCObject* object) const override
		{
			ensureOwnerThread();
			ensureNotPoisoned();
			ensureNotDestroying("validate weak target");
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

			if (nextCollectionThreshold && allocatedBytes >= nextCollectionThreshold)
				collect();

			const size_t objectSize = sizeof(T);
			if (allocatedBytes > std::numeric_limits<size_t>::max() - objectSize)
				throw std::length_error("managed-object memory usage is too large");

			auto object = std::make_unique<T>(std::forward<Args>(args)...);
			T* ptr = object.get();
			ptr->collectorIdentity = this;

			if (!allocatedRegistry.insert(ptr))
				throw std::logic_error("duplicate managed object address");

			try
			{
				allocated.push_back({ ptr, objectSize });
				allocatedBytes += objectSize;
				validateDirectEdges(ptr);
			}
			catch (...)
			{
				if (!allocated.empty() && allocated.back().pointer == ptr)
				{
					allocated.pop_back();
					allocatedBytes -= objectSize;
				}
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
				for (const auto& object : allocated)
				{
					GCObjectPtr ptr = object.pointer;
					if (ptr->markEpoch != currentEpoch)
						sweepingDeadRegistry.insert(ptr);
				}
			}
			catch (...)
			{
				sweepingDeadRegistry.clear();
				state = State::idle;
				throw;
			}

			state = State::sweeping;
			try
			{
				size_t liveCount = 0;
				size_t liveBytes = 0;
				for (const auto& object : allocated)
				{
					GCObjectPtr ptr = object.pointer;
					if (ptr->markEpoch == currentEpoch)
					{
						allocated[liveCount++] = object;
						liveBytes += object.size;
					}
					else
					{
						allocatedRegistry.erase(ptr);
						ptr->collectorIdentity = nullptr;
						delete ptr;  // Destructor callbacks observe State::sweeping.
					}
				}
				allocated.resize(liveCount);
				allocatedBytes = liveBytes;
				sweepingDeadRegistry.clear();

				updateNextCollectionThreshold();
				state = State::idle;
			}
			catch (...)
			{
				sweepingDeadRegistry.clear();
				state = State::poisoned;
				throw;
			}
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
			ensureNotPoisoned();
			ensureNotDestroying("query the collection threshold");
			return configuredMinimumThreshold;
		}

		size_t get_next_collection_threshold() const
		{
			ensureOwnerThread();
			ensureNotPoisoned();
			ensureNotDestroying("query the next collection threshold");
			return nextCollectionThreshold;
		}

		size_t get_objects_count() const
		{
			ensureOwnerThread();
			ensureNotPoisoned();
			ensureNotDestroying("query the object count");
			return allocated.size();
		}

#ifdef CPPGC_TESTING
		void setEpochForTesting(uint64_t epoch) noexcept
		{
			currentEpoch = epoch;
		}

		uint64_t getEpochForTesting() const noexcept
		{
			return currentEpoch;
		}
#endif

	private:
		enum class State
		{
			idle,
			collecting,
			sweeping,
			destroying,
			poisoned
		};

		void ensureOwnerThread() const
		{
			if (std::this_thread::get_id() != ownerThread)
				throw std::logic_error("garbage collector used from a non-owner thread");
		}

		void ensureNotPoisoned() const
		{
			if (state == State::poisoned)
				throw std::logic_error("garbage collector is in a poisoned state after a failed sweep");
		}

		void ensureNotDestroying(const char* operation) const
		{
			if (state == State::destroying)
				throw std::logic_error(std::string("cannot ") + operation + " while collector is being destroyed");
		}

		void ensureIdle(const char* operation) const
		{
			ensureNotPoisoned();
			if (state != State::idle)
				throw std::logic_error(std::string("cannot ") + operation + " while collection is active");
		}

		void advanceEpoch()
		{
			if (currentEpoch == std::numeric_limits<uint64_t>::max())
			{
				for (const auto& object : allocated)
				{
					GCObjectPtr ptr = object.pointer;
					ptr->markEpoch = 0;
				}
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
				configuredMinimumThreshold, allocatedBytes);
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

		struct AllocatedObject
		{
			GCObjectPtr pointer;
			size_t size;
		};

		size_t configuredMinimumThreshold;
		size_t nextCollectionThreshold;
		std::thread::id ownerThread;
		State state = State::idle;
		uint64_t currentEpoch = 0;
		std::unordered_set<GCObjectRootPtrBase*> roots;
		std::unordered_set<GCObjectWeakPtrBase*> weaks;
		std::vector<AllocatedObject> allocated;
		size_t allocatedBytes = 0;
		detail::PointerRegistry allocatedRegistry;
		detail::PointerRegistry sweepingDeadRegistry;
	};
}

#endif // GARBAGE_COLLECTOR_H
