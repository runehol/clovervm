

counter = 100000001
acc = 1245
while counter:
    acc = (-acc*64 + 64)>>6

    counter -= 1

acc
