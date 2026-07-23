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
	class GCMemberBase;
	template<class T>
	class GCMember;

	using GCObjectPtr = GCObject*;
	using GCPointerList = std::vector<GCObjectPtr>;

	class GCObject
	{
	public:
		GCObject() = default;
		GCObject(const GCObject&) = delete;
		GCObject& operator=(const GCObject&) = delete;
		GCObject(GCObject&&) = delete;
		GCObject& operator=(GCObject&&) = delete;

		// Sweep order is unspecified; destructors must not dereference managed peers.
		virtual ~GCObject() = default;

		// Override only for legacy raw pointers or custom non-GCMember edges.
		// Derived overrides must call the base hook when the base traces such edges.
		virtual void traceAdditional(GCPointerList&) const {}

	private:
		friend class GarbageCollector;
		friend class GCMemberBase;
		template<class T>
		friend class GCMember;

		GCMemberBase* firstMember = nullptr;
		const void* collectorIdentity = nullptr;
		uint64_t markEpoch = 0;

		bool hasCollectorIdentity() const noexcept { return collectorIdentity != nullptr; }
		bool sharesCollectorIdentity(const GCObject* other) const noexcept
		{
			return other && collectorIdentity == other->collectorIdentity;
		}
	};

	class GCMemberBase
	{
		friend class GarbageCollector;

	protected:
		explicit GCMemberBase(GCObject& containingObject) noexcept
			: owner(&containingObject), next(containingObject.firstMember)
		{
			if (next)
				next->previous = this;
			containingObject.firstMember = this;
		}

		~GCMemberBase()
		{
			if (previous)
				previous->next = next;
			else
				owner->firstMember = next;
			if (next)
				next->previous = previous;
		}

		GCMemberBase(const GCMemberBase&) = delete;
		GCMemberBase& operator=(const GCMemberBase&) = delete;
		GCMemberBase(GCMemberBase&&) = delete;
		GCMemberBase& operator=(GCMemberBase&&) = delete;

		GCObject* owner;
		GCObject* target = nullptr;

	private:
		GCMemberBase* next;
		GCMemberBase* previous = nullptr;
	};

	template<class T>
	class GCMember : private GCMemberBase
	{
	public:
		explicit GCMember(GCObject& containingObject) noexcept
			: GCMemberBase(containingObject)
		{
			static_assert(std::is_base_of_v<GCObject, T>, "managed type must derive from GCObject");
		}

		GCMember(GCObject& containingObject, T* initialValue)
			: GCMemberBase(containingObject)
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
			return static_cast<T*>(target);
		}

		T* operator->() const noexcept
		{
			return get();
		}

		operator T*() const noexcept
		{
			return get();
		}

		explicit operator bool() const noexcept
		{
			return target != nullptr;
		}

		void reset() noexcept
		{
			target = nullptr;
		}

	private:
		void assign(T* newValue)
		{
			GCObject* newTarget = static_cast<GCObject*>(newValue);
			if (newTarget && owner && owner->hasCollectorIdentity() &&
				!owner->sharesCollectorIdentity(newTarget))
			{
				throw std::invalid_argument("managed edge crosses collector ownership");
			}
			target = newTarget;
		}
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

}

#endif // GUARD_GCOBJECT_H
