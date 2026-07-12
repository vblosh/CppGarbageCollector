#ifndef GUARD_GCOBJECT_H
#define GUARD_GCOBJECT_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace cppgc
{
	class GarbageCollector;
	class GCObject;
	template<class T>
	class GCMember;

	using GCObjectPtr = GCObject*;
	using GCPointerList = std::vector<GCObjectPtr>;

	struct ClassInfo
	{
		const ClassInfo* parentInfo;
	};

	// NOTE: Most users need *no* ClassInfo registration.
	// Override only virtual trace(GCPointerList&) for managed pointers.
	// If you use the is* free functions (isTypeOf<T>, isSubclassOf, isSameType)
	// or rely on GetClassInfo() for multi-TU identity, provide a minimal
	// static registration in your class (see README "Pointer-free and inherited types").

	class GCObject
	{
	public:
		// Sweep order is unspecified; destructors must not dereference managed peers.
		virtual ~GCObject() = default;

		// Override to trace managed pointers (use getGCObjectPointer for members).
		// IMPORTANT for inheritance: if your base class has traced members,
		// call Base::trace(pointers); inside your override so the full chain is visited.
		virtual void trace(GCPointerList& pointers) const {}

		virtual const ClassInfo* getClassInfo() const { return nullptr; }

	private:
		friend class GarbageCollector;
		template<class T>
		friend class GCMember;

		const void* collectorIdentity = nullptr;
		uint64_t markEpoch = 0;

		bool hasCollectorIdentity() const noexcept { return collectorIdentity != nullptr; }
		bool sharesCollectorIdentity(const GCObject* other) const noexcept
		{
			return other && collectorIdentity == other->collectorIdentity;
		}
	};

	template<class T>
	class GCMember
	{
	public:
		explicit GCMember(GCObject* owner = nullptr) noexcept
			: owner(owner)
		{
			static_assert(std::is_base_of_v<GCObject, T>, "managed type must derive from GCObject");
		}

		GCMember(GCObject* owner, T* initialValue)
			: owner(owner)
		{
			static_assert(std::is_base_of_v<GCObject, T>, "managed type must derive from GCObject");
			assign(initialValue);
		}

		GCMember(const GCMember&) = delete;
		GCMember& operator=(const GCMember&) = delete;

		GCMember& operator=(T* newValue)
		{
			assign(newValue);
			return *this;
		}

		T* get() const noexcept
		{
			return value;
		}

		T* operator->() const noexcept
		{
			return value;
		}

		operator T*() const noexcept
		{
			return value;
		}

		explicit operator bool() const noexcept
		{
			return value != nullptr;
		}

		void reset() noexcept
		{
			value = nullptr;
		}

	private:
		void assign(T* newValue)
		{
			GCObject* target = static_cast<GCObject*>(newValue);
			if (target && owner && owner->hasCollectorIdentity() &&
				!owner->sharesCollectorIdentity(target))
			{
				throw std::invalid_argument("managed edge crosses collector ownership");
			}
			value = newValue;
		}

		GCObject* owner;
		T* value = nullptr;
	};

	template<class T>
	GCObjectPtr getGCObjectPointer(T* pointer)
	{
		return static_cast<GCObjectPtr>(pointer);
	}

	template<class T>
	GCObjectPtr getGCObjectPointer(const GCMember<T>& pointer)
	{
		return static_cast<GCObjectPtr>(pointer.get());
	}

	inline bool isSubclassOf(const GCObject* descendant, const GCObject* ancestor)
	{
		if (!descendant || !ancestor)
			return false;

		const ClassInfo* currentInfo = descendant->getClassInfo();
		const ClassInfo* ancestorInfo = ancestor->getClassInfo();
		if (!currentInfo || !ancestorInfo)
			return false;
		do
		{
			if (currentInfo == ancestorInfo)
				return true;
			currentInfo = currentInfo->parentInfo;
		} while (currentInfo);
		return false;
	}

	inline bool isSameType(const GCObject* left, const GCObject* right)
	{
		if (!left || !right)
			return false;
		const ClassInfo* li = left->getClassInfo();
		const ClassInfo* ri = right->getClassInfo();
		return li && ri && li == ri;
	}

	template<typename T>
	bool isTypeOf(const GCObject* object)
	{
		const ClassInfo* ti = T::GetClassInfo();
		return object && ti && ti == object->getClassInfo();
	}
}

#endif // GUARD_GCOBJECT_H
