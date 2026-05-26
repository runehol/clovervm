class Incrementer:
    def bump(self, value, amount=1):
        return value + amount


def run(n):
    obj = Incrementer()
    acc = 0
    for i in range(n):
        acc += obj.bump(i, amount=3)
    return acc
