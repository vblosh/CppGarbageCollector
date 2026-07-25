#ifndef GCOBJECT_WEAK_PTR_H
#define GCOBJECT_WEAK_PTR_H

#include <stdexcept>
#include <type_traits>

#include "IRootsRegistry.h"
#include "GCObject.h"
#include "GCObjectRootPtr.h"

namespace cppgc
{
	// Non-owning handle to a managed object. Unlike GCObjectRootPtr, a weak
	// pointer does not keep its target alive. The collector clears registered
	// weak pointers when their target is reclaimed.
	class GCObjectWeakPtrBase
	{
		IRootsRegistry* rootsRegistry;

	protected:
		GCObjectPtr pObject;

	public:
		explicit GCObjectWeakPtrBase(IRootsRegistry& registry)
			: rootsRegistry(&registry), pObject(nullptr)
		{
			rootsRegistry->addWeak(this);
		}

		virtual ~GCObjectWeakPtrBase()
		{
			if (rootsRegistry)
				rootsRegistry->removeWeak(this);
		}

		GCObjectWeakPtrBase(const GCObjectWeakPtrBase& rhs)
			: rootsRegistry(rhs.rootsRegistry), pObject(rhs.pObject)
		{
			if (rootsRegistry)
				rootsRegistry->addWeak(this);
		}

		GCObjectWeakPtrBase& operator=(const GCObjectWeakPtrBase& rhs)
		{
			if (this == &rhs)
				return *this;
			if (rootsRegistry != rhs.rootsRegistry)
				throw std::invalid_argument("cannot assign weak pointers from different collectors");

			pObject = rhs.pObject;
			return *this;
		}

		GCObjectWeakPtrBase& operator=(GCObjectPtr object)
		{
			if (object && (!rootsRegistry || !rootsRegistry->owns(object)))
				throw std::invalid_argument("object is not owned by this weak pointer's collector");

			pObject = object;
			return *this;
		}

		bool empty() const noexcept
		{
			return pObject == nullptr;
		}

		bool expired() const noexcept
		{
			return pObject == nullptr;
		}

		void reset() noexcept
		{
			pObject = nullptr;
		}

		GCObjectPtr get() const noexcept
		{
			return pObject;
		}

		IRootsRegistry* registry() const noexcept
		{
			return rootsRegistry;
		}

		void detachRegistry(const IRootsRegistry* registry) noexcept
		{
			if (rootsRegistry == registry)
			{
				rootsRegistry = nullptr;
				pObject = nullptr;
			}
		}
	};

	template<class T>
	class GCObjectWeakPtr : public GCObjectWeakPtrBase
	{
		static_assert(std::is_base_of_v<GCObject, T>, "weak pointer type must derive from GCObject");

	public:
		explicit GCObjectWeakPtr(IRootsRegistry& registry)
			: GCObjectWeakPtrBase(registry)
		{}

		GCObjectWeakPtr(const GCObjectWeakPtr& rhs)
			: GCObjectWeakPtrBase(rhs)
		{}

		explicit GCObjectWeakPtr(const GCObjectRootPtr<T>& root)
			: GCObjectWeakPtrBase(*requireRegistry(root))
		{
			pObject = root.get();
		}

		GCObjectWeakPtr& operator=(const GCObjectWeakPtr& rhs)
		{
			GCObjectWeakPtrBase::operator=(rhs);
			return *this;
		}

		GCObjectWeakPtr& operator=(const GCObjectRootPtr<T>& root)
		{
			IRootsRegistry* rootRegistry = root.registry();
			if (registry() != rootRegistry)
				throw std::invalid_argument("cannot assign weak pointers from different collectors");

			pObject = root.get();
			return *this;
		}

		GCObjectWeakPtr& operator=(T* object)
		{
			GCObjectWeakPtrBase::operator=(static_cast<GCObjectPtr>(object));
			return *this;
		}

		T* get() const noexcept
		{
			return static_cast<T*>(pObject);
		}

		// Promote to a strong root if the target is still alive.
		// Requires a live collector (the weak pointer must not be detached).
		GCObjectRootPtr<T> lock() const
		{
			IRootsRegistry* activeRegistry = registry();
			if (!activeRegistry)
				throw std::logic_error("cannot lock a weak pointer without a collector");

			GCObjectRootPtr<T> root(*activeRegistry);
			if (pObject)
				root = static_cast<T*>(pObject);
			return root;
		}

	private:
		static IRootsRegistry* requireRegistry(const GCObjectRootPtr<T>& root)
		{
			IRootsRegistry* activeRegistry = root.registry();
			if (!activeRegistry)
				throw std::logic_error("cannot create a weak pointer from a detached root");
			return activeRegistry;
		}
	};
}

#endif // GCOBJECT_WEAK_PTR_H
