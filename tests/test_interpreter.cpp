#include "attr.h"
#include "class_object.h"
#include "code_object_builder.h"
#include "code_object_print.h"
#include "codegen.h"
#include "compilation_unit.h"
#include "dict.h"
#include "exception_object.h"
#include "function.h"
#include "instance.h"
#include "interpreter.h"
#include "list.h"
#include "native_function.h"
#include "parser.h"
#include "python_exception.h"
#include "range_iterator.h"
#include "scope.h"
#include "shape.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "token_print.h"
#include "tokenizer.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <stdexcept>

using namespace cl;

static constexpr int64_t kMinSmi = -288230376151711744LL;

static void expect_runtime_error(const wchar_t *source,
                                 const char *expected_message)
{
    try
    {
        (void)test::FileRunner(source);
        FAIL() << "Expected std::runtime_error with message: "
               << expected_message;
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ(expected_message, err.what());
    }
}

static std::string narrow_test_wstring(const wchar_t *message)
{
    std::string result;
    for(const wchar_t *ch = message; *ch != 0; ++ch)
    {
        result.push_back(*ch >= 0 && *ch <= 0x7f ? static_cast<char>(*ch)
                                                 : '?');
    }
    return result;
}

static void expect_python_error(const wchar_t *source,
                                const wchar_t *expected_message)
{
    try
    {
        (void)test::FileRunner(source);
        FAIL() << "Expected PythonException";
    }
    catch(const PythonException &err)
    {
        EXPECT_STREQ(expected_message, err.wide_what().c_str());
        EXPECT_STREQ(narrow_test_wstring(expected_message).c_str(), err.what());
    }
}

static void expect_range_iterator(Value actual, int64_t expected_current,
                                  int64_t expected_stop, int64_t expected_step)
{
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::RangeIterator,
              actual.get_ptr<Object>()->native_layout_id());

    RangeIterator *iterator = actual.get_ptr<RangeIterator>();
    EXPECT_EQ(Value::from_smi(expected_current), iterator->current);
    EXPECT_EQ(Value::from_smi(expected_stop), iterator->stop);
    EXPECT_EQ(Value::from_smi(expected_step), iterator->step);
}

static int64_t g_next_counter = 0;

static Value native_next_counter() { return Value::from_smi(g_next_counter++); }

static Value native_zero() { return Value::from_smi(17); }

static Value native_increment(Value value)
{
    if(!value.is_smi())
    {
        throw std::runtime_error("native_increment expected a smi");
    }
    return Value::from_smi(value.get_smi() + 1);
}

static Value native_add(Value left, Value right)
{
    if(!left.is_smi() || !right.is_smi())
    {
        throw std::runtime_error("native_add expected smi arguments");
    }
    return Value::from_smi(left.get_smi() + right.get_smi());
}

static Value native_stop_iteration_with_value()
{
    return active_thread()->set_pending_stop_iteration_value(
        Value::from_smi(123));
}

static Value native_marker_without_pending_exception()
{
    return Value::exception_marker();
}

static Value native_base_exception_with_message()
{
    ClassObject *cls =
        active_thread()->class_for_native_layout(NativeLayoutId::Exception);
    return active_thread()->set_pending_exception_string(
        TValue<ClassObject>::from_oop(cls), L"boom");
}

static void bind_global(test::VmTestContext &test_context,
                        CodeObject *code_object, const wchar_t *name,
                        Value value)
{
    TValue<String> name_value(
        test_context.vm().get_or_create_interned_string_value(name));
    code_object->module_scope.extract()->set_by_name(name_value, value);
}

static Value make_test_function(test::VmTestContext &test_context,
                                const wchar_t *name, const wchar_t *source)
{
    CodeObject *code_object = test_context.compile_file(source);
    (void)test_context.thread()->run(code_object);

    TValue<String> name_value(
        test_context.vm().get_or_create_interned_string_value(name));
    Value function_value =
        code_object->module_scope.extract()->get_by_name(name_value);
    assert(function_value.is_ptr());
    assert(function_value.get_ptr<Object>()->native_layout_id() ==
           NativeLayoutId::Function);
    return function_value;
}

static CodeObject *make_raise_unwind_code(test::VmTestContext &test_context,
                                          Value raised)
{
    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"<raise-test>");
    CodeObjectBuilder builder(&test_context.vm(), nullptr, nullptr, nullptr,
                              name);
    uint32_t constant_idx = builder.allocate_constant(raised);
    builder.emit_lda_constant(0, uint8_t(constant_idx));
    builder.emit_raise_unwind(0);
    builder.emit_halt(0);
    return builder.finalize();
}

TEST(Interpreter, assert_statement_raises_assertion_error)
{
    expect_python_error(L"assert False\n", L"AssertionError");
}

TEST(Interpreter, assert_statement_raises_assertion_error_with_message)
{
    expect_python_error(L"assert False, \"basic math is broken\"\n",
                        L"AssertionError: basic math is broken");
}

TEST(Interpreter, assert_statement_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    assert False\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"AssertionError");
}

TEST(Interpreter, del_global_removes_binding)
{
    expect_python_error(L"value = 7\n"
                        L"del value\n"
                        L"value\n",
                        L"NameError: name 'value' is not defined");
}

TEST(Interpreter, del_missing_global_raises_name_error)
{
    expect_python_error(L"del missing\n",
                        L"NameError: name 'missing' is not defined");
}

TEST(Interpreter, del_local_variable_removes_binding)
{
    expect_python_error(L"def clear(value):\n"
                        L"    del value\n"
                        L"    return value\n"
                        L"clear(7)\n",
                        L"NameError: name 'value' is not defined");
}

TEST(Interpreter, del_missing_local_raises_name_error)
{
    expect_python_error(L"def clear():\n"
                        L"    del value\n"
                        L"clear()\n",
                        L"NameError: name 'value' is not defined");
}

TEST(Interpreter, local_read_before_assignment_raises_name_error)
{
    expect_python_error(L"def read_before_write():\n"
                        L"    value\n"
                        L"    value = 7\n"
                        L"read_before_write()\n",
                        L"NameError: name 'value' is not defined");
}

TEST(Interpreter, conditional_local_assignment_raises_on_missing_path)
{
    expect_python_error(L"def maybe_write(flag):\n"
                        L"    if flag:\n"
                        L"        value = 7\n"
                        L"    return value\n"
                        L"maybe_write(False)\n",
                        L"NameError: name 'value' is not defined");
}

TEST(Interpreter, function_wrong_arity)
{
    expect_python_error(L"def f(a):\n"
                        L"    return a\n"
                        L"f()\n",
                        L"TypeError: wrong number of arguments");
    expect_python_error(L"def f(a):\n"
                        L"    return a\n"
                        L"f(1, 2)\n",
                        L"TypeError: wrong number of arguments");
}

