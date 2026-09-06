#ifndef GCOBJECT_ROOT_PTR_H
#define GCOBJECT_ROOT_PTR_H

#include <stdexcept>
#include <type_traits>

#include "IRootsRegistry.h"
#include "GCObject.h"

namespace cppgc
{
	class GCObjectRootPtrBase
	{
		IRootsRegistry* rootsRegistry;

	protected:
		GCObjectPtr pObject;

	public:
		explicit GCObjectRootPtrBase(IRootsRegistry& registry)
			: rootsRegistry(&registry), pObject(nullptr)
		{
			rootsRegistry->addRoot(this);
		}

		virtual ~GCObjectRootPtrBase()
		{
			if (rootsRegistry)
				rootsRegistry->removeRoot(this);
		}

		// Copying a root whose registry has been detached produces a detached empty root.
		GCObjectRootPtrBase(const GCObjectRootPtrBase& rhs)
			: rootsRegistry(rhs.rootsRegistry), pObject(rhs.pObject)
		{
			if (rootsRegistry)
				rootsRegistry->addRoot(this);
		}

		GCObjectRootPtrBase& operator=(const GCObjectRootPtrBase& rhs)
		{
			if (this == &rhs)
				return *this;
			if (rootsRegistry != rhs.rootsRegistry)
				throw std::invalid_argument("cannot assign roots from different collectors");

			return operator=(rhs.pObject);
		}

		GCObjectRootPtrBase& operator=(GCObjectPtr object)
		{
			if (object && (!rootsRegistry || !rootsRegistry->owns(object)))
				throw std::invalid_argument("object is not owned by this root's collector");

			pObject = object;
			return *this;
		}

		explicit operator bool() const noexcept
		{
			return !empty();
		}

		bool empty() const noexcept
		{
			return pObject == nullptr;
		}

		void reset() noexcept
		{
			pObject = nullptr;
		}

		GCObjectPtr operator->() const noexcept
		{
			return pObject;
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
	class GCObjectRootPtr : public GCObjectRootPtrBase
	{
		static_assert(std::is_base_of_v<GCObject, T>, "root type must derive from GCObject");

	public:
		explicit GCObjectRootPtr(IRootsRegistry& registry)
			: GCObjectRootPtrBase(registry)
		{}

		GCObjectRootPtr(const GCObjectRootPtr& rhs)
			: GCObjectRootPtrBase(rhs)
		{}

		template<class U>
		GCObjectRootPtr(const GCObjectRootPtr<U>& rhs)
			: GCObjectRootPtrBase(rhs)
		{
			static_assert(std::is_convertible_v<U*, T*>, "source root type must be convertible to target root type");
		}

		GCObjectRootPtr& operator=(const GCObjectRootPtr& rhs)
		{
			GCObjectRootPtrBase::operator=(rhs);
			return *this;
		}

		template<class U>
		GCObjectRootPtr& operator=(const GCObjectRootPtr<U>& rhs)
		{
			static_assert(std::is_convertible_v<U*, T*>, "source root type must be convertible to target root type");
			GCObjectRootPtrBase::operator=(rhs);
			return *this;
		}

		GCObjectRootPtr& operator=(T* object)
		{
			GCObjectRootPtrBase::operator=(static_cast<GCObjectPtr>(object));
			return *this;
		}

		T* operator->() const noexcept
		{
			return static_cast<T*>(pObject);
		}

		T* get() const noexcept
		{
			return static_cast<T*>(pObject);
		}

		IRootsRegistry* registry() const noexcept
		{
			return GCObjectRootPtrBase::registry();
		}
	};
}

#endif // GCOBJECT_ROOT_PTR_H
