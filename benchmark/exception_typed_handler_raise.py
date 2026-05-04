def run(n):
    acc = 0
    counter = 0
    while counter < n:
        try:
            raise ValueError
        except ValueError:
            acc += counter
        counter += 1
    return acc
