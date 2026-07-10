using System;

static class Bench
{
    static void Main()
    {
        const int n = 20000000;

        int total = 0;
        for (int i = 0; i < n; i++)
        {
            string s = "id-" + (i % 1000) + "-x";
            total += s.Length;
        }
        Console.WriteLine(total);
    }
}
