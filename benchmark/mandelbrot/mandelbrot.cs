using System;

static class Bench
{
    static void Main()
    {
        const int width = 256;
        const int maxIter = 64;
        const double step = 2.0 / 256.0;

        int inside = 0;
        for (int py = 0; py < width; py++)
        {
            double cy = -1.0 + py * step;
            for (int px = 0; px < width; px++)
            {
                double cx = -1.5 + px * step;
                double zr = 0.0;
                double zi = 0.0;
                int iter = 0;
                while (iter < maxIter && zr * zr + zi * zi <= 4.0)
                {
                    double t = zr * zr - zi * zi + cx;
                    zi = 2.0 * zr * zi + cy;
                    zr = t;
                    iter += 1;
                }
                if (iter == maxIter)
                {
                    inside += 1;
                }
            }
        }
        Console.WriteLine(inside);
    }
}
