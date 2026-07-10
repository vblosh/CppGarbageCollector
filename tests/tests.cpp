#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>

#include "GarbageCollector.h"
#include "GCPerfTest.h"

using namespace cppgc;

class Foo : public GCObject
{
    DECLARE_GCOBJECT_CLASS(Foo)
public:
    Foo(int id)
        : pFoo(nullptr), id(id)
    {}

    virtual ~Foo()
    {}

    Foo* pFoo;
    int id;
};

GCOBJECT_POINTER_MAP_BEGIN(Foo)
GCPOINTER(Foo, pFoo)
GCOBJECT_POINTER_MAP_END(Foo)

class Boo : public Foo
{
    DECLARE_GCOBJECT_CLASS(Boo)
public:
    Boo(int id, char ch)
        : Foo(id), ch(ch), pBoo(nullptr)
    {}

    char ch;
    Boo* pBoo;
};

GCOBJECT_POINTER_MAP_BEGIN(Boo)
GCPOINTER(Boo, pBoo)
GCOBJECT_POINTER_MAP_WITH_PARENT_END(Boo, Foo)

class NoAncestor : public GCObject
{
    DECLARE_GCOBJECT_CLASS_NO_PTR(NoAncestor)
};

IMPLEMENT_GCOBJECT_CLASS_NO_PTR(NoAncestor)

class ChildOfNoAncestor : public NoAncestor
{
    DECLARE_GCOBJECT_CLASS(ChildOfNoAncestor)
public:
    ChildOfNoAncestor() : child(nullptr)
    {}

    void setChild(Foo* value)
    {
        child = value;
    }

private:
    Foo* child;
};

GCOBJECT_POINTER_MAP_BEGIN(ChildOfNoAncestor)
GCPOINTER(ChildOfNoAncestor, child)
GCOBJECT_POINTER_MAP_WITH_PARENT_END(ChildOfNoAncestor, NoAncestor)

class ThrowingObject : public GCObject
{
    DECLARE_GCOBJECT_CLASS_NO_PTR(ThrowingObject)
public:
    ThrowingObject()
    {
        throw std::runtime_error("constructor failure");
    }
};

IMPLEMENT_GCOBJECT_CLASS_NO_PTR(ThrowingObject)

static_assert(!std::is_copy_constructible_v<GarbageCollector>);
static_assert(!std::is_copy_assignable_v<GarbageCollector>);
static_assert(!std::is_move_constructible_v<GarbageCollector>);
static_assert(!std::is_move_assignable_v<GarbageCollector>);

TEST(GCTEST, testRoots1)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root1(gc);
    GCObjectRootPtr<Foo> root2(gc);
    root1 = gc.createInstance<Foo>(1);
    root2 = gc.createInstance<Foo>(2);
    ASSERT_EQ(2, gc.get_objects_count());
    root1 = nullptr;
    gc.collect();
    ASSERT_EQ(1, gc.get_objects_count());
}

TEST(GCTEST, testCyclicRefs1)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root1(gc);
    GCObjectRootPtr<Foo> root2(gc);
    root1 = gc.createInstance<Foo>(1);
    root2 = gc.createInstance<Foo>(2);
    root1->pFoo = root2.get();
    root2->pFoo = root1.get();
    ASSERT_EQ(2, gc.get_objects_count());
    root1 = nullptr;
    root2 = nullptr;
    gc.collect();
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, testTree1)
{
    GarbageCollector gc;
    GCObjectRootPtr<Boo> root11(gc);
    root11 = gc.createInstance<Boo>(11, 'a');
    root11->pFoo = gc.createInstance<Boo>(12, 'b');
    root11->pBoo = gc.createInstance<Boo>(13, 'c');
    ASSERT_EQ(3, gc.get_objects_count());
    gc.collect();
    ASSERT_EQ(3, gc.get_objects_count());
    root11.reset();
    gc.collect();
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, rootCanOutliveCollector)
{
    auto gc = std::make_unique<GarbageCollector>();
    auto root = std::make_unique<GCObjectRootPtr<Foo>>(*gc);
    *root = gc->createInstance<Foo>(1);

    gc.reset();

    ASSERT_TRUE(root->empty());
    ASSERT_NO_THROW(root.reset());
}

