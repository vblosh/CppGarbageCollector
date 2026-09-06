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
static_assert(sizeof(GCMember<Foo>) == sizeof(void*));
static_assert(std::is_trivially_copyable_v<GCMember<Foo>>);

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

TEST(GCTEST, duplicateRootsTraceObjectOncePerCollection)
{
    GarbageCollector gc;
    GCObjectRootPtr<CountingTraceObject> first(gc);
    GCObjectRootPtr<CountingTraceObject> second(gc);
    int traceCount = 0;
    first = gc.createInstance<CountingTraceObject>(traceCount);
    second = first.get();
    traceCount = 0;

    gc.collect();

    ASSERT_EQ(1, traceCount);
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

TEST(GCTEST, explicitTracingIncludesBaseAndDerivedMembers)
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

TEST(GCTEST, detectsManagedEdgeFromAnotherCollectorDuringCollection)
{
    GarbageCollector first;
    GarbageCollector second;
    GCObjectRootPtr<Foo> root(first);
    root = first.createInstance<Foo>(1);
    Foo* foreign = second.createInstance<Foo>(2);

    ASSERT_NO_THROW(root->pFoo = foreign);
    ASSERT_EQ(foreign, root->pFoo.get());
    ASSERT_THROW(first.collect(), std::invalid_argument);

    root->pFoo.reset();
    ASSERT_NO_THROW(first.collect());
    ASSERT_EQ(1, first.get_objects_count());
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

TEST(GCTEST, ignoresForeignLegacyRawEdgeCreatedByConstructor)
{
    GarbageCollector first;
    GarbageCollector second;
    GCObjectRootPtr<LegacyRawNode> root(first);
    LegacyRawNode* foreign = second.createInstance<LegacyRawNode>();

    ASSERT_NO_THROW(root = first.createInstance<LegacyRawNode>(foreign));
    ASSERT_NO_THROW(first.collect());
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
    const size_t fooSize = sizeof(Foo);
    GarbageCollector gc(fooSize * 2);
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(1);
    gc.createInstance<Foo>(2);

    Foo* newest = gc.createInstance<Foo>(3);

    ASSERT_EQ(2, gc.get_objects_count());
    ASSERT_TRUE(gc.owns(newest));
    ASSERT_EQ(fooSize * 2, gc.get_collection_threshold());
    ASSERT_EQ(fooSize + std::max(fooSize / 2, detail::minimumThresholdGrowthBytes),
        gc.get_next_collection_threshold());
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
    const size_t fooSize = sizeof(Foo);
    GarbageCollector gc(fooSize * 2);
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(1);
    gc.createInstance<Foo>(2);

    gc.createInstance<Foo>(3);
    for (int id = 4; id <= 20; ++id)
        gc.createInstance<Foo>(id);

    ASSERT_EQ(19, gc.get_objects_count());
    ASSERT_EQ(fooSize + std::max(fooSize / 2, detail::minimumThresholdGrowthBytes),
        gc.get_next_collection_threshold());
}

TEST(GCTEST, collectionThresholdUsesObjectBytes)
{
    struct LargeObject : GCObject
    {
        char payload[sizeof(Foo) * 4]{};
    };

    const size_t fooSize = sizeof(Foo);
    GarbageCollector gc(fooSize * 3);
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(1);
    gc.createInstance<LargeObject>();

    Foo* newest = gc.createInstance<Foo>(2);

    ASSERT_EQ(2, gc.get_objects_count());
    ASSERT_TRUE(gc.owns(newest));
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

TEST(GCTEST, pointerRegistryReusesDeletedSlotsAndRehashes)
{
	detail::PointerRegistry registry;
	std::vector<GCObject> objects(32);

	for (auto& object : objects)
		ASSERT_TRUE(registry.insert(&object));

	ASSERT_FALSE(registry.insert(&objects.front()));
	ASSERT_TRUE(registry.erase(&objects[7]));
	ASSERT_FALSE(registry.contains(&objects[7]));
	ASSERT_TRUE(registry.insert(&objects[7]));

	for (auto& object : objects)
		ASSERT_TRUE(registry.contains(&object));

	ASSERT_TRUE(registry.erase(&objects.front()));
	ASSERT_FALSE(registry.erase(&objects.front()));
	registry.clear();
	for (auto& object : objects)
		ASSERT_FALSE(registry.contains(&object));
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

TEST(GCTEST, registryRemainsConsistentAfterRepeatedSweepsAndAllocations)
{
	constexpr int liveCount = 2000;
	constexpr int garbagePerRound = 4000;
	GarbageCollector gc;
	GCObjectRootPtr<Foo> root(gc);
	root = gc.createInstance<Foo>(0);
	Foo* tail = root.get();
	for (int id = 1; id < liveCount; ++id)
	{
		tail->pFoo = gc.createInstance<Foo>(id);
		tail = tail->pFoo.get();
	}

	for (int round = 0; round < 3; ++round)
	{
		std::vector<Foo*> garbage;
		garbage.reserve(garbagePerRound);
		for (int id = 0; id < garbagePerRound; ++id)
		{
			Foo* object = gc.createInstance<Foo>(id);
			garbage.push_back(object);
			ASSERT_TRUE(gc.owns(object));
		}

		gc.collect();

		ASSERT_EQ(liveCount, gc.get_objects_count());
		ASSERT_TRUE(gc.owns(root.get()));
		ASSERT_TRUE(gc.owns(tail));
		for (auto pointer : garbage)
			ASSERT_FALSE(gc.owns(pointer));
	}
}

TEST(GCTEST, removingDynamicMemberStopsTracingItsTarget)
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

TEST(GCOBJECT, managedMemberCanDeriveFromPointerFreeClass)
{
    GarbageCollector gc;
    GCObjectRootPtr<ChildWithMember> root(gc);
    auto* child = gc.createInstance<ChildWithMember>();
    gc.createInstance<PointerFreeBase>();
    root = child;
    child->setChild(gc.createInstance<Foo>(1));

    gc.collect();
    ASSERT_EQ(2, gc.get_objects_count());
}

TEST(GCTEST, weakPointerDoesNotKeepObjectAlive)
{
    GarbageCollector gc;
    GCObjectWeakPtr<Foo> weak(gc);
    weak = gc.createInstance<Foo>(1);

    ASSERT_FALSE(weak.empty());
    ASSERT_EQ(1, gc.get_objects_count());

    gc.collect();

    ASSERT_TRUE(weak.empty());
    ASSERT_TRUE(weak.expired());
    ASSERT_EQ(nullptr, weak.get());
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, weakPointerSurvivesWhileObjectIsRooted)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root(gc);
    GCObjectWeakPtr<Foo> weak(gc);
    root = gc.createInstance<Foo>(1);
    weak = root;

    ASSERT_EQ(root.get(), weak.get());
    gc.collect();
    ASSERT_EQ(1, gc.get_objects_count());
    ASSERT_EQ(root.get(), weak.get());

    root.reset();
    gc.collect();
    ASSERT_TRUE(weak.empty());
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, weakPointerCanBeConstructedFromRoot)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(7);

    GCObjectWeakPtr<Foo> weak(root);
    ASSERT_EQ(root.get(), weak.get());
    ASSERT_EQ(7, weak.get()->id);

    root.reset();
    gc.collect();
    ASSERT_TRUE(weak.empty());
}

TEST(GCTEST, weakPointerLockPromotesToRoot)
{
    GarbageCollector gc;
    GCObjectWeakPtr<Foo> weak(gc);
    Foo* object = gc.createInstance<Foo>(3);
    weak = object;

    {
        GCObjectRootPtr<Foo> locked = weak.lock();
        ASSERT_EQ(object, locked.get());
        ASSERT_EQ(3, locked->id);

        weak.reset();
        gc.collect();
        ASSERT_EQ(1, gc.get_objects_count());
        ASSERT_TRUE(gc.owns(object));
    }

    gc.collect();
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, weakPointerLockOfEmptyReturnsEmptyRoot)
{
    GarbageCollector gc;
    GCObjectWeakPtr<Foo> weak(gc);

    GCObjectRootPtr<Foo> locked = weak.lock();
    ASSERT_TRUE(locked.empty());
}

TEST(GCTEST, weakPointerCopyTracksIndependently)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(1);
    GCObjectWeakPtr<Foo> first(root);
    GCObjectWeakPtr<Foo> second(first);

    ASSERT_EQ(first.get(), second.get());
    first.reset();
    ASSERT_TRUE(first.empty());
    ASSERT_EQ(root.get(), second.get());

    root.reset();
    gc.collect();
    ASSERT_TRUE(second.empty());
}

TEST(GCTEST, weakAssignmentValidatesTargetOnce)
{
    Foo target(1);
    CountingWeakRegistry registry(&target);
    GCObjectWeakPtr<Foo> weak(registry);

    weak = &target;

    ASSERT_EQ(&target, weak.get());
    ASSERT_EQ(0, registry.ownsCalls);
    ASSERT_EQ(1, registry.acceptanceCalls);
}

TEST(GCTEST, rejectsWeakPointerFromAnotherCollector)
{
    GarbageCollector first;
    GarbageCollector second;
    GCObjectWeakPtr<Foo> weak(first);
    Foo* foreign = second.createInstance<Foo>(1);

    ASSERT_THROW(weak = foreign, std::invalid_argument);
    ASSERT_TRUE(weak.empty());
}

TEST(GCTEST, rejectsWeakAssignmentAcrossCollectors)
{
    GarbageCollector first;
    GarbageCollector second;
    GCObjectRootPtr<Foo> firstRoot(first);
    GCObjectRootPtr<Foo> secondRoot(second);
    firstRoot = first.createInstance<Foo>(1);
    secondRoot = second.createInstance<Foo>(2);

    GCObjectWeakPtr<Foo> weak(first);
    weak = firstRoot;
    ASSERT_THROW(weak = secondRoot, std::invalid_argument);
    ASSERT_EQ(firstRoot.get(), weak.get());
}

TEST(GCTEST, weakPointerCanOutliveCollector)
{
    auto gc = std::make_unique<GarbageCollector>();
    auto weak = std::make_unique<GCObjectWeakPtr<Foo>>(*gc);
    *weak = gc->createInstance<Foo>(1);

    gc.reset();

    ASSERT_TRUE(weak->empty());
    ASSERT_EQ(nullptr, weak->registry());
    ASSERT_THROW(weak->lock(), std::logic_error);
    ASSERT_NO_THROW(weak.reset());
}

TEST(GCTEST, weakPointerDoesNotKeepCycleAlive)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(1);
    root->pFoo = gc.createInstance<Foo>(2);
    root->pFoo->pFoo = root.get();

    GCObjectWeakPtr<Foo> weakA(gc);
    GCObjectWeakPtr<Foo> weakB(gc);
    weakA = root.get();
    weakB = root->pFoo.get();

    root.reset();
    gc.collect();

    ASSERT_EQ(0, gc.get_objects_count());
    ASSERT_TRUE(weakA.empty());
    ASSERT_TRUE(weakB.empty());
}

