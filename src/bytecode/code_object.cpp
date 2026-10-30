#include "bytecode/code_object.h"
#include "jit/jit_code_object.h"
#include "object_model/class_object.h"
#include "object_model/refcount.h"
#include "object_model/shape.h"
#include "runtime/expanded_call.h"
#include "runtime/virtual_machine.h"

#include <cassert>

namespace cl
{
    ExpandedCallInlineCache::ExpandedCallInlineCache() = default;
    ExpandedCallInlineCache::~ExpandedCallInlineCache() = default;
    ExpandedCallInlineCache::ExpandedCallInlineCache(
        ExpandedCallInlineCache &&) noexcept = default;
    ExpandedCallInlineCache &ExpandedCallInlineCache::operator=(
        ExpandedCallInlineCache &&) noexcept = default;

    ExpandedCallInlineCache::ExpandedCallInlineCache(
        const ExpandedCallInlineCache &other)
    {
        *this = other;
    }

    ExpandedCallInlineCache &
    ExpandedCallInlineCache::operator=(const ExpandedCallInlineCache &other)
    {
        if(this == &other)
        {
            return *this;
        }
        auto cloned_plan =
            other.binding_plan != nullptr
                ? std::make_unique<ExpandedCallCachedPlan>(*other.binding_plan)
                : nullptr;
        guard_value = other.guard_value;
        function = other.function;
        validity_cell = other.validity_cell;
        mode = other.mode;
        binding_plan = std::move(cloned_plan);
        return *this;
    }

    void InlineCacheTables::clear()
    {
        for(AttributeReadInlineCache &cache: attribute_read_caches)
        {
            cache.clear();
        }
        for(AttributeMutationInlineCache &cache: attribute_mutation_caches)
        {
            cache.clear();
        }
        for(ModuleGlobalReadInlineCache &cache: module_global_read_caches)
        {
            cache.clear();
        }
        for(ModuleGlobalMutationInlineCache &cache:
            module_global_mutation_caches)
        {
            cache.clear();
        }
        for(FunctionCallInlineCache &cache: function_call_caches)
        {
            cache = FunctionCallInlineCache{};
        }
        for(ExpandedCallInlineCache &cache: expanded_call_caches)
        {
            cache = ExpandedCallInlineCache{};
        }
        for(OperatorInlineCache &cache: operator_caches)
        {
            cache.clear();
        }
    }

    void CodeObject::release_managed_references(HeapObject *obj)
    {
        assert(obj->native_layout_id() == native_layout);
        CodeObject *code_object = static_cast<CodeObject *>(obj);

        decref_heap_ptr(code_object->shape);
        code_object->shape = nullptr;
        code_object->overflow_storage.release_ref();

        code_object->defining_module.release_ref();
        code_object->local_scope.release_ref();
        code_object->name.release_ref();
        code_object->docstring.release_ref();
        code_object->function_keyword_remap.release_refs();
        code_object->jit_code.release_ref();

        code_object->inline_caches.clear();
        code_object->constant_table.clear();
    }

    void CodeObject::destroy(HeapObject *obj)
    {
        assert(obj->native_layout_id() == native_layout);
        CodeObject *code_object = static_cast<CodeObject *>(obj);
        code_object->~CodeObject();
    }

    void CodeObject::publish_jit_code(jit::JitCodeObject *object)
    {
        assert(object != nullptr);
        assert(jit_code == nullptr);
        jit_code = object;
    }

    BuiltinClassDefinition make_code_object_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::CodeObject};
        ClassObject *cls = ClassObject::make_builtin_class<CodeObject>(
            vm->get_or_create_interned_string_value(L"code"), 0, nullptr, 0,
            vm->object_class(), SubclassPolicy::Disallowed,
            immutable_shape_flags());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }
}  // namespace cl
