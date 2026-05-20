#include "constructor_thunk.h"

#include "class_object.h"
#include "code_object_builder.h"
#include "runtime_helpers.h"
#include "scope.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <optional>
#include <stdexcept>

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

    static CodeObject *
    make_constructor_thunk_code(ClassObject *cls,
                                Optional<TValue2<Function>> init)
    {
        Scope *local_scope = make_internal_raw<Scope>(nullptr);
        TValue<String> thunk_name(interned_string(L"<constructor_thunk>"));
        std::optional<CodeObjectBuilder> code_storage;
        CodeObject *init_code = nullptr;
        uint32_t init_n_parameters = 0;
        bool has_init = init.has_value();

        if(has_init)
        {
            TValue2<Function> init_function = init.value();
            init_code = init_function.extract()->code_object.extract();
            init_n_parameters = init_code->n_parameters;
            if(init_n_parameters == 0 ||
               init_code->n_positional_parameters == 0)
            {
                throw std::runtime_error(
                    "TypeError: __init__ requires a self parameter");
            }

            code_storage.emplace(init_code->compilation_unit,
                                 init_code->module_scope.extract(), local_scope,
                                 thunk_name);
            CodeObjectBuilder &code = *code_storage;
            code.n_parameters() = init_n_parameters - 1;
            code.n_positional_parameters() =
                init_code->n_positional_parameters - 1;
            code.parameter_flags() = init_code->parameter_flags;
        }
        else
        {
            code_storage.emplace(nullptr, active_vm()->builtin_scope_ptr(),
                                 local_scope, thunk_name);
            CodeObjectBuilder &code = *code_storage;
            code.n_parameters() = 0;
            code.n_positional_parameters() = 0;
        }

        CodeObjectBuilder &code = *code_storage;
        reserve_parameter_slots_and_frame_header(&code);

        uint32_t class_const_idx = code.allocate_constant(Value::from_oop(cls));
        code.emit_create_instance_known_class(0, class_const_idx);
        if(!has_init)
        {
            code.emit_return(0);
            return code.finalize();
        }

        {
            CodeObjectBuilder::TemporaryReg instance_reg(code);

            uint32_t init_code_const_idx =
                code.allocate_constant(Value::from_oop(init_code));
            code.emit_star(0, instance_reg);

            code.emit_ldar(0, instance_reg);
            code.emit_star(0, OutgoingArgReg(0));
            for(uint32_t param_idx = 0; param_idx < code.n_parameters();
                ++param_idx)
            {
                code.emit_ldar(0, param_idx);
                code.emit_star(0, OutgoingArgReg(param_idx + 1));
            }

            code.emit_call_code_object(0, init_code_const_idx,
                                       OutgoingArgReg(0), init_n_parameters);
            code.emit_check_init_returned_none(0);
            code.emit_ldar(0, instance_reg);
            code.emit_return(0);
        }
        return code.finalize();
    }

    static TValue<Tuple>
    make_constructor_thunk_defaults(TValue<Tuple> init_defaults,
                                    uint32_t init_n_positional_parameters)
    {
        uint32_t init_n_defaults = init_defaults.extract()->size();
        uint32_t first_default_parameter_idx =
            init_n_positional_parameters - init_n_defaults;
        if(first_default_parameter_idx > 0)
        {
            return init_defaults;
        }

        assert(init_n_defaults > 0);
        uint32_t thunk_n_defaults = init_n_defaults - 1;
        TValue<Tuple> thunk_defaults =
            make_object_value<Tuple>(static_cast<size_t>(thunk_n_defaults));
        for(uint32_t idx = 0; idx < thunk_n_defaults; ++idx)
        {
            thunk_defaults.extract()->initialize_item_unchecked(
                idx, init_defaults.extract()->item_unchecked(idx + 1));
        }
        return thunk_defaults;
    }

    TValue<Function>
    make_constructor_thunk_function(ClassObject *cls,
                                    Optional<TValue2<Function>> init)
    {
        CodeObject *code = make_constructor_thunk_code(cls, init);
        if(!init.has_value())
        {
            return make_object_value<Function>(
                TValue2<CodeObject>::from_oop(code),
                Optional<TValue2<String>>::none());
        }

        TValue2<Function> init_function = init.value();
        Optional<TValue2<Tuple>> defaults =
            init_function.extract()->default_parameters.value();
        if(!defaults.has_value())
        {
            return make_object_value<Function>(
                TValue2<CodeObject>::from_oop(code),
                Optional<TValue2<String>>::none());
        }

        TValue<Tuple> thunk_defaults = make_constructor_thunk_defaults(
            defaults.value(), init_function.extract()
                                  ->code_object.extract()
                                  ->n_positional_parameters);
        if(thunk_defaults.extract()->empty())
        {
            return make_object_value<Function>(
                TValue2<CodeObject>::from_oop(code),
                Optional<TValue2<String>>::none());
        }
        if(thunk_defaults.extract()->size() > code->n_positional_parameters)
        {
            throw std::runtime_error(
                "TypeError: unsupported __init__ default parameter layout");
        }
        return make_object_value<Function>(
            TValue2<CodeObject>::from_oop(code),
            Optional<TValue2<String>>::none(),
            Optional<TValue2<Tuple>>::some(
                TValue2<Tuple>::from_value_unchecked(thunk_defaults)));
    }
}  // namespace cl