TEST(Interpreter, function_wrong_arity_unwinds_nested_frames)
{
    expect_python_error(L"def f(a):\n"
                        L"    return a\n"
                        L"def fail():\n"
                        L"    f()\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError: wrong number of arguments");
}

TEST(Interpreter, function_varargs_collect_empty_tuple)
{
    test::FileRunner file_runner(L"def f(*args):\n"
                                 "    return args\n"
                                 "f()\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_TRUE(TValue<Tuple>::from_value_checked(actual).extract()->empty());
}

TEST(Interpreter, function_varargs_still_requires_positional_arguments)
{
    expect_python_error(L"def f(a, *args):\n"
                        L"    return a\n"
                        L"f()\n",
                        L"TypeError: wrong number of arguments");
}

TEST(Interpreter, class_definition_binds_class_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"Cls\n");

    Value actual = test_context.thread()->run(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"Cls",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
}

TEST(Interpreter, class_body_assignment_becomes_class_member)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    value = 7\n");
    (void)test_context.thread()->run(code_obj);

    Value cls_value = code_obj->module_scope.extract()->get_by_name(cls_name);
    ASSERT_TRUE(cls_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              cls_value.get_ptr<Object>()->native_layout_id());
    ClassObject *cls = cls_value.get_ptr<ClassObject>();
    EXPECT_EQ(Value::from_smi(7), load_attr(Value::from_oop(cls), value_name));

    Shape *shape = cls->get_shape();
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    StorageLocation value_location =
        shape->resolve_present_property(value_name);
    ASSERT_TRUE(value_location.is_found());
    EXPECT_EQ(StorageKind::Inline, value_location.kind);
    EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotCount),
              value_location.physical_idx);
    EXPECT_EQ(Value::from_smi(7), cls->read_storage_location(value_location));
    ASSERT_EQ(ClassObject::kClassMetadataSlotCount + 1, shape->present_count());
    EXPECT_STREQ(L"value",
                 shape->get_property_name(ClassObject::kClassMetadataSlotCount)
                     .extract()
                     ->data);
}

TEST(Interpreter, class_body_attributes_preserve_shape_insertion_order)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> first_name(
        test_context.vm().get_or_create_interned_string_value(L"first"));
    TValue<String> second_name(
        test_context.vm().get_or_create_interned_string_value(L"second"));
    TValue<String> third_name(
        test_context.vm().get_or_create_interned_string_value(L"third"));

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    first = 1\n"
                                                     L"    second = 2\n"
                                                     L"    third = 3\n");
    (void)test_context.thread()->run(code_obj);

    Value cls_value = code_obj->module_scope.extract()->get_by_name(cls_name);
    ASSERT_TRUE(cls_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              cls_value.get_ptr<Object>()->native_layout_id());
    ClassObject *cls = cls_value.get_ptr<ClassObject>();

    TValue<String> names[] = {first_name, second_name, third_name};
    for(uint32_t idx = 0; idx < 3; ++idx)
    {
        EXPECT_STREQ(string_as_wchar_t(names[idx]),
                     string_as_wchar_t(cls->get_shape()->get_property_name(
                         ClassObject::kClassMetadataSlotCount + idx)));

        StorageLocation location =
            cls->get_shape()->resolve_present_property(names[idx]);
        ASSERT_TRUE(location.is_found());
        EXPECT_EQ(StorageKind::Inline, location.kind);
        EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotCount + idx),
                  location.physical_idx);
        EXPECT_EQ(Value::from_smi(idx + 1),
                  cls->read_storage_location(location));
    }
}

TEST(Interpreter, class_body_readonly_metadata_store_is_rejected)
{
    expect_runtime_error(L"class Cls:\n"
                         L"    __name__ = 1\n",
                         "TypeError: cannot set read-only class attribute");
}

TEST(Interpreter, set_name_notification_is_explicitly_unsupported)
{
    expect_runtime_error(L"class Descriptor:\n"
                         L"    def __set_name__(self, owner, name):\n"
                         L"        self.owner = owner\n"
                         L"class Owner:\n"
                         L"    field = Descriptor()\n",
                         "TypeError: __set_name__ notifications are not "
                         "implemented yet");
}

TEST(Interpreter, class_call_allocates_instance)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"Cls()\n");

    Value actual = test_context.thread()->run(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Instance,
              actual.get_ptr<Object>()->native_layout_id());
    ASSERT_TRUE(actual.get_ptr<Instance>()->get_class().as_value().is_ptr());
    EXPECT_EQ(NativeLayoutId::ClassObject, actual.get_ptr<Instance>()
                                               ->get_class()
                                               .as_value()
                                               .get_ptr<Object>()
                                               ->native_layout_id());
}

TEST(Interpreter, class_constructor_rejects_non_none_init_return)
{
    expect_python_error(L"class Cls:\n"
                        L"    def __init__(self):\n"
                        L"        return 1\n"
                        L"Cls()\n",
                        L"TypeError: __init__ should return None, not a value");
}

TEST(Interpreter, class_constructor_non_none_init_return_unwinds_nested_frames)
{
    expect_python_error(L"class Cls:\n"
                        L"    def __init__(self):\n"
                        L"        return 1\n"
                        L"def fail():\n"
                        L"    Cls()\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError: __init__ should return None, not a value");
}

TEST(Interpreter, string_literal_value)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"\"abc\"\n");

    EXPECT_STREQ(L"abc",
                 string_as_wchar_t(TValue<String>::from_value_checked(actual)));
}

TEST(Interpreter, string_dunder_add_calls_native_function)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"\"ab\".__add__(\"cd\")\n");

    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_STREQ(L"abcd",
                 string_as_wchar_t(TValue<String>::from_value_checked(actual)));
}

TEST(Interpreter, string_dunder_add_wrong_type_reports_unimplemented)
{
    expect_python_error(L"\"ab\".__add__(3)\n", L"UnimplementedError");
}

TEST(Interpreter, string_dunder_add_wrong_type_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    return \"ab\".__add__(3)\n"
                        L"fail()\n",
                        L"UnimplementedError");
}

TEST(Interpreter, list_literal_returns_list_object)
{
    test::FileRunner file_runner(L"[1, 2, 4]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::List,
              actual.get_ptr<Object>()->native_layout_id());
    List *list = actual.get_ptr<List>();
    ASSERT_EQ(3u, list->size());
    EXPECT_EQ(Value::from_smi(1), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), list->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(4), list->item_unchecked(2));
}

TEST(Interpreter, empty_list_literal_returns_empty_list)
{
    test::FileRunner file_runner(L"[]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::List,
              actual.get_ptr<Object>()->native_layout_id());
    List *list = actual.get_ptr<List>();
    EXPECT_TRUE(list->empty());
    EXPECT_EQ(0u, list->size());
}

