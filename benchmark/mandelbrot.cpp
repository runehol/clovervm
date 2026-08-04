#include "cpp_benchmarks.h"

namespace benchmark_cpp
{
    namespace
    {
        int64_t mandelbrot_iterations(int64_t n)
        {
            int64_t total_iterations = 0;
            int64_t y = 0;
            double ci = -1.0;
            while(y < n)
            {
                int64_t x = 0;
                double cr = -2.0;
                while(x < n)
                {
                    double zr = 0.0;
                    double zi = 0.0;
                    double zr2 = 0.0;
                    double zi2 = 0.0;
                    int64_t iteration = 0;
                    while(iteration < 80 && zr2 + zi2 <= 4.0)
                    {
                        zi = 2.0 * zr * zi + ci;
                        zr = zr2 - zi2 + cr;
                        zr2 = zr * zr;
                        zi2 = zi * zi;
                        ++iteration;
                    }

                    total_iterations += iteration;
                    cr += 0.03;
                    ++x;
                }

                ci += 0.02;
                ++y;
            }
            return total_iterations;
        }
    }  // namespace

    int64_t mandelbrot_run(int64_t n) { return mandelbrot_iterations(n); }

    int64_t mandelbrot_items(int64_t n) { return mandelbrot_iterations(n); }
}  // namespace benchmark_cpp
