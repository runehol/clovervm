class SlotSubject:
    pass


class PropertySubject:
    def read_value(self):
        return self.stored

    def write_value(self, value):
        self.stored = value

    def delete_value(self):
        del self.stored

    value = property(read_value, write_value, delete_value)


class Descriptor:
    def __get__(self, obj, owner):
        return obj.stored

    def __set__(self, obj, value):
        obj.stored = value

    def __delete__(self, obj):
        del obj.stored


class DescriptorSubject:
    value = Descriptor()


def read(obj, n):
    total = 0
    for i in range(n):
        total += obj.value
    return total


def read_slot(n):
    obj = SlotSubject()
    obj.value = 1
    return read(obj, n)


def read_property(n):
    obj = PropertySubject()
    obj.stored = 1
    return read(obj, n)


def read_descriptor(n):
    obj = DescriptorSubject()
    obj.stored = 1
    return read(obj, n)


def write(obj, n):
    for i in range(n):
        obj.value = i


def write_slot(n):
    obj = SlotSubject()
    obj.value = -1
    write(obj, n)
    return obj.value + 1


def write_property(n):
    obj = PropertySubject()
    obj.stored = -1
    write(obj, n)
    return obj.stored + 1


def write_descriptor(n):
    obj = DescriptorSubject()
    obj.stored = -1
    write(obj, n)
    return obj.stored + 1


def reset_and_delete_slot(n):
    obj = SlotSubject()
    for i in range(n):
        obj.value = i
        del obj.value
    return -1 if hasattr(obj, "value") else n


def reset_and_delete_through_descriptor(obj, n):
    for i in range(n):
        obj.stored = i
        del obj.value
    return -1 if hasattr(obj, "stored") else n


def reset_and_delete_property(n):
    return reset_and_delete_through_descriptor(PropertySubject(), n)


def reset_and_delete_descriptor(n):
    return reset_and_delete_through_descriptor(DescriptorSubject(), n)