TEST(Interpreter, list_literal_evaluates_elements_left_to_right)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(
        L"[next_counter(), next_counter(), next_counter()]\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_native_function(&test_context.vm(), native_next_counter);
    code_obj->module_scope.extract()->set_by_name(name, next_counter);

    Value actual = test_context.thread()->run(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::List,
              actual.get_ptr<Object>()->native_layout_id());
    List *list = actual.get_ptr<List>();
    ASSERT_EQ(3u, list->size());
    EXPECT_EQ(Value::from_smi(0), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(1), list->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(2), list->item_unchecked(2));
}

TEST(Interpreter, tuple_literal_returns_tuple_object)
{
    test::FileRunner file_runner(L"1, 2, 4\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(4), tuple->item_unchecked(2));
}

TEST(Interpreter, parenthesized_tuple_literal_returns_tuple_object)
{
    test::FileRunner file_runner(L"(1, 2, 4)\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(4), tuple->item_unchecked(2));
}

TEST(Interpreter, empty_tuple_literal_returns_empty_tuple_object)
{
    test::FileRunner file_runner(L"()\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    EXPECT_EQ(0u, tuple->size());
}

TEST(Interpreter, tuple_literal_evaluates_elements_left_to_right)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(
        L"next_counter(), next_counter(), next_counter()\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_native_function(&test_context.vm(), native_next_counter);
    code_obj->module_scope.extract()->set_by_name(name, next_counter);

    Value actual = test_context.thread()->run(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(0), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(2));
}

TEST(Interpreter, subscript_load_reads_tuple_item)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"Cls.__mro__[0]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"Cls",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
}

TEST(Interpreter, subscript_load_reads_tuple_item_with_negative_index)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"Cls.__mro__[-1]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"object",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
}

TEST(Interpreter, dict_literal_returns_dict_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    Value actual = test_context.run_file(L"key = \"alpha\"\n"
                                         L"{key: 7, \"beta\": 9}\n");

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Dict,
              actual.get_ptr<Object>()->native_layout_id());
    Dict *dict = actual.get_ptr<Dict>();
    EXPECT_EQ(2u, dict->size());

    Value alpha =
        test_context.vm().get_or_create_interned_string_value(L"alpha");
    Value beta = test_context.vm().get_or_create_interned_string_value(L"beta");
    EXPECT_EQ(Value::from_smi(7), dict->get_item(alpha));
    EXPECT_EQ(Value::from_smi(9), dict->get_item(beta));
}

TEST(Interpreter,
     dict_subscript_augmented_assignment_evaluates_receiver_and_key_once)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"xs = {\"alpha\": 10, \"beta\": 20}\n"
                                  L"def get_dict():\n"
                                  L"    return xs\n"
                                  L"def next_key():\n"
                                  L"    next_counter()\n"
                                  L"    return \"alpha\"\n"
                                  L"get_dict()[next_key()] += 7\n"
                                  L"xs[\"alpha\"]\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_native_function(&test_context.vm(), native_next_counter);
    code_obj->module_scope.extract()->set_by_name(name, next_counter);

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
    EXPECT_EQ(1, g_next_counter);
}

TEST(Interpreter, subscript_store_rejects_tuple_item_assignment)
{
    expect_python_error(
        L"class Cls:\n"
        L"    pass\n"
        L"Cls.__mro__[0] = 1\n",
        L"TypeError: 'tuple' object does not support item assignment");
}

TEST(Interpreter, subscript_store_tuple_item_assignment_unwinds_nested_frames)
{
    expect_python_error(
        L"def fail():\n"
        L"    class Cls:\n"
        L"        pass\n"
        L"    Cls.__mro__[0] = 1\n"
        L"fail()\n",
        L"TypeError: 'tuple' object does not support item assignment");
}

TEST(Interpreter, subscript_delete_rejects_tuple_item_deletion)
{
    expect_python_error(
        L"class Cls:\n"
        L"    pass\n"
        L"del Cls.__mro__[0]\n",
        L"TypeError: 'tuple' object does not support item deletion");
}

TEST(Interpreter, subscript_delete_tuple_item_deletion_unwinds_nested_frames)
{
    expect_python_error(
        L"def fail():\n"
        L"    class Cls:\n"
        L"        pass\n"
        L"    del Cls.__mro__[0]\n"
        L"fail()\n",
        L"TypeError: 'tuple' object does not support item deletion");
}

TEST(Interpreter,
     subscript_augmented_assignment_evaluates_receiver_and_index_once)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"xs = [10, 20, 30]\n"
                                  L"def get_list():\n"
                                  L"    return xs\n"
                                  L"get_list()[next_counter()] += 7\n"
                                  L"xs[0]\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_native_function(&test_context.vm(), native_next_counter);
    code_obj->module_scope.extract()->set_by_name(name, next_counter);

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
    EXPECT_EQ(1, g_next_counter);
}

TEST(Interpreter, subscript_load_rejects_non_integer_list_index)
{
    expect_python_error(L"xs = [1, 2, 3]\n"
                        L"xs[False]\n",
                        L"TypeError: list indices must be integers");
}

TEST(Interpreter, subscript_load_non_integer_list_index_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    xs = [1, 2, 3]\n"
                        L"    return xs[False]\n"
                        L"fail()\n",
                        L"TypeError: list indices must be integers");
}

TEST(Interpreter, subscript_load_rejects_non_integer_tuple_index)
{
    expect_python_error(L"class Cls:\n"
                        L"    pass\n"
                        L"Cls.__mro__[False]\n",
                        L"TypeError: tuple indices must be integers");
}

TEST(Interpreter, subscript_load_rejects_out_of_range_list_index)
{
    expect_python_error(L"xs = [1, 2, 3]\n"
                        L"xs[3]\n",
                        L"IndexError: list index out of range");
}

TEST(Interpreter, subscript_load_out_of_range_list_index_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    xs = [1, 2, 3]\n"
                        L"    return xs[3]\n"
                        L"fail()\n",
                        L"IndexError: list index out of range");
}

TEST(Interpreter, subscript_load_rejects_out_of_range_tuple_index)
{
    expect_python_error(L"class Cls:\n"
                        L"    pass\n"
                        L"Cls.__mro__[2]\n",
                        L"IndexError: tuple index out of range");
}

TEST(Interpreter, subscript_load_out_of_range_tuple_index_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    class Cls:\n"
                        L"        pass\n"
                        L"    return Cls.__mro__[2]\n"
                        L"fail()\n",
                        L"IndexError: tuple index out of range");
}

TEST(Interpreter, subscript_load_missing_dict_key_raises_key_error)
{
    expect_python_error(L"xs = {\"alpha\": 1}\n"
                        L"xs[\"beta\"]\n",
                        L"KeyError");
}