TEST(GCTEST, destructorAssignmentToLaterDeadObjectIsIgnored)
{
    GarbageCollector gc;
    GCObjectWeakPtr<Foo> weak(gc);
    bool assignmentSucceeded = false;
    auto* assigning = gc.createInstance<WeakAssigningDestructor>(
        weak, assignmentSucceeded);
    Foo* target = gc.createInstance<Foo>(1);
    assigning->setTarget(target);

    gc.collect();

    ASSERT_TRUE(assignmentSucceeded);
    ASSERT_TRUE(weak.empty());
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, destructorAssignmentToAlreadySweptDeadObjectIsIgnored)
{
    GarbageCollector gc;
    GCObjectWeakPtr<Foo> weak(gc);
    bool assignmentSucceeded = false;
    Foo* target = gc.createInstance<Foo>(1);
    auto* assigning = gc.createInstance<WeakAssigningDestructor>(
        weak, assignmentSucceeded);
    assigning->setTarget(target);

    gc.collect();

    ASSERT_TRUE(assignmentSucceeded);
    ASSERT_TRUE(weak.empty());
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, destructorAssignmentToLiveObjectSurvivesSweep)
{
    GarbageCollector gc;
    GCObjectWeakPtr<Foo> weak(gc);
    GCObjectRootPtr<Foo> root(gc);
    bool assignmentSucceeded = false;
    auto* assigning = gc.createInstance<WeakAssigningDestructor>(
        weak, assignmentSucceeded);
    root = gc.createInstance<Foo>(1);
    assigning->setTarget(root.get());

    gc.collect();

    ASSERT_TRUE(assignmentSucceeded);
    ASSERT_EQ(root.get(), weak.get());
    ASSERT_EQ(1, gc.get_objects_count());
}

