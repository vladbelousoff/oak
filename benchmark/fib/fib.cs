using System;

static class Bench
{
    static int Fib(int n)
    {
        if (n < 2)
        {
            return n;
        }
        return Fib(n - 1) + Fib(n - 2);
    }

    static void Main()
    {
        int total = 0;
        for (int rep = 0; rep < 40; rep++)
        {
            total += Fib(30);
        }
        Console.WriteLine(total);
    }
}
