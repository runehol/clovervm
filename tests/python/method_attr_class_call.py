class Product:
    def __init__(self, value=0):
        self.value = value


class InstanceAttributeHolder:
    pass


instance_attribute_holder = InstanceAttributeHolder()
instance_attribute_holder.Product = Product
assert instance_attribute_holder.Product(7).value == 7


class ClassAttributeHolder:
    Product = Product


assert ClassAttributeHolder().Product(8).value == 8
