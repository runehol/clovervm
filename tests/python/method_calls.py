class MethodSource:
    def method(self, x):
        return self.value + x

method_obj = MethodSource()
method_obj.value = 3
assert method_obj.method(4) == 7

class MethodVarargs:
    def method(self, *args):
        return args[0] + args[1]

assert MethodVarargs().method(4, 5) == 9

class DirectMethod:
    def method(self, x):
        return self.value + x

direct_obj = DirectMethod()
direct_obj.value = 3
assert direct_obj.method(4) == 7

class DirectZeroArgMethod:
    def method(self):
        return self.value

zero_arg_obj = DirectZeroArgMethod()
zero_arg_obj.value = 7
assert zero_arg_obj.method() == 7

class DirectOddEffectiveArgs:
    def method(self, x, y):
        return self.value + x + y

odd_args_obj = DirectOddEffectiveArgs()
odd_args_obj.value = 3
assert odd_args_obj.method(4, 5) == 12

class DirectClassFunction:
    def method(x):
        return x + 3

assert DirectClassFunction.method(4) == 7

class DirectZeroArgClassFunction:
    def method():
        return 7

assert DirectZeroArgClassFunction.method() == 7

class DirectEvenArgClassFunction:
    def method(x, y):
        return x + y

assert DirectEvenArgClassFunction.method(4, 5) == 9
