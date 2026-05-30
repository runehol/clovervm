#include "constructor_thunk.h"

#include "class_object.h"
#include "code_object_builder.h"
#include "module_object.h"
#include "runtime_helpers.h"
#include "scope.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <optional>
#include <vector>

namespace cl
{
    static void
    reserve_parameter_slots_and_frame_header(CodeObjectBuilder *code)
    {
        Scope *local_scope = code->get_local_scope_ptr();
        local_scope->reserve_empty_slots(code->n_parameters());
        uint32_t n_parameter_padding =
            code->get_padded_n_parameters() - code->n_parameters();
        local_scope->reserve_empty_slots(n_parameter_padding);
        local_scope->reserve_empty_slots(FrameHeaderSize);
    }

    static Expected<FunctionSignature>
    init_only_constructor_thunk_signature(FunctionSignature init_signature)
    {
        FunctionSignature signature = init_signature;
        uint64_t shifted_default_mask = 0;
        bool found_default = false;
        uint32_t first_default_slot = 0;
        uint32_t init_default_span_size =
            Function::default_span_size(init_signature);
        for(uint32_t default_idx = 0; default_idx < init_default_span_size;
            ++default_idx)
        {
            if((init_signature.default_presence_mask &
                (uint64_t(1) << default_idx)) == 0)
            {
                continue;
            }
            uint32_t init_slot =
                init_signature.first_default_slot + default_idx;
            if(init_slot == 0)
            {
                continue;
            }
            uint32_t thunk_slot = init_slot - 1;
            if(!found_default)
            {
                found_default = true;
                first_default_slot = thunk_slot;
            }
            shifted_default_mask |= uint64_t(1)
                                    << (thunk_slot - first_default_slot);
        }

        --signature.n_parameters;
        --signature.n_positional_parameters;
        if(signature.n_posonly_parameters > 0)
        {
            --signature.n_posonly_parameters;
        }
        else
        {
            if(signature.n_pos_or_kw_parameters == 0)
            {
                return Expected<FunctionSignature>::raise_exception(
                    L"TypeError", L"__init__ requires a self parameter");
            }
            --signature.n_pos_or_kw_parameters;
        }
        if(found_default)
        {
            signature.first_default_slot = first_default_slot;
            signature.default_presence_mask = shifted_default_mask;
        }
        else
        {
            signature.first_default_slot = 0;
            signature.default_presence_mask = 0;
        }
        return Expected<FunctionSignature>::ok(signature);
    }

    static Expected<CodeObject *>
    make_init_only_constructor_thunk_code(ClassObject *cls,
                                          Optional<TValue<Function>> init)
    {
        Scope *local_scope = make_internal_raw<Scope>(nullptr);
        TValue<String> thunk_name(interned_string(L"<constructor_thunk>"));
        std::optional<CodeObjectBuilder> code_storage;
        CodeObject *init_code = nullptr;
        uint32_t init_n_parameters = 0;
        bool has_init = init.has_value();

        if(has_init)
        {
            TValue<Function> init_function = init.value();
            init_code = init_function.extract()->code_object.extract();
            init_n_parameters = init_code->function_signature.n_parameters;
            if(init_n_parameters == 0 ||
               init_code->function_signature.n_positional_parameters == 0)
            {
                return Expected<CodeObject *>::raise_exception(
                    L"TypeError", L"__init__ requires a self parameter");
            }

            code_storage.emplace(init_code->compilation_unit,
                                 init_code->get_defining_module(), local_scope,
                                 thunk_name);
            CodeObjectBuilder &code = *code_storage;
            code.function_signature() =
                CL_TRY(init_only_constructor_thunk_signature(
                    init_code->function_signature));
            for(size_t remap_idx = 0;
                remap_idx < init_code->function_keyword_remap.size();
                ++remap_idx)
            {
                uint16_t init_parameter_idx =
                    init_code->function_keyword_remap.parameter_index_at(
                        remap_idx);
                if(init_parameter_idx == 0)
                {
                    continue;
                }
                code.function_keyword_remap().add(
                    TValue<String>::from_value_assumed(
                        init_code->function_keyword_remap.name_at(remap_idx)),
                    uint16_t(init_parameter_idx - 1));
            }
        }
        else
        {
            TValue<ModuleObject> builtins_module =
                active_vm()->global_builtins_module();
            code_storage.emplace(nullptr, builtins_module, local_scope,
                                 thunk_name);
            CodeObjectBuilder &code = *code_storage;
            code.n_parameters() = 0;
            code.n_positional_parameters() = 0;
            code.function_signature().n_pos_or_kw_parameters = 0;
        }

        CodeObjectBuilder &code = *code_storage;
        reserve_parameter_slots_and_frame_header(&code);

        uint32_t class_const_idx =
            CL_TRY(code.allocate_constant(Value::from_oop(cls)));
        CL_TRY(code.emit_create_instance_known_class(0, class_const_idx));
        if(!has_init)
        {
            CL_TRY(code.emit_return(0));
            return code.finalize();
        }

        {
            CodeObjectBuilder::TemporaryReg instance_reg(code);

            uint32_t init_code_const_idx =
                CL_TRY(code.allocate_constant(Value::from_oop(init_code)));
            CL_TRY(code.emit_star(0, instance_reg));

            CL_TRY(code.emit_ldar(0, instance_reg));
            CL_TRY(code.emit_star(0, OutgoingArgReg(0)));
            for(uint32_t param_idx = 0; param_idx < code.n_parameters();
                ++param_idx)
            {
                CL_TRY(code.emit_ldar(0, param_idx));
                CL_TRY(code.emit_star(0, OutgoingArgReg(param_idx + 1)));
            }

            CL_TRY(code.emit_call_code_object(
                0, init_code_const_idx, OutgoingArgReg(0), init_n_parameters));
            CL_TRY(code.emit_check_init_returned_none(0));
            CL_TRY(code.emit_ldar(0, instance_reg));
            CL_TRY(code.emit_return(0));
        }
        return code.finalize();
    }

