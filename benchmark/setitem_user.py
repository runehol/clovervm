class Bag:
    def __init__(self):
        self.a = 0
        self.b = 0
        self.c = 0
        self.d = 0

    def __setitem__(self, key, value):
        if key == 0:
            self.a = value
            return
        if key == 1:
            self.b = value
            return
        if key == 2:
            self.c = value
            return
        self.d = value


def run(n):
    values = Bag()
    i = 0
    idx = 0
    while i < n:
        values[idx] = i
        idx += 1
        if idx == 4:
            idx = 0
        i += 1
    return values.a + values.b + values.c + values.d