TEST(Interpreter, subscript_load_missing_dict_key_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    xs = {\"alpha\": 1}\n"
                        L"    return xs[\"beta\"]\n"
                        L"fail()\n",
                        L"KeyError");
}

TEST(Interpreter, subscript_load_rejects_non_subscriptable_receiver)
{
    expect_python_error(L"1[0]\n", L"TypeError: object is not subscriptable");
}

TEST(Interpreter, subscript_load_non_subscriptable_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    return 1[0]\n"
                        L"fail()\n",
                        L"TypeError: object is not subscriptable");
}

TEST(Interpreter, subscript_delete_rejects_non_subscriptable_receiver)
{
    expect_python_error(L"del (1)[0]\n",
                        L"TypeError: object is not subscriptable");
}

TEST(Interpreter, subscript_delete_missing_dict_key_raises_key_error)
{
    expect_python_error(L"xs = {\"alpha\": 1}\n"
                        L"del xs[\"beta\"]\n",
                        L"KeyError");
}

TEST(Interpreter, subscript_delete_missing_dict_key_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    xs = {\"alpha\": 1}\n"
                        L"    del xs[\"beta\"]\n"
                        L"fail()\n",
                        L"KeyError");
}

TEST(Interpreter, attribute_load_and_store_syntax)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> obj_name(
        test_context.vm().get_or_create_interned_string_value(L"obj"));
    TValue<String> attr_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"obj = Cls()\n"
                                                     L"obj.value = 7\n"
                                                     L"obj.value\n");
    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(7), actual);
    Scope *module_scope = code_obj->module_scope.extract();
    Value obj_value = module_scope->get_by_name(obj_name);
    ASSERT_TRUE(obj_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::Instance,
              obj_value.get_ptr<Object>()->native_layout_id());
    EXPECT_EQ(Value::from_smi(7),
              obj_value.get_ptr<Instance>()->get_own_property(attr_name));
}

TEST(Interpreter, store_attr_caches_instance_add_transition)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"make"));
    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"def make(value):\n"
                                                     L"    obj = Cls()\n"
                                                     L"    obj.value = value\n"
                                                     L"    return obj.value\n"
                                                     L"make(1)\n"
                                                     L"make(2)\n");

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(2), actual);

    Value function_value =
        code_obj->module_scope.extract()->get_by_name(function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->attribute_mutation_caches.size());
    const AttributeMutationInlineCache &cache =
        function_code->attribute_mutation_caches[0];
    ASSERT_NE(nullptr, cache.receiver_shape);
    ASSERT_NE(nullptr, cache.plan.next_shape);
    ASSERT_TRUE(cache.plan.storage_location().is_found());
    EXPECT_TRUE(cache.plan.is_add_own_property());
    ASSERT_NE(nullptr, cache.plan.lookup_validity_cell);
    EXPECT_TRUE(cache.plan.lookup_validity_cell->is_valid());
}

TEST(Interpreter, del_attr_deletes_instance_property_and_caches_plan)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> clear_name(
        test_context.vm().get_or_create_interned_string_value(L"clear"));
    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    CodeObject *definition_code =
        test_context.compile_file(L"def clear(obj):\n"
                                  L"    del obj.value\n");
    (void)test_context.thread()->run(definition_code);
    Value function_value =
        definition_code->module_scope.extract()->get_by_name(clear_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(uint8_t(Bytecode::DelAttr), function_code->code[0]);
    ASSERT_TRUE(function_code->attribute_read_caches.empty());
    ASSERT_EQ(1u, function_code->attribute_mutation_caches.size());

    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, test_context.vm().object_class());
    Instance *first = test_context.thread()->make_internal_raw<Instance>(cls);
    ASSERT_TRUE(first->set_own_property(value_name, Value::from_smi(7)));

    CodeObject *call_code = test_context.compile_file(L"clear(obj)\n"
                                                      L"42\n");
    bind_global(test_context, call_code, L"clear", function_value);
    bind_global(test_context, call_code, L"obj", Value::from_oop(first));
    EXPECT_EQ(Value::from_smi(42), test_context.thread()->run(call_code));
    EXPECT_TRUE(first->get_own_property(value_name).is_not_present());

    ASSERT_EQ(1u, function_code->attribute_mutation_caches.size());
    const AttributeMutationInlineCache &cache =
        function_code->attribute_mutation_caches[0];
    ASSERT_NE(nullptr, cache.receiver_shape);
    EXPECT_TRUE(cache.plan.is_delete_own_property());
    ASSERT_NE(nullptr, cache.plan.next_shape);
    EXPECT_TRUE(cache.plan.storage_location().is_found());
    ASSERT_NE(nullptr, cache.plan.lookup_validity_cell);
    EXPECT_TRUE(cache.plan.lookup_validity_cell->is_valid());

    Instance *second = test_context.thread()->make_internal_raw<Instance>(cls);
    ASSERT_TRUE(second->set_own_property(value_name, Value::from_smi(8)));
    bind_global(test_context, call_code, L"obj", Value::from_oop(second));
    EXPECT_EQ(Value::from_smi(42), test_context.thread()->run(call_code));
    EXPECT_TRUE(second->get_own_property(value_name).is_not_present());
}

TEST(Interpreter, del_attr_missing_attribute_raises_attribute_error)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> clear_name(
        test_context.vm().get_or_create_interned_string_value(L"clear"));
    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));

    CodeObject *definition_code =
        test_context.compile_file(L"def clear(obj):\n"
                                  L"    del obj.value\n");
    (void)test_context.thread()->run(definition_code);
    Value function_value =
        definition_code->module_scope.extract()->get_by_name(clear_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));

    ClassObject *cls = test_context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, test_context.vm().object_class());
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(cls);
    CodeObject *call_code = test_context.compile_file(L"clear(obj)\n");
    bind_global(test_context, call_code, L"clear", function_value);
    bind_global(test_context, call_code, L"obj", Value::from_oop(obj));

    try
    {
        (void)test_context.thread()->run(call_code);
        FAIL() << "Expected AttributeError";
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ("AttributeError", err.what());
    }
}

TEST(Interpreter, cached_class_attribute_read_observes_class_write)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    value = 1\n"
                                 L"def get(obj):\n"
                                 L"    return obj.value\n"
                                 L"obj = Cls()\n"
                                 L"first = get(obj)\n"
                                 L"Cls.value = 2\n"
                                 L"get(obj)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(2), actual);
}

