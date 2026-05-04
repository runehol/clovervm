def run(n):
    acc = 0
    counter = 0
    while counter < n:
        try:
            acc += counter
        except:
            acc -= 1
        counter += 1
    return acc
