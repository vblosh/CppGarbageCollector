#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "GarbageCollector.h"
#include "GCPerfTest.h"
#include "TestGCClasses.h"

using namespace cppgc;

class Foo : public GCObject
{
    DECLARE_GCOBJECT_CLASS(Foo)
public:
    Foo(int id)
        : pFoo(this), id(id)
    {}

    virtual ~Foo()
    {}

    GCMember<Foo> pFoo;
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
        : Foo(id), ch(ch), pBoo(this)
    {}

    char ch;
    GCMember<Boo> pBoo;
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
    ChildOfNoAncestor() : child(this)
    {}

    void setChild(Foo* value)
    {
        child = value;
    }

private:
    GCMember<Foo> child;
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

class LegacyRawNode : public GCObject
{
    DECLARE_GCOBJECT_CLASS(LegacyRawNode)
public:
    LegacyRawNode() : next(nullptr)
    {}

    LegacyRawNode* next;
};

GCOBJECT_POINTER_MAP_BEGIN(LegacyRawNode)
GCPOINTER(LegacyRawNode, next)
GCOBJECT_POINTER_MAP_END(LegacyRawNode)

class ConstructorEdgeObject : public GCObject
{
    DECLARE_GCOBJECT_CLASS(ConstructorEdgeObject)
public:
    explicit ConstructorEdgeObject(Foo* child) : child(this, child)
    {}

private:
    GCMember<Foo> child;
};

GCOBJECT_POINTER_MAP_BEGIN(ConstructorEdgeObject)
GCPOINTER(ConstructorEdgeObject, child)
GCOBJECT_POINTER_MAP_END(ConstructorEdgeObject)

class ReentrantObject : public GCObject
{
    DECLARE_GCOBJECT_CLASS_NO_PTR(ReentrantObject)
public:
    ReentrantObject(GarbageCollector& collector, bool& rejected)
        : collector(collector), rejected(rejected)
    {}

    ~ReentrantObject() override
    {
        try
        {
            collector.collect();
        }
        catch (const std::logic_error&)
        {
            rejected = true;
        }
    }

private:
    GarbageCollector& collector;
    bool& rejected;
};

IMPLEMENT_GCOBJECT_CLASS_NO_PTR(ReentrantObject)

class GraphNode : public GCObject
{
    DECLARE_GCOBJECT_CLASS(GraphNode)
public:
    GraphNode() : first(this), second(this)
    {}

    GCMember<GraphNode> first;
    GCMember<GraphNode> second;
};

GCOBJECT_POINTER_MAP_BEGIN(GraphNode)
GCPOINTER(GraphNode, first)
GCPOINTER(GraphNode, second)
GCOBJECT_POINTER_MAP_END(GraphNode)

class ThrowingTraceObject : public GCObject
{
public:
    const ClassInfo* getClassInfo() const override
    {
        return &classInfo;
    }

    static void trace(GCObjectPtr, GCPointerList&)
    {
        if (shouldThrow)
        {
            shouldThrow = false;
            throw std::runtime_error("trace failure");
        }
    }

