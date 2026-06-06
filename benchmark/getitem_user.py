class Bag:
    def __init__(self):
        self.a = 61
        self.b = 67
        self.c = 71
        self.d = 73

    def __getitem__(self, key):
        if key == 0:
            return self.a
        if key == 1:
            return self.b
        if key == 2:
            return self.c
        return self.d


def run(n):
    values = Bag()
    acc = 0
    i = 0
    idx = 0
    while i < n:
        acc += values[idx]
        idx += 1
        if idx == 4:
            idx = 0
        i += 1
    return acc
