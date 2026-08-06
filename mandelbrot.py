def run(n):
    total_iterations = 0
    y = 0
    ci = -1.0
    while y < n:
        x = 0
        cr = -2.0
        while x < n:
            zr = 0.0
            zi = 0.0
            zr2 = 0.0
            zi2 = 0.0
            iteration = 0
            while iteration < 80 and zr2 + zi2 <= 4.0:
                zi = 2.0 * zr * zi + ci
                zr = zr2 - zi2 + cr
                zr2 = zr * zr
                zi2 = zi * zi
                iteration += 1

            total_iterations += iteration
            cr += 0.03
            x += 1

        ci += 0.02
        y += 1

    return total_iterations


run(2)
