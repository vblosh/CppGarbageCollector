#ifndef GUARD_GCOBJECT_H
#define GUARD_GCOBJECT_H

#include <cstdint>
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

	class TraceVisitor
	{
	public:
		template<class T>
		void visit(const GCMember<T>& member) const;

		// Legacy raw edge tracing. Only accepts pointers to GCObject-derived types;
		// does not scan arbitrary native pointers or unmanaged memory.
		template<class T>
		void visitRaw(T* pointer) const;

	private:
		using Callback = void (*)(void*, GCObjectPtr);

		friend class GarbageCollector;

		TraceVisitor(
			void* context,
			Callback managedCallback,
			Callback rawCallback) noexcept
			: context(context),
			managedCallback(managedCallback),
			rawCallback(rawCallback)
		{}

		void* context;
		Callback managedCallback;
		Callback rawCallback;
	};

	// Base class for all managed garbage-collected objects.
	// Managed classes must derive non-virtually from GCObject. Virtual inheritance
	// from GCObject is not supported because handle downcasts rely on static_cast.
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

		// Visit every GCMember. Derived overrides must call the base implementation
		// when the base declares managed or legacy raw edges.
		virtual void trace(TraceVisitor&) const {}

	private:
		friend class GarbageCollector;

		uint64_t markEpoch = 0;
	};

	template<class T>
	class GCMember
	{
	public:
		GCMember() noexcept = default;

		explicit GCMember(T* initialValue) noexcept
			: target(initialValue)
		{
			static_assert(std::is_base_of_v<GCObject, T>, "managed type must derive from GCObject");
		}

		GCMember(const GCMember&) noexcept = default;
		GCMember& operator=(const GCMember&) noexcept = default;
		GCMember(GCMember&&) noexcept = default;
		GCMember& operator=(GCMember&&) noexcept = default;

		GCMember& operator=(T* newValue) noexcept
		{
			static_assert(std::is_base_of_v<GCObject, T>, "managed type must derive from GCObject");
			target = newValue;
			return *this;
		}

		T* get() const noexcept
		{
			return target;
		}

		T* operator->() const noexcept
		{
			return get();
		}

		T& operator*() const noexcept
		{
			return *get();
		}

		explicit operator T*() const noexcept
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

		bool operator==(std::nullptr_t) const noexcept
		{
			return target == nullptr;
		}

		bool operator!=(std::nullptr_t) const noexcept
		{
			return target != nullptr;
		}

		friend bool operator==(std::nullptr_t, const GCMember& rhs) noexcept
		{
			return rhs.target == nullptr;
		}

		friend bool operator!=(std::nullptr_t, const GCMember& rhs) noexcept
		{
			return rhs.target != nullptr;
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
		friend bool operator==(const U* ptr, const GCMember& rhs) noexcept
		{
			return ptr == rhs.get();
		}

		template<class U>
		friend bool operator!=(const U* ptr, const GCMember& rhs) noexcept
		{
			return ptr != rhs.get();
		}

	private:
		T* target = nullptr;
	};

	template<class T>
	void TraceVisitor::visit(const GCMember<T>& member) const
	{
		static_assert(std::is_base_of_v<GCObject, T>, "managed type must derive from GCObject");
		managedCallback(context, static_cast<GCObjectPtr>(member.get()));
	}

	template<class T>
	void TraceVisitor::visitRaw(T* pointer) const
	{
		static_assert(std::is_base_of_v<GCObject, T>, "raw edge type must derive from GCObject");
		rawCallback(context, static_cast<GCObjectPtr>(pointer));
	}

	template<class T>
	[[deprecated("getGCObjectPointer is deprecated; use static_cast<GCObjectPtr> or GCMember::get() instead")]]
	GCObjectPtr getGCObjectPointer(T* pointer)
	{
		return static_cast<GCObjectPtr>(pointer);
	}

	template<class T>
	[[deprecated("getGCObjectPointer is deprecated; use static_cast<GCObjectPtr> or GCMember::get() instead")]]
	GCObjectPtr getGCObjectPointer(const GCMember<T>& pointer)
	{
		return static_cast<GCObjectPtr>(pointer.get());
	}
}

#endif // GUARD_GCOBJECT_H
