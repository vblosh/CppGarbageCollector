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

			pObject = rhs.pObject;
			return *this;
		}

		GCObjectRootPtrBase& operator=(GCObjectPtr object)
		{
			if (object && (!rootsRegistry || !rootsRegistry->owns(object)))
				throw std::invalid_argument("object is not owned by this root's collector");

			pObject = object;
			return *this;
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

		GCObjectRootPtr& operator=(const GCObjectRootPtr& rhs)
		{
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
	};
}

#endif // GCOBJECT_ROOT_PTR_H
