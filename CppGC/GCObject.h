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

	class Boo : public GCObject
	{
	DECLARE_GCOBJECT_CLASS_NO_PTR
	};

	IMPLEMENT_GCOBJECT_CLASS_NO_PTR(Boo)

	class Boo : public Parent
	{
	DECLARE_GCOBJECT_CLASS_NO_PTR
	};

	IMPLEMENT_GCOBJECT_CLASS_NO_PTR_WITH_PARENT(Boo, Parent)
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
static ClassInfo classInfo; \
static size_t ptrOffsets[]; \
virtual ClassInfo* getClassInfo() override { return &classInfo; }; \

#define IMPLEMENT_GCOBJECT_CLASS(Type) \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), sizeof(Type::ptrOffsets) / sizeof(size_t), Type::ptrOffsets, nullptr }; \

#define GCOBJECT_POINTER_MAP_BEGIN(Type) \
size_t Type::ptrOffsets[] = { \

#define GCPOINTER(Type, Member) \
    offsetof(Type, Member), \

#define GCOBJECT_POINTER_MAP_END(Type) }; \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), sizeof(Type::ptrOffsets) / sizeof(size_t), Type::ptrOffsets, nullptr }; \

#define GCOBJECT_POINTER_MAP_WITH_PARENT_END(Type, Parent) }; \
ClassInfo Type::classInfo{ sizeof(Type), alignof(Type), sizeof(Type::ptrOffsets) / sizeof(size_t), Type::ptrOffsets, &Parent::classInfo }; \

}
#endif //GUARD_GCOBJECT_H