TEST(GCTEST, selfReferentialWeakMemberSupportsLock)
{
    GarbageCollector gc;
    GCObjectRootPtr<SelfWeakNode> root(gc);
    root = gc.createInstance<SelfWeakNode>(gc);
    root->self = root.get();

    GCObjectRootPtr<SelfWeakNode> locked = root->self.lock();

    ASSERT_EQ(root.get(), locked.get());
}

TEST(GCTEST, forwardDeclaredWeakMemberCanReferenceTarget)
{
    GarbageCollector gc;
    GCObjectRootPtr<ForwardDeclaredWeakOwner> owner(gc);
    GCObjectRootPtr<ForwardDeclaredWeakTarget> target(gc);
    owner = gc.createInstance<ForwardDeclaredWeakOwner>(gc);
    target = gc.createInstance<ForwardDeclaredWeakTarget>();

    owner->target = target.get();

    ASSERT_EQ(target.get(), owner->target.get());
}

TEST(GCTEST, destructorAssignmentToDeadObjectRejectedAndRootDoesNotDangle)
{
    // Case 1: Target allocated after assigning object (swept after assigning)
    {
        GarbageCollector gc;
        GCObjectRootPtr<Foo> root(gc);
        bool assignmentSucceeded = false;
        bool exceptionCaught = false;
        auto* assigning = gc.createInstance<RootAssigningDestructor>(
            root, assignmentSucceeded, exceptionCaught);
        Foo* target = gc.createInstance<Foo>(1);
        assigning->setTarget(target);

        gc.collect();

        ASSERT_FALSE(assignmentSucceeded);
        ASSERT_TRUE(exceptionCaught);
        ASSERT_TRUE(root.empty());
        ASSERT_EQ(nullptr, root.get());
        ASSERT_EQ(0, gc.get_objects_count());
    }

    // Case 2: Target allocated before assigning object (already swept before assigning destructor runs)
    {
        GarbageCollector gc;
        GCObjectRootPtr<Foo> root(gc);
        bool assignmentSucceeded = false;
        bool exceptionCaught = false;
        Foo* target = gc.createInstance<Foo>(1);
        auto* assigning = gc.createInstance<RootAssigningDestructor>(
            root, assignmentSucceeded, exceptionCaught);
        assigning->setTarget(target);

        gc.collect();

        ASSERT_FALSE(assignmentSucceeded);
        ASSERT_TRUE(exceptionCaught);
        ASSERT_TRUE(root.empty());
        ASSERT_EQ(nullptr, root.get());
        ASSERT_EQ(0, gc.get_objects_count());
    }
}

