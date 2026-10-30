#include "bytecode/code_object.h"
#include "jit/jit_code_object.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/refcount.h"
#include "object_model/shape.h"
#include "runtime/expanded_call.h"
#include "runtime/virtual_machine.h"

#include <cassert>
#include <type_traits>
#include <vector>

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
        for(KeywordCallInlineCache &cache: keyword_call_caches)
        {
            cache = KeywordCallInlineCache{};
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

    namespace
    {
        template <typename T>
        bool repair_weak_target(T *&target, WeakTargetResolver resolver)
        {
            if(target == nullptr)
            {
                return true;
            }
            HeapObject *replacement =
                resolver(const_cast<std::remove_const_t<T> *>(target));
            if(replacement == nullptr)
            {
                return false;
            }
            HeapReferenceSlot(&target).rewrite_pointer(replacement);
            return true;
        }

        bool repair_weak_value(Value &value, WeakTargetResolver resolver)
        {
            if(!value.is_ptr())
            {
                return true;
            }
            HeapObject *replacement = resolver(value.as.ptr);
            if(replacement == nullptr)
            {
                return false;
            }
            HeapReferenceSlot(&value).rewrite_pointer(replacement);
            return true;
        }

        bool repair_cache(AttributeReadInlineCache &cache,
                          WeakTargetResolver resolver)
        {
            if(!repair_weak_target(cache.receiver_shape, resolver) ||
               !repair_weak_target(cache.lookup_validity_cell, resolver))
            {
                return false;
            }
            switch(cache.plan.kind)
            {
                case AttributeReadPlanKind::ConstantValue:
                    return repair_weak_value(cache.plan.constant_value,
                                             resolver);
                case AttributeReadPlanKind::ReceiverSlot:
                case AttributeReadPlanKind::BindFunctionReceiver:
                case AttributeReadPlanKind::DataDescriptorGet:
                case AttributeReadPlanKind::NonDataDescriptorGet:
                    return repair_weak_target(cache.plan.storage_owner,
                                              resolver);
            }
            assert(false && "invalid attribute read plan kind");
            __builtin_unreachable();
        }

        bool repair_cache(AttributeMutationInlineCache &cache,
                          WeakTargetResolver resolver)
        {
            if(!repair_weak_target(cache.receiver_shape, resolver) ||
               !repair_weak_target(cache.lookup_validity_cell, resolver))
            {
                return false;
            }
            switch(cache.plan.kind)
            {
                case AttributeMutationPlanKind::StoreExisting:
                    return repair_weak_target(cache.plan.storage_owner,
                                              resolver);
                case AttributeMutationPlanKind::AddOwnProperty:
                case AttributeMutationPlanKind::DeleteOwnProperty:
                    return repair_weak_target(cache.plan.next_shape, resolver);
                case AttributeMutationPlanKind::ChangeClass:
                    return true;
            }
            assert(false && "invalid attribute mutation plan kind");
            __builtin_unreachable();
        }

        bool repair_cache(ModuleGlobalReadInlineCache &cache,
                          WeakTargetResolver resolver)
        {
            return repair_weak_target(cache.lookup_validity_cell, resolver) &&
                   repair_weak_target(cache.slot.storage_owner, resolver);
        }

        bool repair_cache(ModuleGlobalMutationInlineCache &cache,
                          WeakTargetResolver resolver)
        {
            return repair_weak_target(cache.lookup_validity_cell, resolver) &&
                   repair_weak_target(cache.store_existing.storage_owner,
                                      resolver);
        }

        bool repair_cache(FunctionCallInlineCache &cache,
                          WeakTargetResolver resolver)
        {
            return repair_weak_value(cache.guard_value, resolver) &&
                   repair_weak_target(cache.function, resolver) &&
                   repair_weak_target(cache.code_object, resolver) &&
                   repair_weak_target(cache.validity_cell, resolver);
        }

        bool repair_cache(KeywordCallInlineCache &cache,
                          WeakTargetResolver resolver)
        {
            return repair_weak_value(cache.guard_value, resolver) &&
                   repair_weak_target(cache.function, resolver) &&
                   repair_weak_target(cache.code_object, resolver) &&
                   repair_weak_target(cache.validity_cell, resolver);
        }

        bool repair_cache(ExpandedCallInlineCache &cache,
                          WeakTargetResolver resolver)
        {
            // binding_plan owns strong keyword names, repaired during strong
            // closure. Do not treat those names as weak cache guards.
            return repair_weak_value(cache.guard_value, resolver) &&
                   repair_weak_target(cache.function, resolver) &&
                   repair_weak_target(cache.validity_cell, resolver);
        }

        bool repair_cache(OperatorInlineCache &cache,
                          WeakTargetResolver resolver)
        {
            for(ShapeKey &key: cache.operand_shape_keys)
            {
                if(!key.repair_weak_reference(resolver))
                {
                    return false;
                }
            }
            for(ValidityCell *&cell: cache.operand_lookup_validity_cells)
            {
                if(!repair_weak_target(cell, resolver))
                {
                    return false;
                }
            }
            return repair_weak_target(cache.function, resolver) &&
                   repair_weak_target(cache.code_object, resolver);
        }

        template <typename Cache>
        void process_weak_cache_table(std::vector<Cache> &table,
                                      WeakTargetResolver resolver)
        {
            for(Cache &cache: table)
            {
                if(!repair_cache(cache, resolver))
                {
                    // No mutator or observer runs during repair. Any partial
                    // repairs are discarded together with the whole entry.
                    // Expanded-call reset releases its owned name plan; weak
                    // pointer repair itself performs no refcount operations.
                    cache = Cache{};
                }
            }
        }
    }  // namespace

    void CodeObject::process_weak_references(HeapObject *obj,
                                             WeakTargetResolver resolver)
    {
        assert(obj->native_layout_id() == native_layout);
        InlineCacheTables &caches =
            static_cast<CodeObject *>(obj)->inline_caches;
        process_weak_cache_table(caches.attribute_read_caches, resolver);
        process_weak_cache_table(caches.attribute_mutation_caches, resolver);
        process_weak_cache_table(caches.module_global_read_caches, resolver);
        process_weak_cache_table(caches.module_global_mutation_caches,
                                 resolver);
        process_weak_cache_table(caches.function_call_caches, resolver);
        process_weak_cache_table(caches.keyword_call_caches, resolver);
        process_weak_cache_table(caches.expanded_call_caches, resolver);
        process_weak_cache_table(caches.operator_caches, resolver);
    }

    void CodeObject::trace_strong_references(HeapObject *obj,
                                             StrongSlotVisitor visitor)
    {
        assert(obj->native_layout_id() == native_layout);
        CodeObject *code = static_cast<CodeObject *>(obj);
        visitor(HeapReferenceSlot(&code->shape));
        visitor(HeapReferenceSlot(&code->overflow_storage));
        visitor(HeapReferenceSlot(&code->defining_module));
        visitor(HeapReferenceSlot(&code->local_scope));
        visitor(HeapReferenceSlot(&code->name));
        visitor(HeapReferenceSlot(&code->docstring));
        visitor(HeapReferenceSlot(&code->jit_code));
        code->function_keyword_remap.keyword_bindable_names
            .visit_strong_reference_slots(visitor);
        code->function_keyword_remap.keyword_bindable_parameter_indexes
            .visit_strong_reference_slots(visitor);
        for(Owned<Value> &constant: code->constant_table)
        {
            visitor(HeapReferenceSlot(&constant));
        }
        // Cache guards and resolved targets are weak. Only the cached binding
        // plan's explicitly owned keyword names contribute strong slots here.
        for(ExpandedCallInlineCache &cache:
            code->inline_caches.expanded_call_caches)
        {
            if(cache.binding_plan != nullptr)
            {
                for(Owned<TValue<String>> &name:
                    cache.binding_plan->keyword_names)
                {
                    visitor(HeapReferenceSlot(&name));
                }
            }
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
