from collections import ChainMap


def test_item_operations():
    front = {'shared': None}
    back = {'shared': 2, 'parent': 3}
    chain = ChainMap(front, back)
    assert chain.maps[0] is front
    assert chain.maps[1] is back
    assert chain['shared'] is None
    assert chain.get('parent') == 3
    assert chain.get('missing', 10) == 10
    assert 'parent' in chain and 'missing' not in chain

    chain['parent'] = 4
    assert front['parent'] == 4 and back['parent'] == 3
    del chain['parent']
    assert chain['parent'] == 3
    try:
        del chain['parent']
        assert False
    except KeyError as error:
        assert error.args == ("Key not found in the first mapping: 'parent'",)
    assert back['parent'] == 3

    del chain['shared']
    assert chain['shared'] == 2
    try:
        chain['missing']
        assert False
    except KeyError as error:
        assert error.args == ('missing',)
    assert not bool(ChainMap({}, {}))
    assert bool(chain)
    try:
        hash(chain)
        assert False
    except TypeError:
        pass


class CustomMapping:
    def __init__(self):
        self.data = {'front': 1, 'shared': 2}

    def __getitem__(self, key):
        if key == 'broken':
            raise ValueError('lookup failed')
        return self.data[key]

    def __setitem__(self, key, value):
        self.data[key] = value

    def __delitem__(self, key):
        del self.data[key]

    def __iter__(self):
        return iter(self.data)

    def __contains__(self, key):
        return key in self.data

    def update(self, values):
        self.data.update(values)


class KeyOnlyMapping(CustomMapping):
    def __getitem__(self, key):
        raise AssertionError('key iteration must not look up values')


class SubChain(ChainMap):
    def __init__(self, *maps):
        self.received = maps
        ChainMap.__init__(self, *maps)

    def __missing__(self, key):
        return 'fallback'


def test_dispatch_and_iteration():
    front = KeyOnlyMapping()
    middle = {'shared': 3, 'middle': 4}
    back = {'back': 5, 'shared': 6}
    chain = ChainMap(front, middle, back)
    snapshot = iter(chain)
    back['late'] = 7
    assert list(snapshot) == ['back', 'shared', 'middle', 'front']
    assert list(iter(chain)) == ['back', 'shared', 'late', 'middle', 'front']
    assert len(chain) == 5
    duplicate_keys = list(iter(ChainMap({True: 'front'}, {1: 'back'})))
    assert duplicate_keys == [1] and duplicate_keys[0].__class__ is int

    mapping = CustomMapping()
    layered = SubChain(mapping, {'broken': 'must not use this value'})
    assert layered['missing'] == 'fallback'
    assert layered.get('missing', 10) == 10
    try:
        layered['broken']
        assert False
    except ValueError as error:
        assert error.args == ('lookup failed',)
    layered['new'] = 2
    assert mapping.data['new'] == 2
    del layered['new']
    assert 'new' not in mapping.data


def test_length_uses_maps_iteration():
    class Layers(list):
        def __iter__(self):
            return iter([{'virtual': 1, 'second': 2}, {'virtual': 3}])

    chain = ChainMap()
    chain.maps = Layers([{'stored': 4}])
    assert chain['virtual'] == 1
    assert len(chain) == 2


def test_iterator_acquisition_failure():
    class StopsOnIter:
        def __iter__(self):
            raise StopIteration('acquiring keys')

    chain = ChainMap({'front': 1}, StopsOnIter(), {'back': 2})
    assert list(iter(chain)) == ['back']
    try:
        len(chain)
        assert False
    except StopIteration as error:
        assert error.args == ('acquiring keys',)

    class FailsOnIter:
        def __iter__(self):
            raise ValueError('acquiring keys')

    try:
        iter(ChainMap(FailsOnIter()))
        assert False
    except ValueError as error:
        assert error.args == ('acquiring keys',)


def test_children_and_parents():
    base = {'base': 1}
    chain = SubChain(base)
    first = chain.new_child()
    second = chain.new_child(m=None, extra=3)
    assert first.__class__ is SubChain and second.__class__ is SubChain
    assert first.maps[0] == {} and first.maps[0] is not second.maps[0]
    assert first.maps[1] is base
    assert second['extra'] == 3

    empty_map = {}
    assert chain.new_child(empty_map).maps[0] is empty_map
    supplied = CustomMapping()
    child = chain.new_child(supplied, shared=8)
    assert child.received[0] is supplied and child.received[1] is base
    assert child.maps is not chain.maps
    assert supplied.data['shared'] == 8
    child['child'] = 9
    assert supplied.data['child'] == 9

    parent = child.parents
    assert parent.__class__ is SubChain and parent.received == (base,)
    assert parent.maps[0] is base and parent.maps is not child.maps
    assert child.parents is not parent
    empty = chain.parents
    assert empty.__class__ is SubChain and empty.received == ()
    assert empty.maps == [{}] and empty.maps[0] is not chain.parents.maps[0]

    alternate = {'alternate': 2}
    child.maps = [supplied, alternate]
    assert child.parents.maps[0] is alternate
    assert parent.maps[0] is base
    try:
        child.parents = parent
        assert False
    except AttributeError:
        pass


def test_call_binding():
    try:
        ChainMap(maps={})
        assert False
    except TypeError:
        pass
    chain = ChainMap()
    try:
        chain.new_child({}, {})
        assert False
    except TypeError:
        pass
    try:
        chain.new_child({}, m={})
        assert False
    except TypeError:
        pass


test_item_operations()
test_dispatch_and_iteration()
test_length_uses_maps_iteration()
test_iterator_acquisition_failure()
test_children_and_parents()
test_call_binding()
