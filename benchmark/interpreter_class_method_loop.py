class Accumulator:
    scale = 3
    bias = 5

    def bump(self, value):
        local = value + self.seed
        self.total += local * Accumulator.scale + Accumulator.bias
        return self.total


def run(n):
    grand_total = 0
    for i in range(n):
        obj = Accumulator()
        obj.seed = i
        obj.total = i
        grand_total += obj.bump(i)
        grand_total += obj.bump(i + 1)
        grand_total += Accumulator.bias
    return grand_total
