#include "code_object.h"
#include "class_object.h"
#include "refcount.h"
#include "shape.h"
#include "virtual_machine.h"

#include <cassert>

namespace cl
{
    void CodeObject::dealloc(HeapObject *obj)
    {
        assert(obj->native_layout_id() == native_layout);
        CodeObject *code_object = static_cast<CodeObject *>(obj);

        decref_heap_ptr(code_object->shape);
        code_object->shape = nullptr;

        code_object->defining_module.release_ref();
        code_object->local_scope.release_ref();
        code_object->name.release_ref();
        code_object->docstring.release_ref();

        for(AttributeReadInlineCache &cache: code_object->attribute_read_caches)
        {
            cache.clear();
        }
        for(AttributeMutationInlineCache &cache:
            code_object->attribute_mutation_caches)
        {
            cache.clear();
        }
        for(FunctionCallInlineCache &cache: code_object->function_call_caches)
        {
            cache = FunctionCallInlineCache{};
        }

        code_object->~CodeObject();
    }

    BuiltinClassDefinition make_code_object_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::CodeObject};
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"code"),
            CodeObject::native_static_release_count(), nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids);
    }
}  // namespace cl
