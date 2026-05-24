# Adapted from the Computer Language Benchmarks Game n-body benchmark.

from math import sqrt

PI = 3.141592653589793
SOLAR_MASS = 4.0 * PI * PI
DAYS_PER_YEAR = 365.24


def make_bodies():
    return [
        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, SOLAR_MASS],
        [
            4.841431442464721,
            -1.1603200440274284,
            -0.10362204447112311,
            0.001660076642744037 * DAYS_PER_YEAR,
            0.007699011184197404 * DAYS_PER_YEAR,
            -0.0000690460016972063 * DAYS_PER_YEAR,
            0.0009547919384243266 * SOLAR_MASS,
        ],
        [
            8.34336671824458,
            4.124798564124305,
            -0.4035234171143214,
            -0.002767425107268624 * DAYS_PER_YEAR,
            0.004998528012349172 * DAYS_PER_YEAR,
            0.000023041729757376393 * DAYS_PER_YEAR,
            0.0002858859806661308 * SOLAR_MASS,
        ],
        [
            12.894369562139131,
            -15.111151401698631,
            -0.22330757889265573,
            0.002964601375647616 * DAYS_PER_YEAR,
            0.0023784717395948095 * DAYS_PER_YEAR,
            -0.000029658956854023755 * DAYS_PER_YEAR,
            0.00004366244043351563 * SOLAR_MASS,
        ],
        [
            15.379697114850916,
            -25.919314609987964,
            0.17925877295037118,
            0.0026806777249038932 * DAYS_PER_YEAR,
            0.001628241700382423 * DAYS_PER_YEAR,
            -0.00009515922545197159 * DAYS_PER_YEAR,
            0.000051513890204661145 * SOLAR_MASS,
        ],
    ]


def offset_momentum(bodies):
    px = 0.0
    py = 0.0
    pz = 0.0

    for i in range(len(bodies)):
        body = bodies[i]
        mass = body[6]
        px = px + body[3] * mass
        py = py + body[4] * mass
        pz = pz + body[5] * mass

    sun = bodies[0]
    sun[3] = -px / SOLAR_MASS
    sun[4] = -py / SOLAR_MASS
    sun[5] = -pz / SOLAR_MASS


def advance(bodies, dt):
    count = len(bodies)

    for i in range(count):
        body = bodies[i]
        for j in range(i + 1, count):
            other = bodies[j]
            dx = body[0] - other[0]
            dy = body[1] - other[1]
            dz = body[2] - other[2]

            distance_sq = dx * dx + dy * dy + dz * dz
            distance = sqrt(distance_sq)
            mag = dt / (distance_sq * distance)

            body_mass = body[6]
            other_mass = other[6]
            body[3] = body[3] - dx * other_mass * mag
            body[4] = body[4] - dy * other_mass * mag
            body[5] = body[5] - dz * other_mass * mag
            other[3] = other[3] + dx * body_mass * mag
            other[4] = other[4] + dy * body_mass * mag
            other[5] = other[5] + dz * body_mass * mag

    for i in range(count):
        body = bodies[i]
        body[0] = body[0] + dt * body[3]
        body[1] = body[1] + dt * body[4]
        body[2] = body[2] + dt * body[5]


def energy(bodies):
    e = 0.0
    count = len(bodies)

    for i in range(count):
        body = bodies[i]
        vx = body[3]
        vy = body[4]
        vz = body[5]
        mass = body[6]
        e = e + 0.5 * mass * (vx * vx + vy * vy + vz * vz)

        for j in range(i + 1, count):
            other = bodies[j]
            dx = body[0] - other[0]
            dy = body[1] - other[1]
            dz = body[2] - other[2]
            distance = sqrt(dx * dx + dy * dy + dz * dz)
            e = e - mass * other[6] / distance

    return e


def run(n):
    bodies = make_bodies()
    offset_momentum(bodies)

    for i in range(n):
        advance(bodies, 0.01)

    final_energy = energy(bodies)
    if final_energy > -0.18:
        if final_energy < -0.16:
            return 1
    return 0
