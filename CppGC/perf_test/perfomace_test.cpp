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
    DECLARE_GCOBJECT_CLASS(Node)
public:
    Node(Typ val)
        : left(nullptr), right(nullptr), value(val)
    {
    }

    Node* left;
    Node* right;
    Typ value;
};

GCOBJECT_POINTER_MAP_BEGIN(Node)
GCPOINTER(Node, left)
GCPOINTER(Node, right)
GCOBJECT_POINTER_MAP_END(Node)

const int NUM_INS = 1000000;

Typ g_Sum;
GarbageCollector gc;

Node* insertInBinaryTree(Node* node, Typ val)
{
    if (val < node->value)
    {
        if (node->left == nullptr)
            return node->left = gc.createInstance<Node>(val);
        else
            return insertInBinaryTree(node->left, val);
    }
    else
    {
        if (node->right == nullptr)
            return node->right = gc.createInstance<Node>(val);
        else
            return insertInBinaryTree(node->right, val);
    }
}

void DFS(Node* node)
{
    if (node == nullptr) return;

    g_Sum += node->value;
    DFS(node->left);
    DFS(node->right);
}

int performanceTest()
{
    std::cout << "Performance test started\n";
    std::chrono::duration<double> elapsed;

    std::vector<double> data(NUM_INS);
    std::ifstream ifp("data.bin", std::ios::in | std::ios::binary);
    if (!ifp)
        return -1;
    ifp.read(reinterpret_cast<char*>(data.data()), data.size() * sizeof(data[0]));
    ifp.close();

    GCObjectRootPtr<Node> root1(gc);
    root1 = gc.createInstance<Node>(0.5);

    GCObjectRootPtr<Node> root2(gc);
    root2 = gc.createInstance<Node>(0.5);

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_INS; i++)
    {
        insertInBinaryTree(root1.get(), data[i]);
        insertInBinaryTree(root2.get(), data[i]);
    }
    elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "2 x insertInBinaryTree " << elapsed.count() << "s elapsed\n";

    start = std::chrono::high_resolution_clock::now();
    g_Sum = 0;
    DFS(root1.get());
    elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "depth first search " << elapsed.count() << "s elapsed\n";
    std::cout << "Sum of binary tree's values=" << std::setprecision(16) << g_Sum << '\n';

    root2.reset();

    std::cout << gc.get_objects_count() << " total objects before Collect()\n";
    start = std::chrono::high_resolution_clock::now();
    gc.collect();
    elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "garbage collector collect " << elapsed.count() << "s elapsed\n";
    std::cout << gc.get_objects_count() << " total objects after Collect()\n";

    start = std::chrono::high_resolution_clock::now();
    g_Sum = 0;
    DFS(root1.get());
    elapsed = std::chrono::high_resolution_clock::now() - start;
    std::cout << "depth first search " << elapsed.count() << "s elapsed\n";
    std::cout << "Sum of binary tree's values=" << std::setprecision(16) << g_Sum << '\n';
    std::cout << "Performance test ended\n";

    return 0;
}

int main(void)
{
    return performanceTest();
}