    static bool shouldThrow;
    static const ClassInfo classInfo;
};

bool ThrowingTraceObject::shouldThrow = false;
const ClassInfo ThrowingTraceObject::classInfo{
    sizeof(ThrowingTraceObject), alignof(ThrowingTraceObject), &ThrowingTraceObject::trace, nullptr };

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

TEST(GCTEST, rejectsManagedEdgeFromAnotherCollector)
{
    GarbageCollector first;
    GarbageCollector second;
    GCObjectRootPtr<Foo> root(first);
    root = first.createInstance<Foo>(1);

    ASSERT_THROW(root->pFoo = second.createInstance<Foo>(2), std::invalid_argument);
    ASSERT_EQ(nullptr, root->pFoo.get());
}

TEST(GCTEST, validatesEdgesCreatedByConstructors)
{
    GarbageCollector first;
    GarbageCollector second;
    Foo* foreign = second.createInstance<Foo>(1);

    ASSERT_THROW(first.createInstance<ConstructorEdgeObject>(foreign), std::invalid_argument);
    ASSERT_EQ(0, first.get_objects_count());
}

TEST(GCTEST, ignoresForeignAndStaleLegacyRawEdges)
{
    GarbageCollector first;
    GarbageCollector second;
    GCObjectRootPtr<LegacyRawNode> root(first);
    root = first.createInstance<LegacyRawNode>();
    root->next = second.createInstance<LegacyRawNode>();

    first.collect();
    ASSERT_EQ(1, first.get_objects_count());

    second.collect();
    ASSERT_EQ(0, second.get_objects_count());

    first.collect();
    ASSERT_EQ(1, first.get_objects_count());
}

TEST(GCTEST, rejectsReentrantCollection)
{
    GarbageCollector gc;
    bool rejected = false;
    gc.createInstance<ReentrantObject>(gc, rejected);

    gc.collect();

    ASSERT_TRUE(rejected);
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, collectionRecoversAfterTraceFailure)
{
    GarbageCollector gc;
    GCObjectRootPtr<ThrowingTraceObject> root(gc);
    root = gc.createInstance<ThrowingTraceObject>();
    ThrowingTraceObject::shouldThrow = true;

    ASSERT_THROW(gc.collect(), std::runtime_error);
    ASSERT_NO_THROW(gc.collect());
    ASSERT_EQ(1, gc.get_objects_count());
}

TEST(GCTEST, rejectsUseFromAnotherThread)
{
    GarbageCollector gc;
    bool rejected = false;
    std::thread worker([&]
    {
        try
        {
            gc.collect();
        }
        catch (const std::logic_error&)
        {
            rejected = true;
        }
    });
    worker.join();

    ASSERT_TRUE(rejected);
}

TEST(GCTEST, collectionThresholdTriggersBeforeAllocation)
{
    GarbageCollector gc(2);
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(1);
    gc.createInstance<Foo>(2);

    Foo* newest = gc.createInstance<Foo>(3);

    ASSERT_EQ(2, gc.get_objects_count());
    ASSERT_TRUE(gc.owns(newest));
    ASSERT_EQ(2, gc.get_collection_threshold());
}

TEST(GCTEST, collectionThresholdCanBeChanged)
{
    GarbageCollector gc;

    gc.set_collection_threshold(4);

    ASSERT_EQ(4, gc.get_collection_threshold());
}

TEST(GCTEST, randomizedGraphCollectsUnreachableComponent)
{
    constexpr int componentSize = 500;
    GarbageCollector gc;
    GCObjectRootPtr<GraphNode> root(gc);
    std::vector<GraphNode*> nodes;
    nodes.reserve(componentSize * 2);
    for (int index = 0; index < componentSize * 2; ++index)
        nodes.push_back(gc.createInstance<GraphNode>());

    std::mt19937 random(12345);
    for (int index = 0; index < componentSize * 2; ++index)
    {
        const int componentStart = index < componentSize ? 0 : componentSize;
        std::uniform_int_distribution<int> target(componentStart, componentStart + componentSize - 1);
        nodes[index]->first = nodes[target(random)];
        nodes[index]->second = nodes[target(random)];
    }
    for (int index = 0; index < componentSize - 1; ++index)
        nodes[index]->first = nodes[index + 1];

    root = nodes.front();
    gc.collect();

    ASSERT_EQ(componentSize, gc.get_objects_count());
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
        tail = tail->pFoo.get();
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

TEST(GCOBJECT, headerMetadataIsSharedAcrossTranslationUnits)
{
    ASSERT_EQ(HeaderDefinedNode::GetClassInfo(),
        getHeaderDefinedNodeInfoFromOtherTranslationUnit());
}

int main(int argc, char** argv) 
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
