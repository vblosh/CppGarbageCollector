#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <iomanip>

#include "GarbageCollector.h"

using Typ = double;

using namespace cppgc;

class Node : public GCObject
{
public:
    Node(Typ val)
        : value(val)
    {
    }

    void trace(TraceVisitor& visitor) const override
    {
        visitor.visit(left);
        visitor.visit(right);
    }

    GCMember<Node> left;
    GCMember<Node> right;
    Typ value;
};

const int NUM_INS = 1000000;

Node* insertInBinaryTree(GarbageCollector& gc, Node* node, Typ val)
{
    if (val < node->value)
    {
        if (node->left == nullptr)
            return node->left = gc.createInstance<Node>(val);
        else
            return insertInBinaryTree(gc, node->left, val);
    }
    else
    {
        if (node->right == nullptr)
            return node->right = gc.createInstance<Node>(val);
        else
            return insertInBinaryTree(gc, node->right, val);
    }
}

void DFS(Typ& sum, Node* node)
{
    if (node == nullptr) return;

    sum += node->value;
    DFS(sum, node->left);
    DFS(sum, node->right);
}

int performanceTest()
{
    GarbageCollector gc;
    Typ sum = 0;

    std::cout << "Performance test started\n";
    std::cout << "sizeof(Node)=" << sizeof(Node)
              << " sizeof(GCObject)=" << sizeof(GCObject)
              << " sizeof(GCMember<Node>)=" << sizeof(GCMember<Node>) << '\n';
    std::chrono::duration<double> elapsed;

    std::vector<double> data(NUM_INS);
    std::ifstream ifp("data.bin", std::ios::in | std::ios::binary);
    if (!ifp)
    {
        std::cerr << "Failed to open data.bin\n";
        return -1;
    }
    ifp.read(reinterpret_cast<char*>(data.data()), data.size() * sizeof(data[0]));
    if (ifp.gcount() != static_cast<std::streamsize>(data.size() * sizeof(data[0])))
    {
        std::cerr << "Incomplete data read from data.bin\n";
        return -1;
    }
    ifp.close();

    GCObjectRootPtr<Node> root1(gc);
    root1 = gc.createInstance<Node>(0.5);

    GCObjectRootPtr<Node> root2(gc);
    root2 = gc.createInstance<Node>(0.5);

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_INS; i++)
    {
        insertInBinaryTree(gc, root1.get(), data[i]);
        insertInBinaryTree(gc, root2.get(), data[i]);
    }
    elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "2 x insertInBinaryTree " << elapsed.count() << "s elapsed\n";

    start = std::chrono::high_resolution_clock::now();
    sum = 0;
    DFS(sum, root1.get());
    elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "depth first search " << elapsed.count() << "s elapsed\n";
    std::cout << "Sum of binary tree's values=" << std::setprecision(16) << sum << '\n';

    root2.reset();

    std::cout << gc.get_objects_count() << " total objects before Collect()\n";
    start = std::chrono::high_resolution_clock::now();
    gc.collect();
    elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "garbage collector collect " << elapsed.count() << "s elapsed\n";
    std::cout << gc.get_objects_count() << " total objects after Collect()\n";

    start = std::chrono::high_resolution_clock::now();
    sum = 0;
    DFS(sum, root1.get());
    elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "depth first search " << elapsed.count() << "s elapsed\n";
    std::cout << "Sum of binary tree's values=" << std::setprecision(16) << sum << '\n';
    std::cout << "Performance test ended\n";

    return 0;
}

int main(void)
{
    return performanceTest();
}