TEST(Interpreter, cached_class_chain_attribute_read_observes_mro_mutations)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> mid_name(
        test_context.vm().get_or_create_interned_string_value(L"Mid"));
    TValue<String> leaf_name(
        test_context.vm().get_or_create_interned_string_value(L"Leaf"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 4, test_context.vm().object_class());
    ClassObject *mid = test_context.thread()->make_internal_raw<ClassObject>(
        mid_name, 4, base);
    ClassObject *leaf = test_context.thread()->make_internal_raw<ClassObject>(
        leaf_name, 4, mid);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(leaf);

    ASSERT_TRUE(base->set_own_property(value_name, Value::from_smi(1)));

    CodeObject *read_code = test_context.compile_file(L"obj.value\n");
    bind_global(test_context, read_code, L"obj", Value::from_oop(obj));

    EXPECT_EQ(Value::from_smi(1), test_context.thread()->run(read_code));

    EXPECT_TRUE(base->set_own_property(value_name, Value::from_smi(2)));
    EXPECT_EQ(Value::from_smi(2), test_context.thread()->run(read_code));

    EXPECT_TRUE(mid->set_own_property(value_name, Value::from_smi(3)));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(read_code));

    EXPECT_TRUE(leaf->set_own_property(value_name, Value::from_smi(4)));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(read_code));

    EXPECT_TRUE(obj->set_own_property(value_name, Value::from_smi(5)));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(read_code));

    EXPECT_TRUE(base->set_own_property(value_name, Value::from_smi(6)));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(read_code));

    EXPECT_TRUE(obj->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(read_code));

    EXPECT_TRUE(leaf->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(read_code));

    EXPECT_TRUE(mid->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(6), test_context.thread()->run(read_code));
}

TEST(Interpreter, cached_class_chain_attribute_read_observes_secondary_base)
{
    test::FileRunner file_runner(L"class Left:\n"
                                 L"    pass\n"
                                 L"class Right:\n"
                                 L"    value = 1\n"
                                 L"class Derived(Left, Right):\n"
                                 L"    pass\n"
                                 L"def get(obj):\n"
                                 L"    return obj.value\n"
                                 L"obj = Derived()\n"
                                 L"first = get(obj)\n"
                                 L"Right.value = 2\n"
                                 L"get(obj)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(2), actual);
}

TEST(Interpreter, cached_direct_method_call_observes_mro_mutations)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> mid_name(
        test_context.vm().get_or_create_interned_string_value(L"Mid"));
    TValue<String> leaf_name(
        test_context.vm().get_or_create_interned_string_value(L"Leaf"));
    TValue<String> method_name(
        test_context.vm().get_or_create_interned_string_value(L"method"));

    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 4, test_context.vm().object_class());
    ClassObject *mid = test_context.thread()->make_internal_raw<ClassObject>(
        mid_name, 4, base);
    ClassObject *leaf = test_context.thread()->make_internal_raw<ClassObject>(
        leaf_name, 4, mid);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(leaf);

    Value base_method_1 = make_test_function(test_context, L"base_method_1",
                                             L"def base_method_1(self):\n"
                                             L"    return 1\n");
    Value base_method_2 = make_test_function(test_context, L"base_method_2",
                                             L"def base_method_2(self):\n"
                                             L"    return 2\n");
    Value mid_method = make_test_function(test_context, L"mid_method",
                                          L"def mid_method(self):\n"
                                          L"    return 3\n");
    Value leaf_method = make_test_function(test_context, L"leaf_method",
                                           L"def leaf_method(self):\n"
                                           L"    return 4\n");
    Value own_method = make_test_function(test_context, L"own_method",
                                          L"def own_method():\n"
                                          L"    return 5\n");

    ASSERT_TRUE(base->set_own_property(method_name, base_method_1));

    CodeObject *call_code = test_context.compile_file(L"obj.method()\n");
    bind_global(test_context, call_code, L"obj", Value::from_oop(obj));

    EXPECT_EQ(Value::from_smi(1), test_context.thread()->run(call_code));

    EXPECT_TRUE(base->set_own_property(method_name, base_method_2));
    EXPECT_EQ(Value::from_smi(2), test_context.thread()->run(call_code));

    EXPECT_TRUE(mid->set_own_property(method_name, mid_method));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(call_code));

    EXPECT_TRUE(leaf->set_own_property(method_name, leaf_method));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(call_code));

    EXPECT_TRUE(obj->set_own_property(method_name, own_method));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(call_code));

    EXPECT_TRUE(base->set_own_property(method_name, base_method_1));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(call_code));

    EXPECT_TRUE(obj->delete_own_property(method_name));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(call_code));

    EXPECT_TRUE(leaf->delete_own_property(method_name));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(call_code));

    EXPECT_TRUE(mid->delete_own_property(method_name));
    EXPECT_EQ(Value::from_smi(1), test_context.thread()->run(call_code));
}

TEST(Interpreter, cached_attribute_stores_invalidate_class_chain_reads)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> mid_name(
        test_context.vm().get_or_create_interned_string_value(L"Mid"));
    TValue<String> leaf_name(
        test_context.vm().get_or_create_interned_string_value(L"Leaf"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 4, test_context.vm().object_class());
    ClassObject *mid = test_context.thread()->make_internal_raw<ClassObject>(
        mid_name, 4, base);
    ClassObject *leaf = test_context.thread()->make_internal_raw<ClassObject>(
        leaf_name, 4, mid);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(leaf);

    ASSERT_TRUE(base->set_own_property(value_name, Value::from_smi(1)));

    CodeObject *read_code = test_context.compile_file(L"obj.value\n");
    bind_global(test_context, read_code, L"obj", Value::from_oop(obj));
    EXPECT_EQ(Value::from_smi(1), test_context.thread()->run(read_code));

    CodeObject *base_store_code =
        test_context.compile_file(L"Base.value = new_value\n"
                                  L"obj.value\n");
    bind_global(test_context, base_store_code, L"Base", Value::from_oop(base));
    bind_global(test_context, base_store_code, L"obj", Value::from_oop(obj));

    bind_global(test_context, base_store_code, L"new_value",
                Value::from_smi(2));
    EXPECT_EQ(Value::from_smi(2), test_context.thread()->run(base_store_code));
    EXPECT_EQ(Value::from_smi(2), test_context.thread()->run(read_code));

    CodeObject *mid_store_code =
        test_context.compile_file(L"Mid.value = new_value\n"
                                  L"obj.value\n");
    bind_global(test_context, mid_store_code, L"Mid", Value::from_oop(mid));
    bind_global(test_context, mid_store_code, L"obj", Value::from_oop(obj));

    bind_global(test_context, mid_store_code, L"new_value", Value::from_smi(3));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(mid_store_code));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(read_code));

    CodeObject *leaf_store_code =
        test_context.compile_file(L"Leaf.value = new_value\n"
                                  L"obj.value\n");
    bind_global(test_context, leaf_store_code, L"Leaf", Value::from_oop(leaf));
    bind_global(test_context, leaf_store_code, L"obj", Value::from_oop(obj));

    bind_global(test_context, leaf_store_code, L"new_value",
                Value::from_smi(4));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(leaf_store_code));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(read_code));

    CodeObject *self_store_code =
        test_context.compile_file(L"obj.value = new_value\n"
                                  L"obj.value\n");
    bind_global(test_context, self_store_code, L"obj", Value::from_oop(obj));

    bind_global(test_context, self_store_code, L"new_value",
                Value::from_smi(5));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(self_store_code));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(read_code));

    bind_global(test_context, base_store_code, L"new_value",
                Value::from_smi(6));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(base_store_code));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(read_code));

    EXPECT_TRUE(obj->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(read_code));
}

