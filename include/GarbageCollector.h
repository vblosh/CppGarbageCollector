#ifndef GARBAGE_COLLECTOR_H
#define GARBAGE_COLLECTOR_H

#include <memory>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "GCObject.h"
#include "IRootsRegistry.h"
#include "GCObjectRootPtr.h"

namespace cppgc
{
	class GarbageCollector : public IRootsRegistry
	{
	public:
		GarbageCollector() = default;
		GarbageCollector(const GarbageCollector&) = delete;
		GarbageCollector& operator=(const GarbageCollector&) = delete;
		GarbageCollector(GarbageCollector&&) = delete;
		GarbageCollector& operator=(GarbageCollector&&) = delete;

		~GarbageCollector() override
		{
			for (auto root : roots)
				root->detachRegistry(this);
			roots.clear();

			for (auto ptr : allocated)
				delete ptr;
			allocated.clear();
		}

		void addRoot(GCObjectRootPtrBase* root) override
		{
			roots.insert(root);
		}

		void removeRoot(GCObjectRootPtrBase* root) override
		{
			roots.erase(root);
		}

		bool owns(const GCObject* object) const override
		{
			return object && allocated.find(const_cast<GCObject*>(object)) != allocated.end();
		}

		template<class T, class... Args>
		T* createInstance(Args&&... args)
		{
			static_assert(std::is_base_of_v<GCObject, T>, "managed type must derive from GCObject");

			auto object = std::make_unique<T>(std::forward<Args>(args)...);
			T* ptr = object.get();
			const auto result = allocated.insert(ptr);
			if (!result.second)
				throw std::logic_error("duplicate managed object address");
			object.release();
			return ptr;
		}

		void DFS(GCObjectPtr root)
		{
			GCPointerList pending;
			pending.push_back(root);

			while (!pending.empty())
			{
				GCObjectPtr node = pending.back();
				pending.pop_back();

				const auto owned = allocated.find(node);
				if (owned == allocated.end() || node->visited)
					continue;

				node->visited = true;
				for (const ClassInfo* info = node->getClassInfo(); info; info = info->parentInfo)
				{
					if (info->tracePointers)
						info->tracePointers(node, pending);
				}
			}
		}

		void collect()
		{
			for (auto root : roots)
				DFS(root->get());

			GCPointerList garbage;
			garbage.reserve(allocated.size());
			for (auto ptr : allocated)
			{
				if (!ptr->visited)
					garbage.push_back(ptr);
				else
					ptr->visited = false;
			}

			for (auto ptr : garbage)
				allocated.erase(ptr);
			for (auto ptr : garbage)
				delete ptr;
		}

		size_t get_objects_count() const noexcept
		{
			return allocated.size();
		}

	private:
		std::set<GCObjectRootPtrBase*> roots;
		std::unordered_set<GCObjectPtr> allocated;
	};
}

#endif // GARBAGE_COLLECTOR_H
