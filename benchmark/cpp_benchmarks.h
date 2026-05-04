#ifndef CLOVERVM_BENCHMARK_CPP_BENCHMARKS_H
#define CLOVERVM_BENCHMARK_CPP_BENCHMARKS_H

#include <cstdint>

namespace benchmark_cpp
{
    int64_t recursive_fib_run(int64_t n);
    int64_t recursive_fib_items(int64_t n);

    int64_t while_loop_run(int64_t n);
    int64_t while_loop_items(int64_t n);

    int64_t for_loop_run(int64_t n);
    int64_t for_loop_items(int64_t n);

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

    int64_t function_default_parameter_run(int64_t n);
    int64_t function_default_parameter_items(int64_t n);

    int64_t function_varargs_run(int64_t n);
    int64_t function_varargs_items(int64_t n);

    int64_t function_varargs_with_positional_run(int64_t n);
    int64_t function_varargs_with_positional_items(int64_t n);

    int64_t function_default_varargs_run(int64_t n);
    int64_t function_default_varargs_items(int64_t n);

    int64_t method_call_class_attribute_write_run(int64_t n);
    int64_t method_call_class_attribute_write_items(int64_t n);

    int64_t pystone_lite_run(int64_t n);
    int64_t pystone_lite_items(int64_t n);

    int64_t pystone_arithmetic_run(int64_t n);
    int64_t pystone_arithmetic_items(int64_t n);
}  // namespace benchmark_cpp

#endif