    static Expected<TValue<Tuple>>
    make_init_only_constructor_thunk_defaults(TValue<Tuple> init_defaults,
                                              FunctionSignature init_signature)
    {
        uint32_t init_default_span_size =
            Function::default_span_size(init_signature);
        if(init_defaults.extract()->size() != init_default_span_size)
        {
            return Expected<TValue<Tuple>>::raise_exception(
                L"SystemError",
                L"unsupported __init__ default parameter layout");
        }

        FunctionSignature thunk_signature =
            CL_TRY(init_only_constructor_thunk_signature(init_signature));
        uint32_t thunk_default_span_size =
            Function::default_span_size(thunk_signature);
        TValue<Tuple> thunk_defaults = make_object_value<Tuple>(
            static_cast<size_t>(thunk_default_span_size));
        if(thunk_default_span_size == 0)
        {
            return Expected<TValue<Tuple>>::ok(thunk_defaults);
        }

        std::vector<Value> default_values(thunk_default_span_size,
                                          Value::None());
        for(uint32_t default_idx = 0; default_idx < init_default_span_size;
            ++default_idx)
        {
            if((init_signature.default_presence_mask &
                (uint64_t(1) << default_idx)) == 0)
            {
                continue;
            }
            uint32_t init_slot =
                init_signature.first_default_slot + default_idx;
            if(init_slot == 0)
            {
                continue;
            }
            uint32_t thunk_slot = init_slot - 1;
            uint32_t thunk_default_idx =
                thunk_slot - thunk_signature.first_default_slot;
            default_values[thunk_default_idx] =
                init_defaults.extract()->item_unchecked(default_idx);
        }

        for(uint32_t idx = 0; idx < thunk_default_span_size; ++idx)
        {
            thunk_defaults.extract()->initialize_item_unchecked(
                idx, default_values[idx]);
        }
        return Expected<TValue<Tuple>>::ok(thunk_defaults);
    }

    Expected<TValue<Function>>
    make_init_only_constructor_thunk_function(ClassObject *cls,
                                              Optional<TValue<Function>> init)
    {
        CodeObject *code =
            CL_TRY(make_init_only_constructor_thunk_code(cls, init));
        if(!init.has_value())
        {
            return Expected<TValue<Function>>::ok(
                make_object_value<Function>(TValue<CodeObject>::from_oop(code),
                                            Optional<TValue<String>>::none()));
        }

        TValue<Function> init_function = init.value();
        Optional<TValue<Tuple>> defaults =
            init_function.extract()->default_parameters.value();
        if(!defaults.has_value())
        {
            return Expected<TValue<Function>>::ok(
                make_object_value<Function>(TValue<CodeObject>::from_oop(code),
                                            Optional<TValue<String>>::none()));
        }

        FunctionSignature init_signature =
            init_function.extract()->code_object.extract()->function_signature;
        TValue<Tuple> thunk_defaults =
            CL_TRY(make_init_only_constructor_thunk_defaults(defaults.value(),
                                                             init_signature));
        if(thunk_defaults.extract()->empty())
        {
            return Expected<TValue<Function>>::ok(
                make_object_value<Function>(TValue<CodeObject>::from_oop(code),
                                            Optional<TValue<String>>::none()));
        }
        if(thunk_defaults.extract()->size() !=
           Function::default_span_size(code->function_signature))
        {
            return Expected<TValue<Function>>::raise_exception(
                L"SystemError",
                L"unsupported __init__ default parameter layout");
        }
        return Expected<TValue<Function>>::ok(make_object_value<Function>(
            TValue<CodeObject>::from_oop(code),
            Optional<TValue<String>>::none(),
            Optional<TValue<Tuple>>::some(thunk_defaults)));
    }
}  // namespace cl
