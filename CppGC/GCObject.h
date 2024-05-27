#ifndef GUARD_GCOBJECT_H
#define GUARD_GCOBJECT_H

namespace cppgc
{
	struct  ClassInfo
	{
		size_t sizeOf;
		size_t alignOf;
		size_t pointersCount;
		size_t* ptrOffsets;
		ClassInfo* parentInfo;
	};

	class GCObject
	{
	public:
		virtual ~GCObject() {}
		virtual ClassInfo* getClassInfo() = 0;
		bool visited = false;
	};

	/*****************************************
		Macros for classes without GC pointers, using:

	class Parent : public GCObject
	{
	DECLARE_GCOBJECT_CLASS_NO_PTR(Parent)
	};

	IMPLEMENT_GCOBJECT_CLASS_NO_PTR(Parent)

	class Child : public Parent
	{
	DECLARE_GCOBJECT_CLASS_NO_PTR(Child)
	};

	IMPLEMENT_GCOBJECT_CLASS_NO_PTR_WITH_PARENT(Child, Parent)
	 *****************************************/

#define DECLARE_GCOBJECT_CLASS_NO_PTR(Type) \
public: \
static ClassInfo classInfo; \
virtual ClassInfo* getClassInfo() override { return &classInfo; }; \

#define IMPLEMENT_GCOBJECT_CLASS_NO_PTR(Type) \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), 0, nullptr, nullptr }; \

#define IMPLEMENT_GCOBJECT_CLASS_NO_PTR_WITH_PARENT(Type, Parent) \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), 0, nullptr, &Parent::classInfo }; \

	 /*****************************************
		 Macros for classes with GC pointers, using:

	class Parent : public GCObject
	{
	DECLARE_GCOBJECT_CLASS(Parent)
	public:
		Parent() {}
		~Parent() {}


	private:
		Parent * left, * right;
	};

	GCOBJECT_POINTER_MAP_BEGIN(Parent)
	GCPOINTER(Parent, left)
	GCPOINTER(Parent, right)
	GCOBJECT_POINTER_MAP_END(Parent)


	class Child : public Parent
	{
		DECLARE_GCOBJECT_CLASS(Child)
	public:
		Child() {}
		~Child() {}


	private:
		Parent* left1, * right1;
	};

	GCOBJECT_POINTER_MAP_BEGIN(Child)
	GCPOINTER(Child, left1)
	GCPOINTER(Child, right1)
	GCOBJECT_POINTER_MAP_WITH_PARENT_END(Child, Parent)
	*****************************************/

#define DECLARE_GCOBJECT_CLASS(Type) \
public: \
virtual ClassInfo* getClassInfo() override { return &classInfo; }; \
static ClassInfo* GetClassInfo() { return &classInfo; }; \
private: \
static ClassInfo classInfo; \
static size_t ptrOffsets[]; \

#define IMPLEMENT_GCOBJECT_CLASS(Type) \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), sizeof(Type::ptrOffsets) / sizeof(size_t), Type::ptrOffsets, nullptr }; \

#define GCOBJECT_POINTER_MAP_BEGIN(Type) \
size_t Type::ptrOffsets[] = { \

#define GCPOINTER(Type, Member) \
    offsetof(Type, Member), \

#define GCOBJECT_POINTER_MAP_END(Type) }; \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), sizeof(Type::ptrOffsets) / sizeof(size_t), Type::ptrOffsets, nullptr }; \

#define GCOBJECT_POINTER_MAP_WITH_PARENT_END(Type, Parent) }; \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), sizeof(Type::ptrOffsets) / sizeof(size_t), Type::ptrOffsets, Parent::GetClassInfo() }; \

inline bool isSubclassOf(GCObject* descendant, GCObject* ancestor)
{
	ClassInfo* currentInfo = descendant->getClassInfo();
	ClassInfo* ancestorInfo = ancestor->getClassInfo();
	do {
		if (currentInfo == ancestorInfo)
			return true;
		else
			currentInfo = currentInfo->parentInfo;
	} while (currentInfo);
	return false;
}

inline bool isSameType(GCObject* left, GCObject* right)
{
	return left->getClassInfo() == right->getClassInfo();
}

template<typename T>
bool isTypeOf(GCObject* object)
{
	return T::GetClassInfo() == object->getClassInfo();
}

}
#endif //GUARD_GCOBJECT_H