TEST(GCTEST, destructorCreatingOrCopyingHandlesRejectedDuringSweep)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(1);
    GCObjectWeakPtr<Foo> weak(root);

    bool createRootFailed = false;
    bool createWeakFailed = false;
    bool copyRootFailed = false;
    bool copyWeakFailed = false;

    (void)gc.createInstance<HandleCreatingDestructor>(
        gc, root, weak, createRootFailed, createWeakFailed, copyRootFailed, copyWeakFailed);

    gc.collect();

    ASSERT_TRUE(createRootFailed);
    ASSERT_TRUE(createWeakFailed);
    ASSERT_TRUE(copyRootFailed);
    ASSERT_TRUE(copyWeakFailed);
    ASSERT_EQ(1, gc.get_objects_count());
    ASSERT_EQ(1, root->id);
}

TEST(GCTEST, destructorMutatingRootsAndWeakPointers)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> liveRoot(gc);
    liveRoot = gc.createInstance<Foo>(100);

    GCObjectRootPtr<Foo> targetRoot(gc);
    bool assignmentSucceeded = false;
    bool exceptionCaught = false;

    auto* assigning = gc.createInstance<RootAssigningDestructor>(
        targetRoot, assignmentSucceeded, exceptionCaught);
    assigning->setTarget(liveRoot.get());

    gc.collect();

    // Assigning a live object during sweeping should succeed
    ASSERT_TRUE(assignmentSucceeded);
    ASSERT_FALSE(exceptionCaught);
    ASSERT_EQ(liveRoot.get(), targetRoot.get());
    ASSERT_EQ(1, gc.get_objects_count());
}

