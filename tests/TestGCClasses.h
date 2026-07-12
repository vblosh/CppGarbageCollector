#pragma once

#include "GarbageCollector.h"

class HeaderDefinedNode : public cppgc::GCObject
{
public:
    static inline const cppgc::ClassInfo classInfo{ nullptr };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

    HeaderDefinedNode() : next(this)
    {}

    void trace(cppgc::GCPointerList& pointers) const override
    {
        pointers.push_back(cppgc::getGCObjectPointer(next));
    }

    cppgc::GCMember<HeaderDefinedNode> next;
};

const cppgc::ClassInfo* getHeaderDefinedNodeInfoFromOtherTranslationUnit();

// --- Test helper classes (moved here for better organization; all use new virtual trace) ---

class Foo : public cppgc::GCObject
{
public:
    static inline const cppgc::ClassInfo classInfo{ nullptr };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

    Foo(int id)
        : pFoo(this), id(id)
    {}

    void trace(cppgc::GCPointerList& pointers) const override
    {
        pointers.push_back(cppgc::getGCObjectPointer(pFoo));
    }

    cppgc::GCMember<Foo> pFoo;
    int id;
};

class Boo : public Foo
{
public:
    static inline const cppgc::ClassInfo classInfo{ Foo::GetClassInfo() };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

    Boo(int id, char ch)
        : Foo(id), ch(ch), pBoo(this)
    {}

    void trace(cppgc::GCPointerList& pointers) const override
    {
        pointers.push_back(cppgc::getGCObjectPointer(pBoo));
        Foo::trace(pointers);
    }

    char ch;
    cppgc::GCMember<Boo> pBoo;
};

class NoAncestor : public cppgc::GCObject
{
public:
    static inline const cppgc::ClassInfo classInfo{ nullptr };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }
};

class ChildOfNoAncestor : public NoAncestor
{
public:
    static inline const cppgc::ClassInfo classInfo{ NoAncestor::GetClassInfo() };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

    ChildOfNoAncestor() : child(this)
    {}

    void setChild(Foo* value)
    {
        child = value;
    }

    void trace(cppgc::GCPointerList& pointers) const override
    {
        pointers.push_back(cppgc::getGCObjectPointer(child));
    }

private:
    cppgc::GCMember<Foo> child;
};

class ThrowingObject : public cppgc::GCObject
{
public:
    static inline const cppgc::ClassInfo classInfo{ nullptr };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

    ThrowingObject()
    {
        throw std::runtime_error("constructor failure");
    }
};

class LegacyRawNode : public cppgc::GCObject
{
public:
    static inline const cppgc::ClassInfo classInfo{ nullptr };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

    LegacyRawNode() : next(nullptr)
    {}

    void trace(cppgc::GCPointerList& pointers) const override
    {
        pointers.push_back(cppgc::getGCObjectPointer(next));
    }

    LegacyRawNode* next;
};

class ConstructorEdgeObject : public cppgc::GCObject
{
public:
    static inline const cppgc::ClassInfo classInfo{ nullptr };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

    explicit ConstructorEdgeObject(Foo* child) : child(this, child)
    {}

    void trace(cppgc::GCPointerList& pointers) const override
    {
        pointers.push_back(cppgc::getGCObjectPointer(child));
    }

private:
    cppgc::GCMember<Foo> child;
};

class ReentrantObject : public cppgc::GCObject
{
public:
    static inline const cppgc::ClassInfo classInfo{ nullptr };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

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
    static inline const cppgc::ClassInfo classInfo{ nullptr };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

    GraphNode() : first(this), second(this)
    {}

    void trace(cppgc::GCPointerList& pointers) const override
    {
        pointers.push_back(cppgc::getGCObjectPointer(first));
        pointers.push_back(cppgc::getGCObjectPointer(second));
    }

    cppgc::GCMember<GraphNode> first;
    cppgc::GCMember<GraphNode> second;
};

class ThrowingTraceObject : public cppgc::GCObject
{
public:
    static inline const cppgc::ClassInfo classInfo{ nullptr };
    static const cppgc::ClassInfo* GetClassInfo() { return &classInfo; }
    virtual const cppgc::ClassInfo* getClassInfo() const override { return &classInfo; }

    void trace(cppgc::GCPointerList& pointers) const override
    {
        (void)pointers;  // unused (throws before using)
        if (shouldThrow)
        {
            shouldThrow = false;
            throw std::runtime_error("trace failure");
        }
    }

    static bool shouldThrow;
};

inline bool ThrowingTraceObject::shouldThrow = false;
