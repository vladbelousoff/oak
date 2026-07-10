using System;
using System.Collections.Generic;

static class Bench
{
    static void Main()
    {
        const int n = 300000;
        const int keys = 20011;
        const int reps = 25;

        int sizeSum = 0;
        int total = 0;
        for (int rep = 0; rep < reps; rep++)
        {
            var counts = new Dictionary<string, int>();
            for (int i = 0; i < n; i++)
            {
                string key = "k" + ((i % keys) * 7919 % keys);
                int current;
                if (counts.TryGetValue(key, out current))
                {
                    counts[key] = current + 1;
                }
                else
                {
                    counts[key] = 1;
                }
            }

            for (int i = 0; i < n; i++)
            {
                string key = "k" + ((i % keys) * 7919 % keys);
                total += counts[key];
            }

            sizeSum += counts.Count;
        }

        Console.WriteLine(sizeSum);
        Console.WriteLine(total);
    }
}