TEST(Interpreter, object_class_has_empty_bases_tuple)
{
    test::FileRunner file_runner(L"object.__bases__\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_TRUE(actual.get_ptr<Tuple>()->empty());
}

TEST(Interpreter, class_definition_rejects_non_class_base)
{
    expect_runtime_error(L"Base = 1\n"
                         L"class Derived(Base):\n"
                         L"    pass\n",
                         "TypeError: class bases must be class objects");
}

TEST(Interpreter, class_definition_rejects_duplicate_base)
{
    expect_runtime_error(L"class Base:\n"
                         L"    pass\n"
                         L"class Derived(Base, Base):\n"
                         L"    pass\n",
                         "TypeError: duplicate base class");
}

TEST(Interpreter, class_definition_rejects_inconsistent_c3_mro)
{
    expect_runtime_error(L"class X:\n"
                         L"    pass\n"
                         L"class Y:\n"
                         L"    pass\n"
                         L"class A(X, Y):\n"
                         L"    pass\n"
                         L"class B(Y, X):\n"
                         L"    pass\n"
                         L"class C(A, B):\n"
                         L"    pass\n",
                         "TypeError: cannot create a consistent method "
                         "resolution order");
}

TEST(Interpreter, class_call_rejects_arguments)
{
    expect_python_error(L"class Cls:\n"
                        L"    pass\n"
                        L"Cls(1)\n",
                        L"TypeError: wrong number of arguments");
}

TEST(Interpreter, return_in_class_body_is_rejected)
{
    expect_runtime_error(L"class Cls:\n"
                         L"    return 1\n",
                         "SyntaxError: 'return' outside function");
}

TEST(Interpreter, return_in_module_body_is_rejected)
{
    expect_runtime_error(L"return\n", "SyntaxError: 'return' outside function");
}

TEST(Interpreter, global_statement_makes_function_delete_global)
{
    expect_python_error(L"a = 10\n"
                        L"def f():\n"
                        L"    global a\n"
                        L"    del a\n"
                        L"f()\n"
                        L"a\n",
                        L"NameError: name 'a' is not defined");
}

TEST(Interpreter, global_statement_rejects_parameter_conflict)
{
    expect_runtime_error(L"def f(value):\n"
                         L"    global value\n",
                         "SyntaxError: name is parameter and global");
}

TEST(Interpreter, global_statement_rejects_prior_function_read)
{
    expect_runtime_error(
        L"def f():\n"
        L"    value\n"
        L"    global value\n",
        "SyntaxError: name is used prior to global declaration");
}

TEST(Interpreter, global_statement_rejects_prior_function_assignment)
{
    expect_runtime_error(
        L"def f():\n"
        L"    value = 1\n"
        L"    global value\n",
        "SyntaxError: name is assigned to before global declaration");
}

TEST(Interpreter, global_statement_rejects_prior_function_delete)
{
    expect_runtime_error(
        L"def f():\n"
        L"    del value\n"
        L"    global value\n",
        "SyntaxError: name is assigned to before global declaration");
}

TEST(Interpreter, module_global_statement_rejects_prior_read)
{
    expect_runtime_error(
        L"value\n"
        L"global value\n",
        "SyntaxError: name is used prior to global declaration");
}

TEST(Interpreter, module_global_statement_rejects_prior_assignment)
{
    expect_runtime_error(
        L"value = 1\n"
        L"global value\n",
        "SyntaxError: name is assigned to before global declaration");
}

TEST(Interpreter, global_statement_rejects_annotated_name)
{
    expect_runtime_error(L"def f():\n"
                         L"    global value\n"
                         L"    value: int\n",
                         "SyntaxError: annotated name can't be global");
}

TEST(Interpreter, name_error)
{
    expect_python_error(L"missing_name\n",
                        L"NameError: name 'missing_name' is not defined");
}

TEST(Interpreter, call_non_callable)
{
    expect_python_error(L"1()\n", L"TypeError: object is not callable");
}

TEST(Interpreter, call_non_callable_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    1()\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError: object is not callable");
}

TEST(Interpreter, call_native_zero_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_zero()\n");

    bind_global(test_context, code_obj, L"native_zero",
                make_native_function(&test_context.vm(), native_zero));

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
}

TEST(Interpreter, native_function_thunk_uses_return_or_raise_adapter)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<Function> native =
        make_native_function(&test_context.vm(), native_zero);

    std::string actual =
        fmt::to_string(*native.extract()->code_object.extract());
    std::string expected = "Code object:\n"
                           "    0 CallNative0 0\n"
                           "    2 ReturnOrRaiseException\n";
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, call_native_one_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_increment(41)\n");

    bind_global(test_context, code_obj, L"native_increment",
                make_native_function(&test_context.vm(), native_increment));

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, call_native_two_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_add(20, 22)\n");

    bind_global(test_context, code_obj, L"native_add",
                make_native_function(&test_context.vm(), native_add));

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, native_exception_marker_materializes_stop_iteration)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_stop()\n");

    bind_global(test_context, code_obj, L"native_stop",
                make_native_function(&test_context.vm(),
                                     native_stop_iteration_with_value));

    try
    {
        (void)test_context.thread()->run(code_obj);
        FAIL() << "Expected unhandled pending exception";
    }
    catch(const PythonException &err)
    {
        EXPECT_STREQ(L"StopIteration", err.wide_what().c_str());
        EXPECT_STREQ("StopIteration", err.what());
    }

    ASSERT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    TValue<StopIterationObject> exception =
        TValue<StopIterationObject>::from_value_checked(
            test_context.thread()->pending_exception_object());
    EXPECT_EQ(Value::from_smi(123), exception.extract()->value.as_value());
}

TEST(Interpreter, native_exception_marker_unwinds_nested_frames)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"def call_stop():\n"
                                                     L"    native_stop()\n"
                                                     L"    return 99\n"
                                                     L"call_stop()\n");

    bind_global(test_context, code_obj, L"native_stop",
                make_native_function(&test_context.vm(),
                                     native_stop_iteration_with_value));

    try
    {
        (void)test_context.thread()->run(code_obj);
        FAIL() << "Expected unhandled pending exception";
    }
    catch(const PythonException &err)
    {
        EXPECT_STREQ(L"StopIteration", err.wide_what().c_str());
        EXPECT_STREQ("StopIteration", err.what());
    }

    ASSERT_EQ(PendingExceptionKind::Object,
              test_context.thread()->pending_exception_kind());
    TValue<StopIterationObject> exception =
        TValue<StopIterationObject>::from_value_checked(
            test_context.thread()->pending_exception_object());
    EXPECT_EQ(Value::from_smi(123), exception.extract()->value.as_value());
}

