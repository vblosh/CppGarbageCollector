#ifndef GUARD_GCOBJECT_H
#define GUARD_GCOBJECT_H

#include <cstddef>
#include <vector>

namespace cppgc
{
	class GarbageCollector;
	class GCObject;

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
		virtual ~GCObject() = default;
		virtual const ClassInfo* getClassInfo() const = 0;

	private:
		friend class GarbageCollector;
		bool visited = false;
	};

#define DECLARE_GCOBJECT_CLASS_NO_PTR(Type) \
public: \
static ClassInfo classInfo; \
static const ClassInfo* GetClassInfo() { return &classInfo; } \
virtual const ClassInfo* getClassInfo() const override { return &classInfo; }

#define IMPLEMENT_GCOBJECT_CLASS_NO_PTR(Type) \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), nullptr, nullptr };

#define IMPLEMENT_GCOBJECT_CLASS_NO_PTR_WITH_PARENT(Type, Parent) \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), nullptr, Parent::GetClassInfo() };

#define DECLARE_GCOBJECT_CLASS(Type) \
public: \
virtual const ClassInfo* getClassInfo() const override { return &classInfo; } \
static const ClassInfo* GetClassInfo() { return &classInfo; } \
private: \
static void tracePointers(GCObjectPtr object, GCPointerList& pointers); \
static ClassInfo classInfo;

#define IMPLEMENT_GCOBJECT_CLASS(Type) \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), &Type::tracePointers, nullptr };

#define GCOBJECT_POINTER_MAP_BEGIN(Type) \
void Type::tracePointers(GCObjectPtr object, GCPointerList& pointers) \
{ \
	Type* self = static_cast<Type*>(object);

#define GCPOINTER(Type, Member) \
	pointers.push_back(self->Member);

#define GCOBJECT_POINTER_MAP_END(Type) \
} \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), &Type::tracePointers, nullptr };

#define GCOBJECT_POINTER_MAP_WITH_PARENT_END(Type, Parent) \
} \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), &Type::tracePointers, Parent::GetClassInfo() };

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
