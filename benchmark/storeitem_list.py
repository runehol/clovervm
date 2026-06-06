def run(n):
    values = [0, 0, 0, 0]
    i = 0
    idx = 0
    while i < n:
        values[idx] = i
        idx += 1
        if idx == 4:
            idx = 0
        i += 1
    return values[0] + values[1] + values[2] + values[3]