TEST(Interpreter, unhandled_python_exception_reports_class_and_message)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_boom()\n");

    bind_global(test_context, code_obj, L"native_boom",
                make_native_function(&test_context.vm(),
                                     native_base_exception_with_message));

    try
    {
        (void)test_context.thread()->run(code_obj);
        FAIL() << "Expected unhandled pending exception";
    }
    catch(const PythonException &err)
    {
        EXPECT_STREQ(L"BaseException: boom", err.wide_what().c_str());
        EXPECT_STREQ("BaseException: boom", err.what());
    }
}

TEST(Interpreter, native_exception_marker_requires_pending_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_broken()\n");

    bind_global(test_context, code_obj, L"native_broken",
                make_native_function(&test_context.vm(),
                                     native_marker_without_pending_exception));

    try
    {
        (void)test_context.thread()->run(code_obj);
        FAIL() << "Expected internal exception-marker error";
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ(
            "InternalError: exception marker without pending exception",
            err.what());
    }
}

TEST(Interpreter, raise_unwind_raises_exception_class)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Value exception_class = Value::from_oop(
        test_context.thread()->class_for_builtin_name(L"Exception"));
    CodeObject *code_obj =
        make_raise_unwind_code(test_context, exception_class);

    try
    {
        (void)test_context.thread()->run(code_obj);
        FAIL() << "Expected PythonException";
    }
    catch(const PythonException &err)
    {
        EXPECT_STREQ(L"Exception", err.wide_what().c_str());
    }
}

TEST(Interpreter, raise_unwind_raises_exception_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<ExceptionObject> exception = make_exception_object(
        TValue<ClassObject>::from_oop(
            test_context.thread()->class_for_builtin_name(L"ValueError")),
        L"boom");
    CodeObject *code_obj =
        make_raise_unwind_code(test_context, exception.as_value());

    try
    {
        (void)test_context.thread()->run(code_obj);
        FAIL() << "Expected PythonException";
    }
    catch(const PythonException &err)
    {
        EXPECT_STREQ(L"ValueError: boom", err.wide_what().c_str());
    }
}

TEST(Interpreter, raise_unwind_rejects_non_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        make_raise_unwind_code(test_context, Value::from_smi(1));

    try
    {
        (void)test_context.thread()->run(code_obj);
        FAIL() << "Expected PythonException";
    }
    catch(const PythonException &err)
    {
        EXPECT_STREQ(L"TypeError: exceptions must derive from BaseException",
                     err.wide_what().c_str());
    }
}

TEST(Interpreter, builtin_scope_lookup)
{
    test::VmTestContext test_context;
    CodeObject *code_obj = test_context.compile_file(L"range\n");

    Scope *module_scope = code_obj->module_scope.extract();
    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"range");
    int32_t slot_idx = module_scope->lookup_slot_index_local(name);
    ASSERT_GE(slot_idx, 0);

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(NativeLayoutId::Function,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_EQ(actual, module_scope->get_by_slot_index_fastpath_only(slot_idx));
}

TEST(Interpreter, builtin_type_classes_are_vm_roots_and_builtins)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    struct ExpectedBuiltinType
    {
        NativeLayoutId native_layout_id;
        const wchar_t *name;
    };

    constexpr ExpectedBuiltinType expected_types[] = {
        {NativeLayoutId::ClassObject, L"type"},
        {NativeLayoutId::String, L"str"},
        {NativeLayoutId::List, L"list"},
        {NativeLayoutId::Tuple, L"tuple"},
        {NativeLayoutId::Dict, L"dict"},
        {NativeLayoutId::Function, L"function"},
        {NativeLayoutId::CodeObject, L"code"},
        {NativeLayoutId::RangeIterator, L"range_iterator"},
        {NativeLayoutId::Instance, L"object"},
    };

    Scope *builtins = test_context.vm().get_builtin_scope().extract();
    ClassObject *type_class = test_context.vm().type_class();
    ASSERT_NE(nullptr, type_class);

    for(const ExpectedBuiltinType &expected: expected_types)
    {
        ClassObject *cls = test_context.vm().class_for_native_layout(
            expected.native_layout_id);
        ASSERT_NE(nullptr, cls);
        EXPECT_EQ(type_class, cls->Object::get_class().extract());
        EXPECT_EQ(-1, cls->refcount);
        EXPECT_TRUE(cls->get_shape()->has_flag(ShapeFlag::IsClassObject));
        EXPECT_TRUE(cls->get_shape()->has_flag(ShapeFlag::IsImmutableType));
        EXPECT_EQ(Value::from_oop(type_class),
                  cls->get_own_property(
                      test_context.vm().get_or_create_interned_string_value(
                          L"__class__")));

        TValue<String> name =
            test_context.vm().get_or_create_interned_string_value(
                expected.name);
        EXPECT_EQ(name, cls->get_name());
        EXPECT_EQ(test_context.vm().str_class(),
                  name.extract()->Object::get_class().extract());
        EXPECT_EQ(test_context.vm().str_instance_root_shape(),
                  name.extract()
                      ->Object::get_class()
                      .extract()
                      ->get_instance_root_shape());

        TValue<String> dunder_bases_name =
            test_context.vm().get_or_create_interned_string_value(L"__bases__");
        TValue<String> dunder_mro_name =
            test_context.vm().get_or_create_interned_string_value(L"__mro__");
        Value bases_value = cls->get_own_property(dunder_bases_name);
        Value mro_value = cls->get_own_property(dunder_mro_name);
        ASSERT_TRUE(can_convert_to<Tuple>(bases_value));
        ASSERT_TRUE(can_convert_to<Tuple>(mro_value));
        EXPECT_EQ(test_context.vm().tuple_class(),
                  bases_value.get_ptr<Object>()->get_class().extract());
        EXPECT_EQ(test_context.vm().tuple_class(),
                  mro_value.get_ptr<Object>()->get_class().extract());
        Tuple *bases = bases_value.get_ptr<Tuple>();
        Tuple *mro = mro_value.get_ptr<Tuple>();
        if(expected.native_layout_id == NativeLayoutId::Instance)
        {
            EXPECT_EQ(0u, bases->size());
            ASSERT_EQ(1u, mro->size());
            EXPECT_EQ(Value::from_oop(cls), mro->item_unchecked(0));
        }
        else
        {
            ASSERT_EQ(1u, bases->size());
            EXPECT_EQ(Value::from_oop(test_context.vm().object_class()),
                      bases->item_unchecked(0));
            ASSERT_EQ(2u, mro->size());
            EXPECT_EQ(Value::from_oop(cls), mro->item_unchecked(0));
            EXPECT_EQ(Value::from_oop(test_context.vm().object_class()),
                      mro->item_unchecked(1));
        }

        if(expected.native_layout_id == NativeLayoutId::CodeObject)
        {
            EXPECT_EQ(Value::not_present(), builtins->get_by_name(name));
        }
        else
        {
            EXPECT_EQ(Value::from_oop(cls), builtins->get_by_name(name));
        }
    }

    TValue<String> post_bootstrap_name =
        test_context.vm().get_or_create_interned_string_value(
            L"post_bootstrap_name");
    EXPECT_EQ(test_context.vm().str_class(),
              post_bootstrap_name.extract()->Object::get_class().extract());
    EXPECT_EQ(test_context.vm().str_instance_root_shape(),
              post_bootstrap_name.extract()
                  ->Object::get_class()
                  .extract()
                  ->get_instance_root_shape());

    EXPECT_EQ(Value::from_oop(type_class),
              type_class->get_own_property(
                  test_context.vm().get_or_create_interned_string_value(
                      L"__class__")));

    ClassObject *str_class = test_context.vm().str_class();
    TValue<String> dunder_str_name =
        test_context.vm().get_or_create_interned_string_value(L"__str__");
    TValue<String> dunder_add_name =
        test_context.vm().get_or_create_interned_string_value(L"__add__");
    Value str_method = str_class->get_own_property(dunder_str_name);
    Value add_method = str_class->get_own_property(dunder_add_name);
    ASSERT_TRUE(can_convert_to<Function>(str_method));
    ASSERT_TRUE(can_convert_to<Function>(add_method));
    EXPECT_EQ(-1, str_method.get_ptr<Object>()->refcount);
    EXPECT_EQ(-1, add_method.get_ptr<Object>()->refcount);
    EXPECT_FALSE(
        str_class->set_own_property(dunder_str_name, Value::from_smi(99)));
}

