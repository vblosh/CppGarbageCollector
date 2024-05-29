#ifndef GARBAGE_COLLECTOR_H
#define GARBAGE_COLLECTOR_H

#include <memory>
#include <forward_list>
#include <set>

#include "GCObject.h"
#include "IRootsRegistry.h"
#include "GCObjectRootPtr.h"

namespace cppgc
{
	class GarbageCollector : public IRootsRegistry
	{
	public:
		GarbageCollector() : objects_count(0)
		{}

		~GarbageCollector()
		{
			for (auto ptr : allocated)
			{
				delete ptr;
			}
			allocated.clear();
			objects_count = 0;
			roots.clear();
		}

		// Inherited via IRootsRegistry
		virtual void addRoot(GCObjectRootPtrBase* root) override
		{
			roots.insert(root);
		}

		virtual void removeRoot(GCObjectRootPtrBase* root) override
		{
			roots.erase(root);
		}

		template<class T, class... Args>
		T* createInstance(Args&&... args)
		{
			T* ptr = ::new T(std::forward<Args>(args)...);

			allocated.push_front(ptr);
			++objects_count;
			return ptr;
		}

		void DFS(GCObjectPtr node)
		{
			if ((node == nullptr) || (node->visited))
				return;

			node->visited = true;
			ClassInfo* pInfo = node->getClassInfo();

			do
			{
				for (size_t i = 0; i < pInfo->pointersCount; ++i)
				{
					GCObjectPtr* pChild = reinterpret_cast<GCObjectPtr*>(reinterpret_cast<char*>(node) + pInfo->ptrOffsets[i]);
					GCObjectPtr child = *pChild;
					if (child && !child->visited)
						DFS(child);
				}
				pInfo = pInfo->parentInfo;
			} while (pInfo);
		}

		void collect()
		{
			// make depth first search on all roots
			// and mark all objects as visited
			for (auto root : roots)
			{
				DFS(root->get());
			}
			// destroy and deallocate all unvisited objects
			for (auto before_ptr = allocated.before_begin(); std::next(before_ptr) != allocated.end();)
			{
				GCObjectPtr ptr = *std::next(before_ptr);
				if (!ptr->visited) {
					delete ptr;
					allocated.erase_after(before_ptr);
					--objects_count;
				}
				else {
					ptr->visited = false;
					before_ptr++;
				}
			}
		}

		size_t get_objects_count()
		{
			return objects_count;
		}

	private:
		size_t objects_count;
		std::set<GCObjectRootPtrBase*> roots;
		std::forward_list<GCObjectPtr> allocated;
	};

}

#endif // GARBAGE_COLLECTOR_H