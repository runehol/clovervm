#include "cpp_benchmarks.h"

#include <array>
#include <cmath>

namespace benchmark_cpp
{
    namespace
    {
        constexpr double pi = 3.141592653589793;
        constexpr double solar_mass = 4.0 * pi * pi;
        constexpr double days_per_year = 365.24;

        using Body = std::array<double, 7>;
        using Bodies = std::array<Body, 5>;

        Bodies make_bodies()
        {
            return Bodies{{
                {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, solar_mass},
                {4.841431442464721, -1.1603200440274284, -0.10362204447112311,
                 0.001660076642744037 * days_per_year,
                 0.007699011184197404 * days_per_year,
                 -0.0000690460016972063 * days_per_year,
                 0.0009547919384243266 * solar_mass},
                {8.34336671824458, 4.124798564124305, -0.4035234171143214,
                 -0.002767425107268624 * days_per_year,
                 0.004998528012349172 * days_per_year,
                 0.000023041729757376393 * days_per_year,
                 0.0002858859806661308 * solar_mass},
                {12.894369562139131, -15.111151401698631, -0.22330757889265573,
                 0.002964601375647616 * days_per_year,
                 0.0023784717395948095 * days_per_year,
                 -0.000029658956854023755 * days_per_year,
                 0.00004366244043351563 * solar_mass},
                {15.379697114850916, -25.919314609987964, 0.17925877295037118,
                 0.0026806777249038932 * days_per_year,
                 0.001628241700382423 * days_per_year,
                 -0.00009515922545197159 * days_per_year,
                 0.000051513890204661145 * solar_mass},
            }};
        }

        void offset_momentum(Bodies &bodies)
        {
            double px = 0.0;
            double py = 0.0;
            double pz = 0.0;
            for(Body &body: bodies)
            {
                px += body[3] * body[6];
                py += body[4] * body[6];
                pz += body[5] * body[6];
            }

            Body &sun = bodies[0];
            sun[3] = -px / solar_mass;
            sun[4] = -py / solar_mass;
            sun[5] = -pz / solar_mass;
        }

        void advance(Bodies &bodies, double dt)
        {
            for(size_t i = 0; i < bodies.size(); ++i)
            {
                Body &body = bodies[i];
                for(size_t j = i + 1; j < bodies.size(); ++j)
                {
                    Body &other = bodies[j];
                    double dx = body[0] - other[0];
                    double dy = body[1] - other[1];
                    double dz = body[2] - other[2];

                    double distance_sq = dx * dx + dy * dy + dz * dz;
                    double distance = std::sqrt(distance_sq);
                    double mag = dt / (distance_sq * distance);

                    double body_mass = body[6];
                    double other_mass = other[6];
                    body[3] -= dx * other_mass * mag;
                    body[4] -= dy * other_mass * mag;
                    body[5] -= dz * other_mass * mag;
                    other[3] += dx * body_mass * mag;
                    other[4] += dy * body_mass * mag;
                    other[5] += dz * body_mass * mag;
                }
            }

            for(Body &body: bodies)
            {
                body[0] += dt * body[3];
                body[1] += dt * body[4];
                body[2] += dt * body[5];
            }
        }

        double energy(const Bodies &bodies)
        {
            double e = 0.0;
            for(size_t i = 0; i < bodies.size(); ++i)
            {
                const Body &body = bodies[i];
                double vx = body[3];
                double vy = body[4];
                double vz = body[5];
                double mass = body[6];
                e += 0.5 * mass * (vx * vx + vy * vy + vz * vz);

                for(size_t j = i + 1; j < bodies.size(); ++j)
                {
                    const Body &other = bodies[j];
                    double dx = body[0] - other[0];
                    double dy = body[1] - other[1];
                    double dz = body[2] - other[2];
                    double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                    e -= mass * other[6] / distance;
                }
            }
            return e;
        }
    }  // namespace

    int64_t nbody_run(int64_t n)
    {
        Bodies bodies = make_bodies();
        offset_momentum(bodies);
        for(int64_t i = 0; i < n; ++i)
        {
            advance(bodies, 0.01);
        }

        double final_energy = energy(bodies);
        return final_energy > -0.18 && final_energy < -0.16 ? 1 : 0;
    }

    int64_t nbody_items(int64_t n) { return n * 10; }
}  // namespace benchmark_cpp
