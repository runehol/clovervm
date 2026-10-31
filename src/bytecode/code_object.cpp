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
        gc::WeakState repair_weak_target(T *&target, WeakResolver resolver)
        {
            if(target == nullptr)
            {
                return gc::WeakState::Resolved;
            }
            WeakResolution resolution =
                resolver(const_cast<std::remove_const_t<T> *>(target));
            if(resolution.state == gc::WeakState::Resolved)
            {
                HeapReferenceSlot(&target).rewrite_pointer(resolution.target);
            }
            return resolution.state;
        }

        gc::WeakState repair_weak_value(Value &value, WeakResolver resolver)
        {
            if(!value.is_ptr())
            {
                return gc::WeakState::Resolved;
            }
            WeakResolution resolution = resolver(value.as.ptr);
            if(resolution.state == gc::WeakState::Resolved)
            {
                HeapReferenceSlot(&value).rewrite_pointer(resolution.target);
            }
            return resolution.state;
        }

        // Accumulate without short-circuiting: Pending must not prevent other
        // fields from being repaired. Dead dominates Pending for whole-entry
        // invalidation, but no field is dereferenced through its weak target.
        class WeakCacheRepair
        {
        public:
            explicit WeakCacheRepair(WeakResolver resolver)
                : resolver_(resolver)
            {
            }

            template <typename T> void target(T *&value)
            {
                observe(repair_weak_target(value, resolver_));
            }

            void value(Value &value)
            {
                observe(repair_weak_value(value, resolver_));
            }
            void key(ShapeKey &key)
            {
                observe(key.repair_weak_reference(resolver_));
            }
            gc::WeakState state() const { return state_; }

        private:
            void observe(gc::WeakState state)
            {
                if(state == gc::WeakState::Dead ||
                   state_ == gc::WeakState::Resolved)
                {
                    state_ = state;
                }
            }

            WeakResolver resolver_;
            gc::WeakState state_ = gc::WeakState::Resolved;
        };

        gc::WeakState repair_cache(AttributeReadInlineCache &cache,
                                   WeakResolver resolver)
        {
            WeakCacheRepair repair(resolver);
            repair.target(cache.receiver_shape);
            repair.target(cache.lookup_validity_cell);
            switch(cache.plan.kind)
            {
                case AttributeReadPlanKind::ConstantValue:
                    repair.value(cache.plan.constant_value);
                    return repair.state();
                case AttributeReadPlanKind::ReceiverSlot:
                case AttributeReadPlanKind::BindFunctionReceiver:
                case AttributeReadPlanKind::DataDescriptorGet:
                case AttributeReadPlanKind::NonDataDescriptorGet:
                    repair.target(cache.plan.storage_owner);
                    return repair.state();
            }
            assert(false && "invalid attribute read plan kind");
            __builtin_unreachable();
        }

        gc::WeakState repair_cache(AttributeMutationInlineCache &cache,
                                   WeakResolver resolver)
        {
            WeakCacheRepair repair(resolver);
            repair.target(cache.receiver_shape);
            repair.target(cache.lookup_validity_cell);
            switch(cache.plan.kind)
            {
                case AttributeMutationPlanKind::StoreExisting:
                    repair.target(cache.plan.storage_owner);
                    return repair.state();
                case AttributeMutationPlanKind::AddOwnProperty:
                case AttributeMutationPlanKind::DeleteOwnProperty:
                    repair.target(cache.plan.next_shape);
                    return repair.state();
                case AttributeMutationPlanKind::ChangeClass:
                    return repair.state();
            }
            assert(false && "invalid attribute mutation plan kind");
            __builtin_unreachable();
        }

        gc::WeakState repair_cache(ModuleGlobalReadInlineCache &cache,
                                   WeakResolver resolver)
        {
            WeakCacheRepair repair(resolver);
            repair.target(cache.lookup_validity_cell);
            repair.target(cache.slot.storage_owner);
            return repair.state();
        }

        gc::WeakState repair_cache(ModuleGlobalMutationInlineCache &cache,
                                   WeakResolver resolver)
        {
            WeakCacheRepair repair(resolver);
            repair.target(cache.lookup_validity_cell);
            repair.target(cache.store_existing.storage_owner);
            return repair.state();
        }

        gc::WeakState repair_cache(FunctionCallInlineCache &cache,
                                   WeakResolver resolver)
        {
            WeakCacheRepair repair(resolver);
            repair.value(cache.guard_value);
            repair.target(cache.function);
            repair.target(cache.code_object);
            repair.target(cache.validity_cell);
            return repair.state();
        }

        gc::WeakState repair_cache(KeywordCallInlineCache &cache,
                                   WeakResolver resolver)
        {
            WeakCacheRepair repair(resolver);
            repair.value(cache.guard_value);
            repair.target(cache.function);
            repair.target(cache.code_object);
            repair.target(cache.validity_cell);
            return repair.state();
        }

        gc::WeakState repair_cache(ExpandedCallInlineCache &cache,
                                   WeakResolver resolver)
        {
            // binding_plan owns strong keyword names, repaired during strong
            // closure. Do not treat those names as weak cache guards.
            WeakCacheRepair repair(resolver);
            repair.value(cache.guard_value);
            repair.target(cache.function);
            repair.target(cache.validity_cell);
            return repair.state();
        }

        gc::WeakState repair_cache(OperatorInlineCache &cache,
                                   WeakResolver resolver)
        {
            WeakCacheRepair repair(resolver);
            for(ShapeKey &key: cache.operand_shape_keys)
            {
                repair.key(key);
            }
            for(ValidityCell *&cell: cache.operand_lookup_validity_cells)
            {
                repair.target(cell);
            }
            repair.target(cache.function);
            repair.target(cache.code_object);
            return repair.state();
        }

        template <typename Cache>
        gc::WeakRepairResult repair_cache_unit(Cache &cache,
                                               WeakResolver resolver)
        {
            switch(repair_cache(cache, resolver))
            {
                case gc::WeakState::Dead:
                    // No mutator or observer runs during repair. Any partial
                    // repairs are discarded together with the whole entry.
                    // Expanded-call reset releases its owned name plan; weak
                    // pointer repair itself performs no refcount operations.
                    cache = Cache{};
                    return gc::WeakRepairResult::Complete;
                case gc::WeakState::Pending:
                    return gc::WeakRepairResult::Pending;
                case gc::WeakState::Resolved:
                    return gc::WeakRepairResult::Complete;
            }
            __builtin_unreachable();
        }

        template <typename Cache>
        void visit_weak_cache_table(std::vector<Cache> &table,
                                    WeakUnitVisitor visitor)
        {
            for(Cache &cache: table)
            {
                visitor(WeakRepairUnit::bind<repair_cache_unit<Cache>>(cache));
            }
        }
    }  // namespace

    void CodeObject::visit_weak_reference_units(HeapObject *obj,
                                                WeakUnitVisitor visitor)
    {
        assert(obj->native_layout_id() == native_layout);
        InlineCacheTables &caches =
            static_cast<CodeObject *>(obj)->inline_caches;
        visit_weak_cache_table(caches.attribute_read_caches, visitor);
        visit_weak_cache_table(caches.attribute_mutation_caches, visitor);
        visit_weak_cache_table(caches.module_global_read_caches, visitor);
        visit_weak_cache_table(caches.module_global_mutation_caches, visitor);
        visit_weak_cache_table(caches.function_call_caches, visitor);
        visit_weak_cache_table(caches.keyword_call_caches, visitor);
        visit_weak_cache_table(caches.expanded_call_caches, visitor);
        visit_weak_cache_table(caches.operator_caches, visitor);
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