TEST(Interpreter, range_builtin_returns_range_iterator)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"range(5)\n");

    expect_range_iterator(actual, 0, 5, 1);
}

TEST(Interpreter, range_builtin_two_arguments_returns_range_iterator)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"range(2, 5)\n");

    expect_range_iterator(actual, 2, 5, 1);
}

TEST(Interpreter, range_builtin_three_arguments_returns_range_iterator)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"range(2, 9, 3)\n");

    expect_range_iterator(actual, 2, 9, 3);
}

TEST(Interpreter, direct_range_for_loop_reports_integer_argument_errors)
{
    expect_python_error(L"for x in range(False):\n"
                        L"    0\n",
                        L"TypeError: range() arguments must be integers");
}

TEST(Interpreter, direct_range_for_loop_reports_zero_step_errors)
{
    expect_python_error(L"for x in range(1, 4, 0):\n"
                        L"    0\n",
                        L"ValueError: range() arg 3 must not be zero");
}

TEST(Interpreter, global_delete_does_not_delete_builtin_fallback)
{
    expect_python_error(L"range\n"
                        L"del range\n",
                        L"NameError: name 'range' is not defined");
}

TEST(Interpreter, global_delete_reveals_builtin_after_shadow_delete)
{
    test::FileRunner file_runner(L"range = 42\n"
                                 "del range\n"
                                 "range(1)\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::RangeIterator,
              actual.get_ptr<Object>()->native_layout_id());
}

TEST(Interpreter, range_builtin_requires_integer_argument)
{
    expect_python_error(L"range(False)\n",
                        L"TypeError: range() arguments must be integers");
    expect_python_error(L"range(1, False)\n",
                        L"TypeError: range() arguments must be integers");
    expect_python_error(L"range(1, 2, False)\n",
                        L"TypeError: range() arguments must be integers");
}

TEST(Interpreter, range_integer_argument_error_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    range(False)\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError: range() arguments must be integers");
}

TEST(Interpreter, range_builtin_rejects_zero_step)
{
    expect_python_error(L"range(1, 2, 0)\n",
                        L"ValueError: range() arg 3 must not be zero");
}

TEST(Interpreter, range_zero_step_error_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    range(1, 2, 0)\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"ValueError: range() arg 3 must not be zero");
}

TEST(Interpreter, range_builtin_rejects_wrong_arity_at_function_boundary)
{
    expect_python_error(L"range()\n", L"TypeError: wrong number of arguments");
    expect_python_error(L"range(1, 2, 3, 4)\n",
                        L"TypeError: wrong number of arguments");
}

TEST(Interpreter, for_loop_rejects_non_iterable)
{
    expect_python_error(L"for x in 1:\n"
                        L"    x\n",
                        L"TypeError: object is not iterable");
}

TEST(Interpreter, for_loop_non_iterable_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    for x in 1:\n"
                        L"        x\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"TypeError: object is not iterable");
}

TEST(Interpreter, left_shift_negative_count)
{
    expect_python_error(L"a = 1\n"
                        L"b = -1\n"
                        L"a << b\n",
                        L"ValueError: negative shift count");
}

TEST(Interpreter, right_shift_negative_count)
{
    expect_python_error(L"a = 8\n"
                        L"b = -1\n"
                        L"a >> b\n",
                        L"ValueError: negative shift count");
}

TEST(Interpreter, negative_shift_count_unwinds_nested_frames)
{
    expect_python_error(L"def fail():\n"
                        L"    1 << -1\n"
                        L"    return 99\n"
                        L"fail()\n",
                        L"ValueError: negative shift count");
}

TEST(Interpreter, left_shift_overflow_smi)
{
    expect_runtime_error(L"1 << 58\n", "Clovervm exception");
}

TEST(Interpreter, left_shift_overflow_register)
{
    expect_runtime_error(L"a = 1\n"
                         L"b = 58\n"
                         L"a << b\n",
                         "Clovervm exception");
}

TEST(Interpreter, add_overflow)
{
    expect_runtime_error(L"288230376151711743 + 1\n", "Clovervm exception");
}

TEST(Interpreter, subtract_overflow)
{
    expect_runtime_error(L"-288230376151711743 - 2\n", "Clovervm exception");
}

TEST(Interpreter, multiply_overflow)
{
    expect_runtime_error(L"288230376151711743 * 2\n", "Clovervm exception");
}

TEST(Interpreter, negate_overflow)
{
    Value expected = Value::from_smi(kMinSmi);
    test::FileRunner file_runner(L"-288230376151711743 - 1\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);

    expect_runtime_error(L"x = -288230376151711743 - 1\n"
                         L"-x\n",
                         "Clovervm exception");
}
