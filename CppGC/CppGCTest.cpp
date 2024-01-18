#include <iostream>

#include "GarbageCollector.h"

using namespace cppgc;

class Tree : public GCObject
{
public:
    char sym;
    Tree *left, *right;
    Tree(char asym) : sym(asym), left(nullptr), right(nullptr)
    {
        //GCObject* bleft = left;
        //GCObject* bright = right;
        children.push_back((GCObject**)&left);
        children.push_back((GCObject**)&right);
        std::cout << "Tree(" << sym << ") at 0x" << this << " created\n";
    }

    ~Tree()
    {
        std::cout << "~Tree(" << sym << ") at 0x" << this << " destroyed\n";
    }
};

int main()
{
    GarbageCollector gc;

    GCObjectRootPtr<Tree> root(gc);

    std::cout << "Test Started\n";

    Tree* pA = gc.createInstance<Tree>('A');
    Tree* pB = gc.createInstance<Tree>('B');
    Tree* pC = gc.createInstance<Tree>('C');
    Tree* pD = gc.createInstance<Tree>('D');
    Tree* pE = gc.createInstance<Tree>('E');
    Tree* pF = gc.createInstance<Tree>('F');
    pC->right = pD;
    pC->left = pE;
    pD->left = pC;
    pD->right = pE;
    pB->left = pC;
    pB->right = pD;
    pA->left = pB;
    root = pA;
    // should collect only pF
    gc.collect();

    pA->left = nullptr;
    // should collect only pB, pC, pD, pE
    gc.collect();

    root = nullptr;
    // should collect pA
    gc.collect();

    std::cout << "Test Ended\n";
}
