#ifndef GARBAGE_COLLECTOR_H
#define GARBAGE_COLLECTOR_H

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "GCObject.h"
#include "IRootsRegistry.h"
#include "GCObjectRootPtr.h"

namespace cppgc
{
	namespace detail
	{
		inline constexpr size_t minimumThresholdGrowth = 1024;

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

			auto objects = std::move(allocated);
			for (auto ptr : objects)
				ptr->collectorIdentity = nullptr;
			for (auto ptr : objects)
				delete ptr;
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

		bool owns(const GCObject* object) const override
		{
			ensureOwnerThread();
			return object && allocated.find(const_cast<GCObject*>(object)) != allocated.end();
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

			const auto result = allocated.insert(ptr);
			if (!result.second)
				throw std::logic_error("duplicate managed object address");

			try
			{
				validateDirectEdges(ptr);
			}
			catch (...)
			{
				allocated.erase(ptr);
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
				for (auto root : roots)
					markReachable(root->get());

				GCPointerList garbage;
				garbage.reserve(allocated.size());
				for (auto ptr : allocated)
				{
					if (ptr->markEpoch != currentEpoch)
						garbage.push_back(ptr);
				}

				for (auto ptr : garbage)
				{
					allocated.erase(ptr);
					ptr->collectorIdentity = nullptr;
				}
				for (auto ptr : garbage)
					delete ptr;

				updateNextCollectionThreshold();
			}
			catch (...)
			{
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

		size_t get_collection_threshold() const noexcept
		{
			return configuredMinimumThreshold;
		}

		size_t get_next_collection_threshold() const noexcept
		{
			return nextCollectionThreshold;
		}

		size_t get_objects_count() const noexcept
		{
			return allocated.size();
		}

	private:
		enum class State
		{
			idle,
			collecting,
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

		void traceDirectEdges(GCObjectPtr object, GCPointerList& pointers) const
		{
			for (const ClassInfo* info = object->getClassInfo(); info; info = info->parentInfo)
			{
				if (info->tracePointers)
					info->tracePointers(object, pointers);
			}
		}

		void validateDirectEdges(GCObjectPtr object) const
		{
			GCPointerList pointers;
			traceDirectEdges(object, pointers);
			for (auto pointer : pointers)
			{
				if (pointer && allocated.find(pointer) == allocated.end())
					throw std::invalid_argument("managed object has an edge outside its collector");
			}
		}

		void markReachable(GCObjectPtr root)
		{
			if (allocated.find(root) == allocated.end())
				return;

			GCPointerList pending;
			GCPointerList children;
			root->markEpoch = currentEpoch;
			pending.push_back(root);

			while (!pending.empty())
			{
				GCObjectPtr node = pending.back();
				pending.pop_back();

				children.clear();
				traceDirectEdges(node, children);
				for (auto child : children)
				{
					if (allocated.find(child) == allocated.end() || child->markEpoch == currentEpoch)
						continue;
					child->markEpoch = currentEpoch;
					pending.push_back(child);
				}
			}
		}

		size_t configuredMinimumThreshold;
		size_t nextCollectionThreshold;
		std::thread::id ownerThread;
		State state = State::idle;
		uint64_t currentEpoch = 0;
		std::set<GCObjectRootPtrBase*> roots;
		std::unordered_set<GCObjectPtr> allocated;
	};
}

#endif // GARBAGE_COLLECTOR_H
