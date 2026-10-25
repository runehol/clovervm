# Fixed input isolates formatting from integer arithmetic.
def run(n):
    value = (1 << 1024) + 7
    acc = 0
    for i in range(n):
        text = oct(value)
        acc += len(text)
    return acc
