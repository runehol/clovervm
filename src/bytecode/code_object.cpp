#include "bytecode/code_object.h"
#include "object_model/class_object.h"
#include "object_model/refcount.h"
#include "object_model/shape.h"
#include "runtime/virtual_machine.h"

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
        code_object->function_keyword_remap.release_refs();

        for(AttributeReadInlineCache &cache: code_object->attribute_read_caches)
        {
            cache.clear();
        }
        for(AttributeMutationInlineCache &cache:
            code_object->attribute_mutation_caches)
        {
            cache.clear();
        }
        for(ModuleGlobalReadInlineCache &cache:
            code_object->module_global_read_caches)
        {
            cache.clear();
        }
        for(ModuleGlobalMutationInlineCache &cache:
            code_object->module_global_mutation_caches)
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
        ClassObject *cls = ClassObject::make_builtin_class<CodeObject>(
            vm->get_or_create_interned_string_value(L"code"),
            CodeObject::native_static_release_count(), nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }
}  // namespace cl
