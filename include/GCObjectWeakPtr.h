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

		~GCObjectWeakPtrBase()
		{
			if (rootsRegistry)
			{
				try
				{
					rootsRegistry->removeWeak(this);
				}
				catch (...)
				{
				}
			}
		}

	public:
		explicit GCObjectWeakPtrBase(IRootsRegistry& registry)
			: rootsRegistry(&registry), pObject(nullptr)
		{
			rootsRegistry->addWeak(this);
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

			return operator=(rhs.pObject);
		}

		GCObjectWeakPtrBase& operator=(GCObjectPtr object)
		{
			if (object)
			{
				if (!rootsRegistry)
					throw std::invalid_argument("object is not owned by this weak pointer's collector");
				if (!rootsRegistry->acceptsWeakTarget(object))
				{
					pObject = nullptr;
					return *this;
				}
			}

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
	public:
		explicit GCObjectWeakPtr(IRootsRegistry& registry)
			: GCObjectWeakPtrBase(registry)
		{}

		GCObjectWeakPtr(const GCObjectWeakPtr& rhs)
			: GCObjectWeakPtrBase(rhs)
		{}

		template<class U>
		GCObjectWeakPtr(const GCObjectWeakPtr<U>& rhs)
			: GCObjectWeakPtrBase(rhs)
		{
			static_assert(std::is_convertible_v<U*, T*>, "source weak pointer type must be convertible to target weak pointer type");
		}

		template<class U>
		explicit GCObjectWeakPtr(const GCObjectRootPtr<U>& root)
			: GCObjectWeakPtrBase(*requireRegistry(root))
		{
			static_assert(std::is_base_of_v<GCObject, T>, "weak pointer type must derive from GCObject");
			static_assert(std::is_convertible_v<U*, T*>, "root type must be convertible to weak pointer type");
			pObject = static_cast<T*>(root.get());
		}

		GCObjectWeakPtr& operator=(const GCObjectWeakPtr& rhs)
		{
			GCObjectWeakPtrBase::operator=(rhs);
			return *this;
		}

		template<class U>
		GCObjectWeakPtr& operator=(const GCObjectWeakPtr<U>& rhs)
		{
			static_assert(std::is_convertible_v<U*, T*>, "source weak pointer type must be convertible to target weak pointer type");
			GCObjectWeakPtrBase::operator=(rhs);
			return *this;
		}

		template<class U>
		GCObjectWeakPtr& operator=(const GCObjectRootPtr<U>& root)
		{
			static_assert(std::is_base_of_v<GCObject, T>, "weak pointer type must derive from GCObject");
			static_assert(std::is_convertible_v<U*, T*>, "root type must be convertible to weak pointer type");
			IRootsRegistry* rootRegistry = root.registry();
			if (registry() != rootRegistry)
				throw std::invalid_argument("cannot assign weak pointers from different collectors");

			GCObjectWeakPtrBase::operator=(static_cast<T*>(root.get()));
			return *this;
		}

		GCObjectWeakPtr& operator=(T* object)
		{
			static_assert(std::is_base_of_v<GCObject, T>, "weak pointer type must derive from GCObject");
			GCObjectWeakPtrBase::operator=(static_cast<GCObjectPtr>(object));
			return *this;
		}

		// Note: operator*() and operator->() are intentionally omitted for GCObjectWeakPtr.
		// Callers must use lock() to promote the weak pointer to a strong GCObjectRootPtr
		// before dereferencing, ensuring the object is kept alive during access.

		// Downcast relies on non-virtual inheritance from GCObject.
		T* get() const noexcept
		{
			static_assert(std::is_base_of_v<GCObject, T>, "weak pointer type must derive from GCObject");
			return static_cast<T*>(pObject);
		}

		// Promote to a strong root if the target is still alive.
		// Requires a live collector (the weak pointer must not be detached).
		auto lock() const
		{
			static_assert(std::is_base_of_v<GCObject, T>, "weak pointer type must derive from GCObject");

			IRootsRegistry* activeRegistry = registry();
			if (!activeRegistry)
				throw std::logic_error("cannot lock a weak pointer without a collector");

			GCObjectRootPtr<T> root(*activeRegistry);
			if (pObject)
				root = static_cast<T*>(pObject);
			return root;
		}

		template<class U>
		bool operator==(const GCObjectWeakPtr<U>& rhs) const noexcept
		{
			return get() == rhs.get();
		}

		template<class U>
		bool operator!=(const GCObjectWeakPtr<U>& rhs) const noexcept
		{
			return get() != rhs.get();
		}

		template<class U>
		bool operator==(const GCObjectRootPtr<U>& rhs) const noexcept
		{
			return get() == rhs.get();
		}

		template<class U>
		bool operator!=(const GCObjectRootPtr<U>& rhs) const noexcept
		{
			return get() != rhs.get();
		}

		bool operator==(std::nullptr_t) const noexcept
		{
			return empty();
		}

		bool operator!=(std::nullptr_t) const noexcept
		{
			return !empty();
		}

		friend bool operator==(std::nullptr_t, const GCObjectWeakPtr& rhs) noexcept
		{
			return rhs.empty();
		}

		friend bool operator!=(std::nullptr_t, const GCObjectWeakPtr& rhs) noexcept
		{
			return !rhs.empty();
		}

		template<class U>
		bool operator==(const U* ptr) const noexcept
		{
			return get() == ptr;
		}

		template<class U>
		bool operator!=(const U* ptr) const noexcept
		{
			return get() != ptr;
		}

		template<class U>
		friend bool operator==(const U* ptr, const GCObjectWeakPtr& rhs) noexcept
		{
			return ptr == rhs.get();
		}

		template<class U>
		friend bool operator!=(const U* ptr, const GCObjectWeakPtr& rhs) noexcept
		{
			return ptr != rhs.get();
		}

		template<class U>
		bool operator==(const GCMember<U>& rhs) const noexcept
		{
			return get() == rhs.get();
		}

		template<class U>
		bool operator!=(const GCMember<U>& rhs) const noexcept
		{
			return get() != rhs.get();
		}

		template<class U>
		friend bool operator==(const GCMember<U>& lhs, const GCObjectWeakPtr& rhs) noexcept
		{
			return lhs.get() == rhs.get();
		}

		template<class U>
		friend bool operator!=(const GCMember<U>& lhs, const GCObjectWeakPtr& rhs) noexcept
		{
			return lhs.get() != rhs.get();
		}

	private:
		template<class U>
		static IRootsRegistry* requireRegistry(const GCObjectRootPtr<U>& root)
		{
			IRootsRegistry* activeRegistry = root.registry();
			if (!activeRegistry)
				throw std::logic_error("cannot create a weak pointer from a detached root");
			return activeRegistry;
		}
	};

	template<class T, class U>
	inline bool operator==(const GCObjectRootPtr<T>& lhs, const GCObjectWeakPtr<U>& rhs) noexcept
	{
		return lhs.get() == rhs.get();
	}

	template<class T, class U>
	inline bool operator!=(const GCObjectRootPtr<T>& lhs, const GCObjectWeakPtr<U>& rhs) noexcept
	{
		return lhs.get() != rhs.get();
	}
}

#endif // GCOBJECT_WEAK_PTR_H
