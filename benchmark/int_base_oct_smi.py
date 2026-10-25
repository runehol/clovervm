# Fixed input isolates formatting from integer arithmetic.
def run(n):
    value = -288230376151711744
    acc = 0
    for i in range(n):
        text = oct(value)
        acc += len(text)
    return acc
