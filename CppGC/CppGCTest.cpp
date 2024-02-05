#include <iostream>

#include "GarbageCollector.h"

using namespace std;
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

void simpleTest()
{
    GarbageCollector gc;
    GCObjectRootPtr<Foo> root1(gc);
    GCObjectRootPtr<Foo> root2(gc);
    root1 = gc.createInstance<Foo>(1);
    root2 = gc.createInstance<Foo>(2);
    root1->pFoo = root2.get();
    root2->pFoo = root1.get();
    root1 = nullptr;
    cout << gc.get_objects_count() << " objects created" << endl;
    gc.collect();
    cout << gc.get_objects_count() << " objects created" << endl;
    root2 = nullptr;
    gc.collect();
    cout << gc.get_objects_count() << " objects created" << endl;

    GCObjectRootPtr<Boo> root11(gc);
    root11 = gc.createInstance<Boo>(11, 'a');
    root11->pFoo = gc.createInstance<Boo>(12, 'b');
    root11->pBoo = gc.createInstance<Boo>(13, 'c');
    cout << gc.get_objects_count() << " objects created" << endl;
    gc.collect();
    cout << gc.get_objects_count() << " objects created" << endl;
    root11.reset();
    gc.collect();
    cout << gc.get_objects_count() << " objects created" << endl;
}

int main()
{
    std::cout << "Test Started\n";

    simpleTest();

    std::cout << "Test Ended\n";
}
