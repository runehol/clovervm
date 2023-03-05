import time


def ubench(iter_count):
    counter = iter_count
    acc = 1245
    while counter:
        acc = (-acc*64 + 64)>>6

        counter -= 1

    return acc


iterations = 100000001
start = time.time()
print(ubench(iterations))
elapsed = time.time() - start
print("Took", elapsed, "seconds")
