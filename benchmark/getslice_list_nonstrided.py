def run(n):
    values = [3, 5, 7, 11, 13, 17, 19, 23]
    acc = 0
    i = 0
    while i < n:
        slice_value = values[1:5]
        acc += slice_value[0] + len(slice_value)
        i += 1
    return acc