TEST(GCTEST, collectorDestructionSafelyRejectsAllPublicAPIs)
{
    bool ownsRejected = false;
    bool countRejected = false;
    bool thresholdRejected = false;
    bool nextThresholdRejected = false;
    bool collectRejected = false;
    bool createRejected = false;
    bool acceptsWeakRejected = false;
    bool removeRootRejected = false;
    bool removeWeakRejected = false;

    {
        GarbageCollector gc;
        (void)gc.createInstance<DestructorCollectorInterrogator>(
            gc, ownsRejected, countRejected, thresholdRejected,
            nextThresholdRejected, collectRejected, createRejected,
            acceptsWeakRejected, removeRootRejected, removeWeakRejected);
    }

    ASSERT_TRUE(ownsRejected);
    ASSERT_TRUE(countRejected);
    ASSERT_TRUE(thresholdRejected);
    ASSERT_TRUE(nextThresholdRejected);
    ASSERT_TRUE(collectRejected);
    ASSERT_TRUE(createRejected);
    ASSERT_TRUE(acceptsWeakRejected);
    ASSERT_TRUE(removeRootRejected);
    ASSERT_TRUE(removeWeakRejected);
}

TEST(GCTEST, epochRolloverResetsObjectEpochsAndPreservesLiveGraph)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root(gc);
    root = gc.createInstance<Foo>(42);
    root->pFoo = gc.createInstance<Foo>(43);

    // Unrooted cycle
    Foo* dead1 = gc.createInstance<Foo>(1);
    Foo* dead2 = gc.createInstance<Foo>(2);
    dead1->pFoo = dead2;
    dead2->pFoo = dead1;

    ASSERT_EQ(4, gc.get_objects_count());

#ifdef CPPGC_TESTING
    gc.setEpochForTesting(std::numeric_limits<uint64_t>::max());
    ASSERT_EQ(std::numeric_limits<uint64_t>::max(), gc.getEpochForTesting());
#endif

    gc.collect();

#ifdef CPPGC_TESTING
    ASSERT_EQ(1, gc.getEpochForTesting());
#endif

    ASSERT_EQ(2, gc.get_objects_count());
    ASSERT_EQ(42, root->id);
    ASSERT_EQ(43, root->pFoo->id);
}

TEST(GCTEST, pointerReuseAfterCollectionIsSafelyTraced)
{
    GarbageCollector gc;
    GCObjectRootPtr<LegacyRawNode> root(gc);
    root = gc.createInstance<LegacyRawNode>();

    // Create an unrooted node
    LegacyRawNode* temporary = gc.createInstance<LegacyRawNode>();
    (void)temporary;
    gc.collect(); // temporary collected

    // Allocate a new node which may reuse the memory
    LegacyRawNode* fresh = gc.createInstance<LegacyRawNode>();
    root->next = fresh;

    gc.collect();

    ASSERT_EQ(2, gc.get_objects_count());
    ASSERT_EQ(fresh, root->next);
}

TEST(GCTEST, threadMisuseOfHandlesRejected)
{
    GarbageCollector gc;
    Foo* object = gc.createInstance<Foo>(10);
    GCObjectWeakPtr<Foo> weak(gc);
    GCObjectRootPtr<Foo> root(gc);

    bool constructRootRejected = false;
    bool constructWeakRejected = false;
    bool assignWeakRejected = false;
    bool assignRootRejected = false;
    bool lockWeakRejected = false;

    std::thread worker([&]
    {
        try { GCObjectRootPtr<Foo> r(gc); } catch (const std::logic_error&) { constructRootRejected = true; }
        try { GCObjectWeakPtr<Foo> w(gc); } catch (const std::logic_error&) { constructWeakRejected = true; }
        try { weak = object; } catch (const std::logic_error&) { assignWeakRejected = true; }
        try { root = object; } catch (const std::logic_error&) { assignRootRejected = true; }
        try { (void)weak.lock(); } catch (const std::logic_error&) { lockWeakRejected = true; }
    });
    worker.join();

    ASSERT_TRUE(constructRootRejected);
    ASSERT_TRUE(constructWeakRejected);
    ASSERT_TRUE(assignWeakRejected);
    ASSERT_TRUE(assignRootRejected);
    ASSERT_TRUE(lockWeakRejected);
}

