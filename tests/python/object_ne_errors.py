class Equal:
    def __eq__(self, other):
        return self.result
a = Equal()
b = Equal()
# Equality and truth exceptions keep their original identity.
error = ValueError('original')
class RaisesEq:
    def __eq__(self, other):
        raise error
class RaisesBool:
    def __bool__(self):
        raise error
class RaisesLen:
    def __len__(self):
        raise error
class InvalidBool:
    def __bool__(self):
        return 1
class InvalidLen:
    def __len__(self):
        return 'bad'
class NegativeLen:
    def __len__(self):
        return -1

def direct(x, y):
    return object.__ne__(x, y)
def operator(x, y):
    return x != y
for compare in [direct, operator]:
    for x in [RaisesEq(), a]:
        for truth in [RaisesBool(), RaisesLen()]:
            a.result = truth
            try:
                compare(x, b)
            except ValueError as caught:
                assert caught is error
            else:
                raise AssertionError('exception lost')
    for truth in [InvalidBool(), InvalidLen(), NegativeLen()]:
        a.result = truth
        expected_error = ValueError if isinstance(truth, NegativeLen) else TypeError
        try:
            compare(a, b)
        except expected_error:
            pass
        else:
            raise AssertionError('invalid truth accepted')
