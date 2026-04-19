class Incrementer:
    def bump(self, value):
        return value + 1


def run(n):
    obj = Incrementer()
    acc = 0
    for i in range(n):
        acc += obj.bump(i)
    return acc
