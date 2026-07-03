using System;

static class Bench
{
    static int Nsieve(int limit)
    {
        int[] flags = new int[limit + 1];
        for (int i = 0; i <= limit; i++)
        {
            flags[i] = 1;
        }

        int p = 2;
        while (p * p <= limit)
        {
            if (flags[p] == 1)
            {
                int m = p * p;
                while (m <= limit)
                {
                    flags[m] = 0;
                    m += p;
                }
            }
            p += 1;
        }

        int count = 0;
        for (int n = 2; n <= limit; n++)
        {
            if (flags[n] == 1)
            {
                count += 1;
            }
        }
        return count;
    }

    static void Main()
    {
        int total = 0;
        for (int rep = 0; rep < 1; rep++)
        {
            total += Nsieve(500000);
        }
        Console.WriteLine(total);
    }
}
