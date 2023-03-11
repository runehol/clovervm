

limit = 100000001
counter = 0
acc = 1245
while counter < limit:
    acc = (-acc*64 + 64)>>6

    counter += 1

acc
