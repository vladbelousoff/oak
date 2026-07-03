using System;
using System.Collections.Generic;

static class Bench
{
    static void Main()
    {
        const int n = 300000;
        const int keys = 20011;

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

        int total = 0;
        for (int i = 0; i < n; i++)
        {
            string key = "k" + ((i % keys) * 7919 % keys);
            total += counts[key];
        }

        Console.WriteLine(counts.Count);
        Console.WriteLine(total);
    }
}
