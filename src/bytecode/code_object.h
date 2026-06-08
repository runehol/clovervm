#ifndef CL_CODE_OBJECT_H
#define CL_CODE_OBJECT_H

#include "builtin_types/module_object.h"
#include "bytecode/bytecode.h"
#include "compiler/scope.h"
#include "import_system/module_global_cache.h"
#include "object_model/attribute_cache.h"
#include "object_model/builtin_class_registry.h"
#include "object_model/owned.h"
#include "object_model/shape_key.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include "object_model/vm_array.h"
#include <algorithm>
#include <cassert>
#include <clovervm/native_module.h>
#include <cstdint>
#include <vector>

namespace cl
{
    class ThreadState;
    class VirtualMachine;

    enum class FunctionParameterFlags : uint32_t
    {
        None = 0,
        HasVarArgs = 1U << 0,
        HasKwArgs = 1U << 1,
    };

    inline FunctionParameterFlags operator|(FunctionParameterFlags lhs,
                                            FunctionParameterFlags rhs)
    {
        return FunctionParameterFlags(uint32_t(lhs) | uint32_t(rhs));
    }

    inline FunctionParameterFlags &operator|=(FunctionParameterFlags &lhs,
                                              FunctionParameterFlags rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    inline bool has_function_parameter_flag(FunctionParameterFlags flags,
                                            FunctionParameterFlags flag)
    {
        return (uint32_t(flags) & uint32_t(flag)) != 0;
    }

    struct FunctionSignature
    {
        uint32_t n_parameters = 0;
        uint32_t n_positional_parameters = 0;
        uint32_t n_posonly_parameters = 0;
        uint32_t n_pos_or_kw_parameters = 0;
        uint32_t n_kwonly_parameters = 0;
        uint32_t first_default_slot = 0;
        uint64_t default_presence_mask = 0;
        bool has_required_keyword_only_parameters = false;
        FunctionParameterFlags parameter_flags = FunctionParameterFlags::None;

        bool has_varargs() const
        {
            return has_function_parameter_flag(
                parameter_flags, FunctionParameterFlags::HasVarArgs);
        }

        bool has_kwargs() const
        {
            return has_function_parameter_flag(
                parameter_flags, FunctionParameterFlags::HasKwArgs);
        }
    };

    struct FunctionCallSignature
    {
        FunctionSignature function;
        uint32_t min_positional_arity = 0;
        uint32_t max_positional_arity = 0;
    };

    struct FunctionKeywordRemap
    {
        static constexpr uint64_t embedded_value_count =
            ValueArray<Value>::embedded_value_count +
            RawArray<uint16_t>::embedded_value_count;

        ValueArray<Value> keyword_bindable_names;
        RawArray<uint16_t> keyword_bindable_parameter_indexes;

        size_t size() const { return keyword_bindable_names.size(); }
        bool empty() const { return keyword_bindable_names.empty(); }
        Value name_at(size_t idx) const { return keyword_bindable_names[idx]; }
        uint16_t parameter_index_at(size_t idx) const
        {
            return keyword_bindable_parameter_indexes[idx];
        }

        void add(TValue<String> name, uint16_t parameter_idx)
        {
            keyword_bindable_names.push_back(name.raw_value());
            keyword_bindable_parameter_indexes.push_back(parameter_idx);
        }

        void copy_from(const FunctionKeywordRemap &other)
        {
            keyword_bindable_names.copy_from(other.keyword_bindable_names);
            keyword_bindable_parameter_indexes.copy_from(
                other.keyword_bindable_parameter_indexes);
        }

        void release_refs()
        {
            keyword_bindable_names.release_refs();
            keyword_bindable_parameter_indexes.release_refs();
        }
    };

    using IntrinsicFunction0 = Value (*)(ThreadState *);
    using IntrinsicFunction1 = Value (*)(ThreadState *, Value);
    using IntrinsicFunction2 = Value (*)(ThreadState *, Value, Value);
    using IntrinsicFunction3 = Value (*)(ThreadState *, Value, Value, Value);
    using IntrinsicFunction4 = Value (*)(ThreadState *, Value, Value, Value,
                                         Value);
    using IntrinsicFunction5 = Value (*)(ThreadState *, Value, Value, Value,
                                         Value, Value);
    using IntrinsicFunction6 = Value (*)(ThreadState *, Value, Value, Value,
                                         Value, Value, Value);
    using IntrinsicFunction7 = Value (*)(ThreadState *, Value, Value, Value,
                                         Value, Value, Value, Value);

    using UnaryHandler = Value (*)(ThreadState *, Value);
    using BinaryHandler = Value (*)(ThreadState *, Value, Value);
    using TernaryHandler = Value (*)(ThreadState *, Value, Value, Value);

    enum class TrustedHandlerArity : uint8_t
    {
        None,
        Unary,
        Binary,
        Ternary,
    };

    struct TrustedHandler
    {
        TrustedHandlerArity arity = TrustedHandlerArity::None;

        union
        {
            UnaryHandler unary;
            BinaryHandler binary;
            TernaryHandler ternary;
        };

        TrustedHandler() : arity(TrustedHandlerArity::None), unary(nullptr) {}

        static TrustedHandler none() { return TrustedHandler(); }

        static TrustedHandler for_unary(UnaryHandler handler)
        {
            TrustedHandler trusted_handler;
            trusted_handler.arity = TrustedHandlerArity::Unary;
            trusted_handler.unary = handler;
            return trusted_handler;
        }

        static TrustedHandler for_binary(BinaryHandler handler)
        {
            TrustedHandler trusted_handler;
            trusted_handler.arity = TrustedHandlerArity::Binary;
            trusted_handler.binary = handler;
            return trusted_handler;
        }

        static TrustedHandler for_ternary(TernaryHandler handler)
        {
            TrustedHandler trusted_handler;
            trusted_handler.arity = TrustedHandlerArity::Ternary;
            trusted_handler.ternary = handler;
            return trusted_handler;
        }

        bool is_none() const { return arity == TrustedHandlerArity::None; }
    };

    using TrustedHandlerResolver = TrustedHandler (*)(VirtualMachine *,
                                                      ShapeKey, ShapeKey,
                                                      ShapeKey);

    union NativeFunctionTarget
    {
        IntrinsicFunction0 fixed0;
        IntrinsicFunction1 fixed1;
        IntrinsicFunction2 fixed2;
        IntrinsicFunction3 fixed3;
        IntrinsicFunction4 fixed4;
        IntrinsicFunction5 fixed5;
        IntrinsicFunction6 fixed6;
        IntrinsicFunction7 fixed7;
        clover_extension_fn_0 extension0;
        clover_extension_fn_1 extension1;
        clover_extension_fn_2 extension2;
        clover_extension_fn_3 extension3;
        clover_extension_fn_4 extension4;
        clover_extension_fn_5 extension5;
        clover_extension_fn_6 extension6;
        clover_extension_fn_7 extension7;
    };

    struct CompilationUnit;
    class CodeObject;
    class Function;
    class ClassObject;
    class ValidityCell;

    enum class FunctionCallAdaptation : uint8_t
    {
        FixedArity,
        Defaults,
        Varargs,
    };

    struct FunctionCallInlineCache
    {
        Value guard_value = Value::not_present();
        Function *function = nullptr;
        CodeObject *code_object = nullptr;
        ValidityCell *validity_cell = nullptr;
        uint32_t n_args = UINT32_MAX;
        FunctionCallAdaptation adaptation = FunctionCallAdaptation::FixedArity;
    };

    struct OperatorInlineCache
    {
        ShapeKey operand_shape_keys[3];
        ValidityCell *method_lookup_validity_cell = nullptr;
        AttributeReadPlan method_read_plan =
            AttributeReadDescriptor::not_found().plan;
        TrustedHandler handler;
        Function *function = nullptr;
        CodeObject *code_object = nullptr;
        uint32_t n_args = UINT32_MAX;
        FunctionCallAdaptation adaptation = FunctionCallAdaptation::FixedArity;
        bool has_self = false;

        ALWAYSINLINE bool method_read_matches_operand0(Value receiver) const
        {
            return operand_shape_keys[0] == ShapeKey::from_value(receiver) &&
                   method_lookup_validity_cell != nullptr &&
                   method_lookup_validity_cell->is_valid();
        }

        ALWAYSINLINE bool matches_unary(Value operand0) const
        {
            return operand_shape_keys[0] == ShapeKey::from_value(operand0) &&
                   method_lookup_validity_cell != nullptr &&
                   method_lookup_validity_cell->is_valid();
        }

        ALWAYSINLINE bool matches_binary(Value operand0, Value operand1) const
        {
            return operand_shape_keys[0] == ShapeKey::from_value(operand0) &&
                   operand_shape_keys[1] == ShapeKey::from_value(operand1) &&
                   method_lookup_validity_cell != nullptr &&
                   method_lookup_validity_cell->is_valid();
        }

        ALWAYSINLINE bool matches_ternary(Value operand0, Value operand1,
                                          Value operand2) const
        {
            return operand_shape_keys[0] == ShapeKey::from_value(operand0) &&
                   operand_shape_keys[1] == ShapeKey::from_value(operand1) &&
                   operand_shape_keys[2] == ShapeKey::from_value(operand2) &&
                   method_lookup_validity_cell != nullptr &&
                   method_lookup_validity_cell->is_valid();
        }

        void populate_method_read(Value receiver,
                                  const AttributeReadDescriptor &descriptor)
        {
            assert(receiver.is_ptr());
            assert(descriptor.is_cacheable());
            method_lookup_validity_cell = descriptor.lookup_validity_cell;
            method_read_plan = descriptor.plan;
        }

        void populate_binary_shapes(ShapeKey operand0_shape_key,
                                    ShapeKey operand1_shape_key)
        {
            operand_shape_keys[0] = operand0_shape_key;
            operand_shape_keys[1] = operand1_shape_key;
            operand_shape_keys[2] = ShapeKey{};
        }

        void populate_ternary_shapes(ShapeKey operand0_shape_key,
                                     ShapeKey operand1_shape_key,
                                     ShapeKey operand2_shape_key)
        {
            operand_shape_keys[0] = operand0_shape_key;
            operand_shape_keys[1] = operand1_shape_key;
            operand_shape_keys[2] = operand2_shape_key;
        }

        static OperatorInlineCache python_function_call(
            Value receiver, const AttributeReadDescriptor &method_descriptor,
            ShapeKey operand0_shape_key, ShapeKey operand1_shape_key,
            ShapeKey operand2_shape_key, Function *function,
            CodeObject *code_object, uint32_t n_args, bool has_self,
            FunctionCallAdaptation adaptation)
        {
            OperatorInlineCache cache;
            cache.populate_ternary_shapes(
                operand0_shape_key, operand1_shape_key, operand2_shape_key);
            cache.function = function;
            cache.code_object = code_object;
            cache.n_args = n_args;
            cache.adaptation = adaptation;
            cache.has_self = has_self;
            if(method_descriptor.is_cacheable() && receiver.is_ptr())
            {
                cache.populate_method_read(receiver, method_descriptor);
            }
            return cache;
        }

        static OperatorInlineCache trusted_handler_call(
            Value receiver, const AttributeReadDescriptor &method_descriptor,
            ShapeKey operand0_shape_key, ShapeKey operand1_shape_key,
            ShapeKey operand2_shape_key, TrustedHandler handler)
        {
            OperatorInlineCache cache;
            cache.populate_ternary_shapes(
                operand0_shape_key, operand1_shape_key, operand2_shape_key);
            cache.handler = handler;
            if(method_descriptor.is_cacheable() && receiver.is_ptr())
            {
                cache.populate_method_read(receiver, method_descriptor);
            }
            return cache;
        }

        void clear()
        {
            operand_shape_keys[0] = ShapeKey{};
            operand_shape_keys[1] = ShapeKey{};
            operand_shape_keys[2] = ShapeKey{};
            method_lookup_validity_cell = nullptr;
            method_read_plan = AttributeReadDescriptor::not_found().plan;
            handler = TrustedHandler::none();
            function = nullptr;
            code_object = nullptr;
            n_args = UINT32_MAX;
            adaptation = FunctionCallAdaptation::FixedArity;
            has_self = false;
        }
    };

    struct KeywordCallInlineCache
    {
        Value guard_value = Value::not_present();
        Function *function = nullptr;
        CodeObject *code_object = nullptr;
        ValidityCell *validity_cell = nullptr;
        Value keyword_names = Value::not_present();
        uint32_t n_pos_args = UINT32_MAX;
        uint32_t default_fill_start_slot = 0;
        FunctionCallAdaptation adaptation = FunctionCallAdaptation::FixedArity;
        std::vector<int8_t> keyword_dest_regs;
    };

    struct ExceptionTableEntry
    {
        uint32_t start_pc;
        uint32_t end_pc;
        uint32_t handler_pc;
    };

    static constexpr int32_t FrameHeaderPreviousFpOffset = 0;
    static constexpr int32_t FrameHeaderCompiledReturnPcOffset = 1;
    static constexpr int32_t FrameHeaderReturnCodeObjectOffset = 2;
    static constexpr int32_t FrameHeaderReturnPcOffset = 3;
    static constexpr int32_t FrameHeaderSizeAboveFp = 4;
    static constexpr int32_t FrameHeaderSizeBelowFp = 0;
    static constexpr int32_t FrameHeaderSize =
        FrameHeaderSizeAboveFp + FrameHeaderSizeBelowFp;
    static_assert(FrameHeaderSizeAboveFp ==
                  FrameHeaderReturnPcOffset - FrameHeaderPreviousFpOffset + 1);
    static constexpr uint32_t ClassBodyParameterCount = 2;

    static constexpr uintptr_t FrameAlignmentBytes = 16;

    constexpr uint32_t round_up_to_abi_alignment(uint32_t value)
    {
        return (value + 1u) & ~1u;
    }

    static_assert(round_up_to_abi_alignment(0) == 0);
    static_assert(round_up_to_abi_alignment(1) == 2);
    static_assert(round_up_to_abi_alignment(2) == 2);
    static_assert(round_up_to_abi_alignment(3) == 4);

    class CodeObject : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::CodeObject;

        CodeObject(ClassObject *cls, const CompilationUnit *_compilation_unit,
                   TValue<ModuleObject> _defining_module, Scope *_local_scope,
                   TValue<String> _name)
            : Object(cls, native_layout), defining_module(_defining_module),
              local_scope(_local_scope), name(_name),
              docstring(Optional<TValue<String>>::none()),
              compilation_unit(_compilation_unit)
        {
        }

        Member<TValue<ModuleObject>> defining_module;
        MemberHeapPtr<Scope> local_scope;
        Member<TValue<String>> name;
        Member<Optional<TValue<String>>> docstring;
        const CompilationUnit *compilation_unit;

        FunctionSignature function_signature;
        FunctionKeywordRemap function_keyword_remap;
        uint32_t n_locals = 0;
        uint32_t n_temporaries = 0;
        int8_t first_free_arg_encoded_reg = 0;

        Scope *get_local_scope_ptr() const { return local_scope.extract(); }
        TValue<ModuleObject> get_defining_module() const
        {
            return defining_module.value();
        }

        bool has_varargs() const { return function_signature.has_varargs(); }

        bool has_kwargs() const { return function_signature.has_kwargs(); }

        std::vector<uint8_t> code;

        std::vector<uint32_t> source_offsets;
        std::vector<Owned<Value>> constant_table;
        std::vector<AttributeReadInlineCache> attribute_read_caches;
        std::vector<AttributeMutationInlineCache> attribute_mutation_caches;
        std::vector<ModuleGlobalReadInlineCache> module_global_read_caches;
        std::vector<ModuleGlobalMutationInlineCache>
            module_global_mutation_caches;
        std::vector<FunctionCallInlineCache> function_call_caches;
        std::vector<OperatorInlineCache> operator_caches;
        std::vector<KeywordCallInlineCache> keyword_call_caches;
        std::vector<NativeFunctionTarget> native_function_targets;
        std::vector<ExceptionTableEntry> exception_table;
        TrustedHandlerResolver trusted_handler_resolver = nullptr;

        uint32_t get_n_registers() const
        {
            return function_signature.n_parameters + n_temporaries + n_locals;
        }

        uint32_t get_padded_n_parameters() const
        {
            return round_up_to_abi_alignment(function_signature.n_parameters);
        }

        uint32_t get_padded_n_ordinary_below_frame_slots() const
        {
            return round_up_to_abi_alignment(n_locals + n_temporaries);
        }

        int8_t get_first_free_arg_encoded_reg() const
        {
            return first_free_arg_encoded_reg;
        }

        int8_t encode_reg(uint32_t reg) const
        {
            return get_padded_n_parameters() - 1 + FrameHeaderSizeAboveFp - reg;
        }

        uint32_t decode_reg(int8_t encoded_reg) const
        {
            int32_t reg = int32_t(get_padded_n_parameters()) - 1 +
                          FrameHeaderSizeAboveFp - encoded_reg;
            assert(reg >= 0);
            assert(uint32_t(reg) <
                   get_padded_n_parameters() + FrameHeaderSize +
                       get_padded_n_ordinary_below_frame_slots());
            return uint32_t(reg);
        }

        int32_t get_highest_occupied_frame_offset() const
        {
            if(function_signature.n_parameters == 0)
            {
                return 0;
            }
            return int32_t(FrameHeaderSizeAboveFp + get_padded_n_parameters() -
                           1);
        }

        size_t size() const
        {
            assert(code.size() == source_offsets.size());
            return code.size();
        }

        ALWAYSINLINE uint32_t offset_for_interpreted_pc(const uint8_t *pc) const
        {
            const uint8_t *base = code.data();
            assert(pc >= base);
            assert(pc <= base + code.size());
            return uint32_t(pc - base);
        }

        ALWAYSINLINE const uint8_t *
        interpreted_pc_for_offset(uint32_t offset) const
        {
            assert(offset < code.size());
            return code.data() + offset;
        }

        ALWAYSINLINE const ExceptionTableEntry *
        find_exception_handler(uint32_t pc_offset) const
        {
            for(const ExceptionTableEntry &entry: exception_table)
            {
                if(pc_offset >= entry.start_pc && pc_offset < entry.end_pc)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        static void dealloc(HeapObject *obj);

        CL_DECLARE_CUSTOM_DEALLOC(CodeObject, dealloc);
        CL_DECLARE_STATIC_OBJECT_SIZE(CodeObject);
    };

    BuiltinClassDefinition make_code_object_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_CODE_OBJECT_H
