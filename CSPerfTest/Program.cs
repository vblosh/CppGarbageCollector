using System;
using System.Diagnostics;
using Typ = System.Double;

namespace GCPerfTest
{
    class Node
    {
        public Node(Typ val)
        {
            value = val;
        }

        public Node left;
        public Node right;
        public Typ value;
    };

    class Program
    {
        const int NUM_INS = 1000000;

        static void insertInBinaryTree(Node node, Typ val)
        {
            if (val < node.value)
            {
                if (node.left == null)
                    node.left = new Node(val);
                else
                    insertInBinaryTree(node.left, val);
            }
            else
            {
                if (node.right == null)
                    node.right = new Node(val);
                else
                    insertInBinaryTree(node.right, val);
            }
        }

        static Typ g_Sum;

        static Typ nextDataValue(ref ulong state)
        {
            unchecked
            {
                state += 0x9e3779b97f4a7c15UL;
                ulong value = state;
                value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9UL;
                value = (value ^ (value >> 27)) * 0x94d049bb133111ebUL;
                value ^= value >> 31;
                return (value >> 11) * (1.0 / 9007199254740992.0);
            }
        }

        static void DFS(Node node)
        {
            if (node == null) return;

            g_Sum += node.value;
            DFS(node.left);
            DFS(node.right);
        }
        static void performanceTest()
        {
            var data = new Typ[NUM_INS];
            ulong dataState = 12345;
            for (int i = 0; i < data.Length; ++i)
            {
                data[i] = nextDataValue(ref dataState);
            }

            Node root1 = new Node(0.5);
            Node root2 = new Node(0.5);

            Stopwatch stopwatch;
            stopwatch = Stopwatch.StartNew();
            for (int i = 0; i < NUM_INS; ++i)
            {
                insertInBinaryTree(root1, data[i]);
                insertInBinaryTree(root2, data[i]);
            }
            stopwatch.Stop();
            Console.WriteLine("2 x insertInBinaryTree {0} s elapsed", (double)stopwatch.ElapsedMilliseconds / 1000);

            stopwatch = Stopwatch.StartNew();
            g_Sum = 0;
            DFS(root1);
            stopwatch.Stop();
            Console.WriteLine("depth first search {0} s elapsed", (double)stopwatch.ElapsedMilliseconds / 1000);
            Console.WriteLine("Sum of binary tree's values={0}", g_Sum);

            Console.WriteLine("{0} bytes memory used before Collect()", GC.GetTotalMemory(false));
            stopwatch = Stopwatch.StartNew();
            root2 = null;
            GC.Collect();
            GC.WaitForPendingFinalizers();
            stopwatch.Stop();
            Console.WriteLine("garbage collector collect {0} s elapsed", (double)stopwatch.ElapsedMilliseconds / 1000);
            Console.WriteLine("{0} bytes memory used before Collect()", GC.GetTotalMemory(false));

            stopwatch = Stopwatch.StartNew();
            g_Sum = 0;
            DFS(root1);
            stopwatch.Stop();
            Console.WriteLine("depth first search {0} s elapsed", (double)stopwatch.ElapsedMilliseconds / 1000);
            Console.WriteLine("Sum of binary tree's values={0}", g_Sum);
        }

        static void Main(string[] args)
        {
            performanceTest();
        }
    }
}