TEST(GCTEST, rejectsRootFromAnotherCollector)
{
    GarbageCollector first;
    GarbageCollector second;
    GCObjectRootPtr<Foo> root(first);
    Foo* foreign = second.createInstance<Foo>(1);

    ASSERT_THROW(root = foreign, std::invalid_argument);
    ASSERT_TRUE(root.empty());
}

TEST(GCTEST, ignoresForeignAndStaleObjectEdges)
{
    GarbageCollector first;
    GarbageCollector second;
    GCObjectRootPtr<Foo> root(first);
    root = first.createInstance<Foo>(1);
    root->pFoo = second.createInstance<Foo>(2);

    first.collect();
    ASSERT_EQ(1, first.get_objects_count());

    second.collect();
    ASSERT_EQ(0, second.get_objects_count());

    first.collect();
    ASSERT_EQ(1, first.get_objects_count());
}

TEST(GCTEST, marksDeepGraphsWithoutCallStackRecursion)
{
    constexpr int nodeCount = 100000;
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(0);
    Foo* tail = root.get();

    for (int id = 1; id < nodeCount; ++id)
    {
        tail->pFoo = gc.createInstance<Foo>(id);
        tail = tail->pFoo;
    }

    gc.collect();
    ASSERT_EQ(nodeCount, gc.get_objects_count());

    root.reset();
    gc.collect();
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, constructorFailureDoesNotRegisterObject)
{
    GarbageCollector gc;

    ASSERT_THROW(gc.createInstance<ThrowingObject>(), std::runtime_error);
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCOBJECT, isSubclassTrue)
{
    GarbageCollector gc;
    ASSERT_TRUE(isSubclassOf(gc.createInstance<Foo>(1), gc.createInstance<Foo>(1)));
    ASSERT_TRUE(isSubclassOf(gc.createInstance<Boo>(1, 'a'), gc.createInstance<Foo>(1)));
}

TEST(GCOBJECT, isSubclassFalse)
{
    GarbageCollector gc;
    ASSERT_FALSE(isSubclassOf(gc.createInstance<NoAncestor>(), gc.createInstance<Foo>(1)));
}

TEST(GCOBJECT, isSameTypeTrue)
{
    GarbageCollector gc;
    ASSERT_TRUE(isSameType(gc.createInstance<Foo>(2), gc.createInstance<Foo>(1)));
}

TEST(GCOBJECT, isSameTypeFalse)
{
    GarbageCollector gc;
    ASSERT_FALSE(isSameType(gc.createInstance<NoAncestor>(), gc.createInstance<Foo>(1)));
}

TEST(GCOBJECT, isTypeOfTrue)
{
    GarbageCollector gc;
    ASSERT_TRUE(isTypeOf<Foo>(gc.createInstance<Foo>(1)));
}

TEST(GCOBJECT, isTypeOfFalse)
{
    GarbageCollector gc;
    ASSERT_FALSE(isTypeOf<Boo>(gc.createInstance<Foo>(1)));
}

TEST(GCOBJECT, pointerFreeClassSupportsIsTypeOf)
{
    GarbageCollector gc;
    ASSERT_TRUE(isTypeOf<NoAncestor>(gc.createInstance<NoAncestor>()));
}

TEST(GCOBJECT, pointerClassCanDeriveFromPointerFreeClass)
{
    GarbageCollector gc;
    GCObjectRootPtr<ChildOfNoAncestor> root(gc);
    auto* child = gc.createInstance<ChildOfNoAncestor>();
    auto* parent = gc.createInstance<NoAncestor>();
    root = child;
    child->setChild(gc.createInstance<Foo>(1));

    ASSERT_TRUE(isSubclassOf(child, parent));
    ASSERT_TRUE(isTypeOf<ChildOfNoAncestor>(child));

    gc.collect();
    ASSERT_EQ(2, gc.get_objects_count());
}

TEST(GCOBJECT, nullTypeQueriesAreSafe)
{
    ASSERT_FALSE(isSubclassOf(nullptr, nullptr));
    ASSERT_FALSE(isSameType(nullptr, nullptr));
    ASSERT_FALSE(isTypeOf<Foo>(nullptr));
}

int main(int argc, char** argv) 
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
