class Product:
    def __init__(self, value=0):
        self.value = value


_Product = Product


class InstanceAttributeHolder:
    pass


instance_attribute_holder = InstanceAttributeHolder()
instance_attribute_holder.Product = Product
assert instance_attribute_holder.Product(7).value == 7


class ClassAttributeHolder:
    Product = _Product


assert ClassAttributeHolder().Product(8).value == 8


# Cached class-attribute reads observe writes on the class and secondary bases.
class MutableClassAttribute:
    value = 1


def read_mutable_class_attribute(obj):
    return obj.value


mutable_class_instance = MutableClassAttribute()
assert read_mutable_class_attribute(mutable_class_instance) == 1
MutableClassAttribute.value = 2
assert read_mutable_class_attribute(mutable_class_instance) == 2


class EmptyLeft:
    pass


class MutableRight:
    value = 1


class MultipleInheritanceAttribute(EmptyLeft, MutableRight):
    pass


multiple_inheritance_instance = MultipleInheritanceAttribute()
assert read_mutable_class_attribute(multiple_inheritance_instance) == 1
MutableRight.value = 2
assert read_mutable_class_attribute(multiple_inheritance_instance) == 2
