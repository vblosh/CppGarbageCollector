using System;
using System.Diagnostics;
using System.IO;
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
        static readonly string FileName = @"data.bin";

        static void DFS(Node node)
        {
            if (node == null) return;

            g_Sum += node.value;
            DFS(node.left);
            DFS(node.right);
        }
        static void perfomanceTest()
        {
            var data = new Typ[NUM_INS];
            bool load = true;
            if (load)
            {
                var bytes = File.ReadAllBytes(FileName);
                for (int i = 0; i < data.Length; ++i)
                {
                    data[i] = BitConverter.ToDouble(bytes, sizeof(Typ) * i);
                }
            }
            else
            {
                // prepare array
                Random rnd = new Random(12345);
                for (int i = 0; i < NUM_INS; ++i)
                {
                    data[i] = rnd.NextDouble();
                }

                // save it
                Byte[] bytes = new byte[data.Length * sizeof(Typ)];
                for (int i = 0; i < data.Length; ++i)
                {
                    var dblbytes = BitConverter.GetBytes(data[i]);
                    Array.Copy(dblbytes, 0, bytes, sizeof(Typ) * i, sizeof(Typ));
                }
                File.WriteAllBytes(FileName, bytes);
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
            perfomanceTest();
        }
    }
}
