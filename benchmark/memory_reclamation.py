def run(n):
    survivor = (0, 1, 2, 3, 4, 5, 6, 7)
    acc = 0
    for i in range(n):
        survivor = (i, i + 1, i + 2, i + 3, i + 4, i + 5, i + 6, i + 7)
        orphan = (i + 8, i + 9, i + 10, i + 11, i + 12, i + 13, i + 14,
                  i + 15)
        acc += survivor[0] + orphan[7]
    return acc
