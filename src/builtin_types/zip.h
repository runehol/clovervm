#ifndef CL_ZIP_H
#define CL_ZIP_H

#include "builtin_types/tuple.h"
#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"

namespace cl
{
    class ModuleObject;
    class VirtualMachine;

    class ZipIterator : public Object
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ZipIterator;

        ZipIterator(ClassObject *cls, TValue<Tuple> _sources,
                    TValue<Bool> _strict)
            : Object(cls, native_layout), sources(_sources), strict(_strict)
        {
        }

        Member<TValue<Tuple>> sources;
        Member<TValue<Bool>> strict;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ZipIterator, Object, 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(ZipIterator);
    };

    static_assert(std::is_trivially_destructible_v<ZipIterator>);

    BuiltinClassDefinition make_zip_class(VirtualMachine *vm);
    void install_zip_helpers(VirtualMachine *vm, ModuleObject *module);
    void install_zip_python_methods(VirtualMachine *vm, ModuleObject *module);
}  // namespace cl

#endif  // CL_ZIP_H
