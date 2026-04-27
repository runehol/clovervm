class Counter:
    value = 0

    def bump(self, value):
        Counter.value = value
        return Counter.value


def run(n):
    obj = Counter()
    acc = 0
    for i in range(n):
        acc += obj.bump(i)
    return acc
