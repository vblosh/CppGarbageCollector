#include <gtest/gtest.h>

#include <memory>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "GarbageCollector.h"
#include "TestGCClasses.h"

using namespace cppgc;

static_assert(!std::is_copy_constructible_v<GarbageCollector>);
static_assert(!std::is_copy_assignable_v<GarbageCollector>);
static_assert(!std::is_move_constructible_v<GarbageCollector>);
static_assert(!std::is_move_assignable_v<GarbageCollector>);
static_assert(!std::is_copy_constructible_v<GCObject>);
static_assert(!std::is_copy_assignable_v<GCObject>);
static_assert(!std::is_move_constructible_v<GCObject>);
static_assert(!std::is_move_assignable_v<GCObject>);

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

TEST(GCTEST, automaticTracingIncludesBaseAndDerivedMembers)
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
    bool collectRejected = false;
    bool countRejected = false;
    bool configuredThresholdRejected = false;
    bool nextThresholdRejected = false;
    bool ownsRejected = false;
    std::thread worker([&]
    {
        try
        {
            gc.collect();
        }
        catch (const std::logic_error&)
        {
            collectRejected = true;
        }
        try { (void)gc.get_objects_count(); } catch (const std::logic_error&) { countRejected = true; }
        try { (void)gc.get_collection_threshold(); } catch (const std::logic_error&) { configuredThresholdRejected = true; }
        try { (void)gc.get_next_collection_threshold(); } catch (const std::logic_error&) { nextThresholdRejected = true; }
        try { (void)gc.owns(nullptr); } catch (const std::logic_error&) { ownsRejected = true; }
    });
    worker.join();

    ASSERT_TRUE(collectRejected);
    ASSERT_TRUE(countRejected);
    ASSERT_TRUE(configuredThresholdRejected);
    ASSERT_TRUE(nextThresholdRejected);
    ASSERT_TRUE(ownsRejected);
}

TEST(GCTEST, zeroRegistrationClassWorks)
{
    struct Zero : public GCObject {};
    GarbageCollector gc;
    GCObjectRootPtr<Zero> root(gc);
    root = gc.createInstance<Zero>();
    ASSERT_EQ(1, gc.get_objects_count());
    gc.collect();
    ASSERT_EQ(1, gc.get_objects_count());
    root.reset();
    gc.collect();
    ASSERT_EQ(0, gc.get_objects_count());
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
    ASSERT_EQ(1025, gc.get_next_collection_threshold());
}

TEST(GCTEST, collectionThresholdCanBeChanged)
{
    GarbageCollector gc;

    gc.set_collection_threshold(4);

    ASSERT_EQ(4, gc.get_collection_threshold());
    ASSERT_EQ(4, gc.get_next_collection_threshold());
}

TEST(GCTEST, adaptiveThresholdAvoidsRepeatedCollection)
{
    GarbageCollector gc(2);
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(1);
    gc.createInstance<Foo>(2);

    gc.createInstance<Foo>(3); // Collects one unreachable object; next threshold becomes 1025.
    for (int id = 4; id <= 20; ++id)
        gc.createInstance<Foo>(id);

    ASSERT_EQ(19, gc.get_objects_count());
    ASSERT_EQ(1025, gc.get_next_collection_threshold());
}

TEST(GCTEST, zeroThresholdKeepsAutomaticCollectionDisabled)
{
    GarbageCollector gc(4);
    gc.set_collection_threshold(0);

    for (int id = 0; id < 10; ++id)
        gc.createInstance<Foo>(id);

    ASSERT_EQ(10, gc.get_objects_count());
    ASSERT_EQ(0, gc.get_collection_threshold());
    ASSERT_EQ(0, gc.get_next_collection_threshold());
}

TEST(GCTEST, adaptiveThresholdSaturatesInsteadOfOverflowing)
{
    const size_t maximum = std::numeric_limits<size_t>::max();

    ASSERT_EQ(maximum,
        detail::calculateNextCollectionThreshold(1, maximum - 100));
    ASSERT_EQ(0,
        detail::calculateNextCollectionThreshold(0, maximum));
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

TEST(GCTEST, removingMemberOutOfRegistrationOrderUpdatesTracing)
{
    GarbageCollector gc;
    GCObjectRootPtr<DynamicMemberOwner> root(gc);
    root = gc.createInstance<DynamicMemberOwner>();
    Foo* firstTarget = gc.createInstance<Foo>(1);
    Foo* secondTarget = gc.createInstance<Foo>(2);
    root->addEdges(firstTarget, secondTarget);

    gc.collect();
    ASSERT_EQ(3, gc.get_objects_count());

    root->removeFirstEdge();
    gc.collect();

    ASSERT_EQ(2, gc.get_objects_count());
    ASSERT_FALSE(gc.owns(firstTarget));
    ASSERT_TRUE(gc.owns(secondTarget));
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

    // Exercise automatic intrusive tracing for a class defined in a shared header.
    GarbageCollector gc;
    GCObjectRootPtr<HeaderDefinedNode> root(gc);
    auto* node = gc.createInstance<HeaderDefinedNode>();
    root = node;
    // Assign another node to exercise automatic member discovery.
    node->next = gc.createInstance<HeaderDefinedNode>();
    gc.collect();
    ASSERT_EQ(2, gc.get_objects_count());
    root.reset();
    gc.collect();
    ASSERT_EQ(0, gc.get_objects_count());
}

int main(int argc, char** argv) 
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
