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
        Console.WriteLine(Fib(30));
    }
}
