#ifndef GUARD_GCOBJECT_H
#define GUARD_GCOBJECT_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cppgc
{
	class GarbageCollector;
	class GCObject;
	template<class T>
	class GCMember;

	using GCObjectPtr = GCObject*;
	using GCPointerList = std::vector<GCObjectPtr>;
	using TracePointers = void (*)(GCObjectPtr, GCPointerList&);

	struct ClassInfo
	{
		size_t sizeOf;
		size_t alignOf;
		TracePointers tracePointers;
		const ClassInfo* parentInfo;
	};

	class GCObject
	{
	public:
		// Sweep order is unspecified; destructors must not dereference managed peers.
		virtual ~GCObject() = default;
		virtual const ClassInfo* getClassInfo() const = 0;

	private:
		friend class GarbageCollector;
		template<class T>
		friend class GCMember;

		const void* collectorIdentity = nullptr;
		uint64_t markEpoch = 0;
	};

	template<class T>
	class GCMember
	{
	public:
		explicit GCMember(GCObject* owner = nullptr) noexcept
			: owner(owner)
		{}

		GCMember(GCObject* owner, T* initialValue)
			: owner(owner)
		{
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
			if (target && owner && owner->collectorIdentity &&
				target->collectorIdentity != owner->collectorIdentity)
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

#define DECLARE_GCOBJECT_CLASS_NO_PTR(Type) \
public: \
static const cppgc::ClassInfo classInfo; \
static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; } \
virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

#define IMPLEMENT_GCOBJECT_CLASS_NO_PTR(Type) \
inline const cppgc::ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), nullptr, nullptr };

#define IMPLEMENT_GCOBJECT_CLASS_NO_PTR_WITH_PARENT(Type, Parent) \
inline const cppgc::ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), nullptr, Parent::GetClassInfo() };

#define DECLARE_GCOBJECT_CLASS(Type) \
public: \
virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; } \
static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; } \
private: \
static void tracePointers(cppgc::GCObjectPtr object, cppgc::GCPointerList& pointers); \
static const cppgc::ClassInfo classInfo;

#define IMPLEMENT_GCOBJECT_CLASS(Type) \
inline const cppgc::ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), &Type::tracePointers, nullptr };

#define GCOBJECT_POINTER_MAP_BEGIN(Type) \
inline void Type::tracePointers(cppgc::GCObjectPtr object, cppgc::GCPointerList& pointers) \
{ \
	Type* self = static_cast<Type*>(object);

#define GCPOINTER(Type, Member) \
	pointers.push_back(cppgc::getGCObjectPointer(self->Member));

#define GCOBJECT_POINTER_MAP_END(Type) \
} \
inline const cppgc::ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), &Type::tracePointers, nullptr };

#define GCOBJECT_POINTER_MAP_WITH_PARENT_END(Type, Parent) \
} \
inline const cppgc::ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), &Type::tracePointers, Parent::GetClassInfo() };

	inline bool isSubclassOf(const GCObject* descendant, const GCObject* ancestor)
	{
		if (!descendant || !ancestor)
			return false;

		const ClassInfo* currentInfo = descendant->getClassInfo();
		const ClassInfo* ancestorInfo = ancestor->getClassInfo();
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
		return left && right && left->getClassInfo() == right->getClassInfo();
	}

	template<typename T>
	bool isTypeOf(const GCObject* object)
	{
		return object && T::GetClassInfo() == object->getClassInfo();
	}
}

#endif // GUARD_GCOBJECT_H
