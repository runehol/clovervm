#ifndef CLOVERVM_BENCHMARK_CPP_BENCHMARKS_H
#define CLOVERVM_BENCHMARK_CPP_BENCHMARKS_H

#include <cstdint>

namespace benchmark_cpp
{
    int64_t recursive_fib_run(int64_t n);
    int64_t recursive_fib_items(int64_t n);

    int64_t str_constructor_int_run(int64_t n);
    int64_t str_constructor_int_items(int64_t n);

    int64_t str_constructor_string_run(int64_t n);
    int64_t str_constructor_string_items(int64_t n);

    int64_t int_constructor_int_run(int64_t n);
    int64_t int_constructor_int_items(int64_t n);

    int64_t int_constructor_string_run(int64_t n);
    int64_t int_constructor_string_items(int64_t n);

    int64_t while_loop_run(int64_t n);
    int64_t while_loop_items(int64_t n);

    int64_t for_loop_run(int64_t n);
    int64_t for_loop_items(int64_t n);

    int64_t getitem_list_run(int64_t n);
    int64_t getitem_list_items(int64_t n);

    int64_t getitem_tuple_run(int64_t n);
    int64_t getitem_tuple_items(int64_t n);

    int64_t getitem_dict_run(int64_t n);
    int64_t getitem_dict_items(int64_t n);

    int64_t getitem_str_run(int64_t n);
    int64_t getitem_str_items(int64_t n);

    int64_t getitem_user_run(int64_t n);
    int64_t getitem_user_items(int64_t n);

    int64_t for_loop_slow_path_run(int64_t n);
    int64_t for_loop_slow_path_items(int64_t n);

    int64_t nested_for_loop_run(int64_t n);
    int64_t nested_for_loop_items(int64_t n);

    int64_t exception_bare_handler_no_raise_run(int64_t n);
    int64_t exception_bare_handler_no_raise_items(int64_t n);

    int64_t exception_typed_handler_no_raise_run(int64_t n);
    int64_t exception_typed_handler_no_raise_items(int64_t n);

    int64_t exception_bare_handler_raise_run(int64_t n);
    int64_t exception_bare_handler_raise_items(int64_t n);

    int64_t exception_typed_handler_raise_run(int64_t n);
    int64_t exception_typed_handler_raise_items(int64_t n);

    int64_t class_instantiation_run(int64_t n);
    int64_t class_instantiation_items(int64_t n);

    int64_t class_instantiation_with_init_run(int64_t n);
    int64_t class_instantiation_with_init_items(int64_t n);

    int64_t instance_attribute_add_member_run(int64_t n);
    int64_t instance_attribute_add_member_items(int64_t n);

    int64_t instance_attribute_add_after_construction_run(int64_t n);
    int64_t instance_attribute_add_after_construction_items(int64_t n);

    int64_t instance_attribute_read_run(int64_t n);
    int64_t instance_attribute_read_items(int64_t n);

    int64_t class_attribute_read_run(int64_t n);
    int64_t class_attribute_read_items(int64_t n);

    int64_t instance_attribute_write_run(int64_t n);
    int64_t instance_attribute_write_items(int64_t n);

    int64_t class_attribute_write_run(int64_t n);
    int64_t class_attribute_write_items(int64_t n);

    int64_t method_call_run(int64_t n);
    int64_t method_call_items(int64_t n);

    int64_t method_keyword_call_run(int64_t n);
    int64_t method_keyword_call_items(int64_t n);

    int64_t function_default_parameter_run(int64_t n);
    int64_t function_default_parameter_items(int64_t n);

    int64_t function_keyword_run(int64_t n);
    int64_t function_keyword_items(int64_t n);

    int64_t function_keyword_mixed_run(int64_t n);
    int64_t function_keyword_mixed_items(int64_t n);

    int64_t function_keyword_default_run(int64_t n);
    int64_t function_keyword_default_items(int64_t n);

    int64_t function_varargs_run(int64_t n);
    int64_t function_varargs_items(int64_t n);

    int64_t function_varargs_with_positional_run(int64_t n);
    int64_t function_varargs_with_positional_items(int64_t n);

    int64_t global_read_run(int64_t n);
    int64_t global_read_items(int64_t n);

    int64_t builtin_lookup_run(int64_t n);
    int64_t builtin_lookup_items(int64_t n);

    int64_t global_write_run(int64_t n);
    int64_t global_write_items(int64_t n);

    int64_t global_refcounted_write_run(int64_t n);
    int64_t global_refcounted_write_items(int64_t n);

    int64_t global_add_delete_run(int64_t n);
    int64_t global_add_delete_items(int64_t n);

    int64_t function_default_varargs_run(int64_t n);
    int64_t function_default_varargs_items(int64_t n);

    int64_t instance_attribute_refcounted_write_run(int64_t n);
    int64_t instance_attribute_refcounted_write_items(int64_t n);

    int64_t method_call_class_attribute_write_run(int64_t n);
    int64_t method_call_class_attribute_write_items(int64_t n);

    int64_t memory_reclamation_run(int64_t n);
    int64_t memory_reclamation_items(int64_t n);

    int64_t nbody_run(int64_t n);
    int64_t nbody_items(int64_t n);

    int64_t pystone_lite_run(int64_t n);
    int64_t pystone_lite_items(int64_t n);

    int64_t pystone_arithmetic_run(int64_t n);
    int64_t pystone_arithmetic_items(int64_t n);
}  // namespace benchmark_cpp

#endif
