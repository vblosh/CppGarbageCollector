#include <gtest/gtest.h>

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

int main(int argc, char** argv) {
    bool unitTests = false;
    if (unitTests)
    {
        ::testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
    }
    else
    {
        return perfomanceTest();
    }
}
