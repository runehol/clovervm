def run(n):
    values = "abcdefgh"
    acc = 0
    i = 0
    while i < n:
        slice_value = values[1:5]
        acc += len(slice_value)
        i += 1
    return acc
