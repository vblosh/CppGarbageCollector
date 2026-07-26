#pragma once

#include <memory>

#include "GarbageCollector.h"

// Test helper classes for ownership and tracing behavior.

class Foo : public cppgc::GCObject
{
public:
    Foo(int id)
        : id(id)
    {}

    void trace(cppgc::TraceVisitor& visitor) const override
    {
        visitor.visit(pFoo);
    }

    cppgc::GCMember<Foo> pFoo;
    int id;
};

class Boo : public Foo
{
public:
    Boo(int id, char ch)
        : Foo(id), ch(ch)
    {}

    void trace(cppgc::TraceVisitor& visitor) const override
    {
        Foo::trace(visitor);
        visitor.visit(pBoo);
    }

    char ch;
    cppgc::GCMember<Boo> pBoo;
};

class PointerFreeBase : public cppgc::GCObject
{};

class ChildWithMember : public PointerFreeBase
{
public:
    void setChild(Foo* value)
    {
        child = value;
    }

private:
    void trace(cppgc::TraceVisitor& visitor) const override
    {
        visitor.visit(child);
    }

    cppgc::GCMember<Foo> child;
};

class ThrowingObject : public cppgc::GCObject
{
public:
    ThrowingObject()
    {
        throw std::runtime_error("constructor failure");
    }
};

class LegacyRawNode : public cppgc::GCObject
{
public:
    explicit LegacyRawNode(LegacyRawNode* next = nullptr)
        : next(next)
    {}

    void trace(cppgc::TraceVisitor& visitor) const override
    {
        visitor.visitRaw(next);
    }

    LegacyRawNode* next;
};

class ConstructorEdgeObject : public cppgc::GCObject
{
public:
    explicit ConstructorEdgeObject(Foo* child) : child(child)
    {}

private:
    void trace(cppgc::TraceVisitor& visitor) const override
    {
        visitor.visit(child);
    }

    cppgc::GCMember<Foo> child;
};

class ReentrantObject : public cppgc::GCObject
{
public:
    ReentrantObject(cppgc::GarbageCollector& collector, bool& rejected)
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
    cppgc::GarbageCollector& collector;
    bool& rejected;
};

class GraphNode : public cppgc::GCObject
{
public:
    void trace(cppgc::TraceVisitor& visitor) const override
    {
        visitor.visit(first);
        visitor.visit(second);
    }

    cppgc::GCMember<GraphNode> first;
    cppgc::GCMember<GraphNode> second;
};

class CountingTraceObject : public cppgc::GCObject
{
public:
    explicit CountingTraceObject(int& traceCount)
        : traceCount(traceCount)
    {}

    void trace(cppgc::TraceVisitor&) const override
    {
        ++traceCount;
    }

private:
    int& traceCount;
};

class DynamicMemberOwner : public cppgc::GCObject
{
public:
    void addEdges(Foo* firstTarget, Foo* secondTarget)
    {
        first = std::make_unique<cppgc::GCMember<Foo>>(firstTarget);
        second = std::make_unique<cppgc::GCMember<Foo>>(secondTarget);
    }

    void removeFirstEdge()
    {
        first.reset();
    }

private:
    void trace(cppgc::TraceVisitor& visitor) const override
    {
        if (first)
            visitor.visit(*first);
        if (second)
            visitor.visit(*second);
    }

    std::unique_ptr<cppgc::GCMember<Foo>> first;
    std::unique_ptr<cppgc::GCMember<Foo>> second;
};

class ThrowingTraceObject : public cppgc::GCObject
{
public:
    void trace(cppgc::TraceVisitor&) const override
    {
        if (shouldThrow)
        {
            shouldThrow = false;
            throw std::runtime_error("trace failure");
        }
    }

    static bool shouldThrow;
};

inline bool ThrowingTraceObject::shouldThrow = false;

class WeakAssigningDestructor : public cppgc::GCObject
{
public:
    WeakAssigningDestructor(
        cppgc::GCObjectWeakPtr<Foo>& weak,
        bool& assignmentSucceeded)
        : weak(weak), assignmentSucceeded(assignmentSucceeded)
    {}

    ~WeakAssigningDestructor() override
    {
        try
        {
            weak = target;
            assignmentSucceeded = true;
        }
        catch (...)
        {
            assignmentSucceeded = false;
        }
    }

    void setTarget(Foo* value)
    {
        target = value;
    }

private:
    cppgc::GCObjectWeakPtr<Foo>& weak;
    bool& assignmentSucceeded;
    Foo* target = nullptr;
};

class CountingWeakRegistry : public cppgc::IRootsRegistry
{
public:
    explicit CountingWeakRegistry(const cppgc::GCObject* liveTarget)
        : liveTarget(liveTarget)
    {}

    void addRoot(cppgc::GCObjectRootPtrBase*) override {}
    void removeRoot(cppgc::GCObjectRootPtrBase*) override {}
    void addWeak(cppgc::GCObjectWeakPtrBase*) override {}
    void removeWeak(cppgc::GCObjectWeakPtrBase*) override {}

    bool owns(const cppgc::GCObject* object) const override
    {
        ++ownsCalls;
        return object == liveTarget;
    }

    bool acceptsWeakTarget(const cppgc::GCObject* object) const override
    {
        ++acceptanceCalls;
        return object == liveTarget;
    }

    mutable int ownsCalls = 0;
    mutable int acceptanceCalls = 0;

private:
    const cppgc::GCObject* liveTarget;
};

class SelfWeakNode : public cppgc::GCObject
{
public:
    explicit SelfWeakNode(cppgc::IRootsRegistry& registry)
        : self(registry)
    {}

    cppgc::GCObjectWeakPtr<SelfWeakNode> self;
};

class ForwardDeclaredWeakTarget;

class ForwardDeclaredWeakOwner : public cppgc::GCObject
{
public:
    explicit ForwardDeclaredWeakOwner(cppgc::IRootsRegistry& registry)
        : target(registry)
    {}

    cppgc::GCObjectWeakPtr<ForwardDeclaredWeakTarget> target;
};

class ForwardDeclaredWeakTarget : public cppgc::GCObject
{};