TEST(GCTEST, multipleInheritancePointerAdjustment)
{
    GarbageCollector gc;
    GCObjectRootPtr<MultiInheritedObject> root(gc);
    root = gc.createInstance<MultiInheritedObject>(10);
    root->peer = gc.createInstance<MultiInheritedObject>(20);

    // Verify virtual method dispatches across non-first base interfaces
    NonGCInterface* nonGC = root.get();
    ASSERT_EQ(10 + 42, nonGC->interfaceValue());

    SecondNonGCInterface* secondNonGC = root.get();
    ASSERT_EQ(20, secondNonGC->secondValue());

    ASSERT_EQ(2, gc.get_objects_count());
    gc.collect();
    ASSERT_EQ(2, gc.get_objects_count());

    ASSERT_EQ(10 + 42, nonGC->interfaceValue());
    ASSERT_EQ(20, secondNonGC->secondValue());

    root.reset();
    gc.collect();
    ASSERT_EQ(0, gc.get_objects_count());
}

TEST(GCTEST, typedWeakAndRootPointerConversions)
{
    GarbageCollector gc;
    GCObjectRootPtr<DerivedManaged> derivedRoot(gc);
    derivedRoot = gc.createInstance<DerivedManaged>(10, 20);

    // Covariant construction: Root<Derived> -> Root<Base>
    GCObjectRootPtr<BaseManaged> baseRoot(derivedRoot);
    ASSERT_EQ(derivedRoot.get(), baseRoot.get());
    ASSERT_EQ(10, baseRoot->baseVal);

    // Covariant construction: Root<Derived> -> Weak<Base>
    GCObjectWeakPtr<BaseManaged> baseWeakFromRoot(derivedRoot);
    ASSERT_EQ(derivedRoot.get(), baseWeakFromRoot.get());
    ASSERT_EQ(10, baseWeakFromRoot.get()->baseVal);

    // Covariant copy: Weak<Derived> -> Weak<Base>
    GCObjectWeakPtr<DerivedManaged> derivedWeak(derivedRoot);
    GCObjectWeakPtr<BaseManaged> baseWeakFromWeak(derivedWeak);
    ASSERT_EQ(derivedRoot.get(), baseWeakFromWeak.get());

    // Lock promoted base weak returns Root<Base>
    GCObjectRootPtr<BaseManaged> locked = baseWeakFromWeak.lock();
    ASSERT_FALSE(locked.empty());
    ASSERT_EQ(derivedRoot.get(), locked.get());

    // Covariant assignment: Root<Derived> -> Root<Base>
    GCObjectRootPtr<BaseManaged> assignedBaseRoot(gc);
    assignedBaseRoot = derivedRoot;
    ASSERT_EQ(derivedRoot.get(), assignedBaseRoot.get());

    // Covariant assignment: Weak<Derived> -> Weak<Base>
    GCObjectWeakPtr<BaseManaged> assignedBaseWeak(gc);
    assignedBaseWeak = derivedWeak;
    ASSERT_EQ(derivedRoot.get(), assignedBaseWeak.get());
}

TEST(GCTEST, handleExplicitOperatorBool)
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root(gc);
    GCObjectWeakPtr<Foo> weak(gc);

    ASSERT_FALSE(static_cast<bool>(root));
    ASSERT_FALSE(static_cast<bool>(weak));
    if (root) { FAIL(); }
    if (weak) { FAIL(); }

    root = gc.createInstance<Foo>(1);
    weak = root;

    ASSERT_TRUE(static_cast<bool>(root));
    ASSERT_TRUE(static_cast<bool>(weak));
    if (!root) { FAIL(); }
    if (!weak) { FAIL(); }

    root.reset();
    ASSERT_FALSE(static_cast<bool>(root));

    gc.collect();
    ASSERT_FALSE(static_cast<bool>(weak));
}

int main(int argc, char** argv) 
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
