#ifndef CL_CODE_OBJECT_H
#define CL_CODE_OBJECT_H

#include "attribute_cache.h"
#include "builtin_class_registry.h"
#include "bytecode.h"
#include "owned.h"
#include "owned_typed_value.h"
#include "scope.h"
#include "value.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace cl
{
    enum class FunctionParameterFlags : uint32_t
    {
        None = 0,
        HasVarArgs = 1U << 0,
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

    using NativeFunction0 = Value (*)();
    using NativeFunction1 = Value (*)(Value);
    using NativeFunction2 = Value (*)(Value, Value);
    using NativeFunction3 = Value (*)(Value, Value, Value);

    union NativeFunctionTarget
    {
        NativeFunction0 fixed0;
        NativeFunction1 fixed1;
        NativeFunction2 fixed2;
        NativeFunction3 fixed3;
    };

    struct CompilationUnit;
    struct CodeObject;
    class Function;
    class ClassObject;
    class ValidityCell;
    class VirtualMachine;

    enum class FunctionCallAdaptation : uint8_t
    {
        FixedArity,
        Defaults,
        Varargs,
    };

    enum class FunctionCallInlineCacheKind : uint8_t
    {
        Empty,
        Function,
        Constructor,
    };

    struct FunctionCallInlineCache
    {
        FunctionCallInlineCacheKind kind = FunctionCallInlineCacheKind::Empty;
        Value guard_value = Value::not_present();
        Function *function = nullptr;
        CodeObject *code_object = nullptr;
        ValidityCell *validity_cell = nullptr;
        uint32_t n_args = 0;
        FunctionCallAdaptation adaptation = FunctionCallAdaptation::FixedArity;
    };

    struct OutgoingArgReg
    {
        explicit OutgoingArgReg(uint32_t _slot_offset)
            : slot_offset(_slot_offset)
        {
        }

        uint32_t slot_offset;
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

    constexpr uint32_t round_up_to_abi_alignment(uint32_t value)
    {
        return (value + 1u) & ~1u;
    }

    static_assert(round_up_to_abi_alignment(0) == 0);
    static_assert(round_up_to_abi_alignment(1) == 2);
    static_assert(round_up_to_abi_alignment(2) == 2);
    static_assert(round_up_to_abi_alignment(3) == 4);

    struct CodeObject : public Object
    {
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::CodeObject;

        CodeObject(ClassObject *cls, const CompilationUnit *_compilation_unit,
                   Scope *_module_scope, Scope *_local_scope, Value _name)
            : Object(cls, native_layout_id, compact_layout()),
              module_scope(_module_scope), local_scope(_local_scope),
              name(_name), compilation_unit(_compilation_unit)
        {
        }

        MemberHeapPtr<Scope> module_scope;
        MemberHeapPtr<Scope> local_scope;
        MemberValue name;
        const CompilationUnit *compilation_unit;

        uint32_t n_parameters = 0;
        uint32_t n_positional_parameters = 0;
        FunctionParameterFlags parameter_flags = FunctionParameterFlags::None;
        uint32_t n_locals = 0;
        uint32_t n_temporaries = 0;
        uint32_t n_outgoing_call_slots = 0;

        Scope *get_local_scope_ptr() const { return local_scope.extract(); }

        bool has_varargs() const
        {
            return has_function_parameter_flag(
                parameter_flags, FunctionParameterFlags::HasVarArgs);
        }

        std::vector<uint8_t> code;

        std::vector<uint32_t> source_offsets;
        std::vector<OwnedValue> constant_table;
        std::vector<AttributeReadInlineCache> attribute_read_caches;
        std::vector<AttributeMutationInlineCache> attribute_mutation_caches;
        std::vector<FunctionCallInlineCache> function_call_caches;
        std::vector<NativeFunctionTarget> native_function_targets;

        uint32_t get_n_registers() const
        {
            return n_parameters + n_temporaries + n_locals +
                   n_outgoing_call_slots;
        }

        uint32_t get_padded_n_parameters() const
        {
            return round_up_to_abi_alignment(n_parameters);
        }

        uint32_t get_padded_n_ordinary_below_frame_slots() const
        {
            return round_up_to_abi_alignment(n_locals + n_temporaries);
        }

        uint32_t get_outgoing_arg_reg(uint32_t outgoing_slot_offset) const
        {
            return get_padded_n_parameters() + FrameHeaderSize +
                   get_padded_n_ordinary_below_frame_slots() +
                   outgoing_slot_offset;
        }

        int8_t encode_reg(uint32_t reg) const
        {
            return get_padded_n_parameters() - 1 + FrameHeaderSizeAboveFp - reg;
        }

        int32_t get_highest_occupied_frame_offset() const
        {
            if(n_parameters == 0)
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

        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(CodeObject, Object, 3);
    };

    BuiltinClassDefinition make_code_object_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_CODE_OBJECT_H
