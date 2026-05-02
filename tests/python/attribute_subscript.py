class Box:
    pass

obj = Box()
obj.value = 7
lst = [obj]
assert lst[0].value == 7

lst[0].value = 11
assert obj.value == 11

lst[0].value += 5
assert obj.value == 16

obj.lst = [4, 7, 9]
assert obj.lst[1] == 7

obj.lst[1] = 11
assert obj.lst[1] == 11

obj.lst[1] += 5
assert obj.lst[1] == 16
