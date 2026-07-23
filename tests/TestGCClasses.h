#pragma once

#include <memory>

#include "GarbageCollector.h"

// Test helper classes for ownership and tracing behavior.

class Foo : public cppgc::GCObject
{
public:
    Foo(int id)
        : pFoo(*this), id(id)
    {}

    cppgc::GCMember<Foo> pFoo;
    int id;
};

class Boo : public Foo
{
public:
    Boo(int id, char ch)
        : Foo(id), ch(ch), pBoo(*this)
    {}

    char ch;
    cppgc::GCMember<Boo> pBoo;
};

class PointerFreeBase : public cppgc::GCObject
{};

class ChildWithMember : public PointerFreeBase
{
public:
    ChildWithMember() : child(*this)
    {}

    void setChild(Foo* value)
    {
        child = value;
    }

private:
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
    LegacyRawNode() : next(nullptr)
    {}

    void traceAdditional(cppgc::GCPointerList& pointers) const override
    {
        pointers.push_back(cppgc::getGCObjectPointer(next));
    }

    LegacyRawNode* next;
};

class ConstructorEdgeObject : public cppgc::GCObject
{
public:
    explicit ConstructorEdgeObject(Foo* child) : child(*this, child)
    {}

private:
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
    GraphNode() : first(*this), second(*this)
    {}

    cppgc::GCMember<GraphNode> first;
    cppgc::GCMember<GraphNode> second;
};

class DynamicMemberOwner : public cppgc::GCObject
{
public:
    void addEdges(Foo* firstTarget, Foo* secondTarget)
    {
        first = std::make_unique<cppgc::GCMember<Foo>>(*this, firstTarget);
        second = std::make_unique<cppgc::GCMember<Foo>>(*this, secondTarget);
    }

    void removeFirstEdge()
    {
        first.reset();
    }

private:
    std::unique_ptr<cppgc::GCMember<Foo>> first;
    std::unique_ptr<cppgc::GCMember<Foo>> second;
};

class ThrowingTraceObject : public cppgc::GCObject
{
public:
    void traceAdditional(cppgc::GCPointerList&) const override
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
