def run():
    acc = 0
    for x in range(__N__):
        for y in range(10):
            acc += x * y
    return acc

run()
