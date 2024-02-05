#pragma once
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
		GarbageCollector()
		{}

		~GarbageCollector()
		{
			for (auto ptr : allocated) 
			{
				delete ptr;
			}
			allocated.clear();
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
			return ptr;
		}

		void DFS(GCObject* node)
		{
			if ((node == nullptr) || (node->visited)) 
				return;
			
			node->visited = true;
			for (auto child : *node)
			{
				if((*child) && !(*child)->visited) 
					DFS(*child);
			}
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
				GCObject* ptr = *std::next(before_ptr);
				if (!ptr->visited) {
					delete ptr;
					allocated.erase_after(before_ptr);
				}
				else {
					ptr->visited = false;
					before_ptr++;
				}
			}
		}

	private:
		std::set<GCObjectRootPtrBase*> roots;
		std::forward_list<GCObject*> allocated;
	};

}