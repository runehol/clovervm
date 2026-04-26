#include "builtin_function.h"
#include "class_object.h"
#include "codegen.h"
#include "compilation_unit.h"
#include "dict.h"
#include "instance.h"
#include "interpreter.h"
#include "list.h"
#include "parser.h"
#include "range_iterator.h"
#include "scope.h"
#include "shape.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "token_print.h"
#include "tokenizer.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <stdexcept>

using namespace cl;

static constexpr int64_t kMinSmi = -288230376151711744LL;

static Value builtin_add(ThreadState *, const CallArguments &args)
{
    if(args.n_args != 2 || !args[0].is_smi() || !args[1].is_smi())
    {
        throw std::runtime_error("builtin_add received unexpected arguments");
    }
    return Value::from_smi(args[0].get_smi() + args[1].get_smi());
}

static Value builtin_sum(ThreadState *, const CallArguments &args)
{
    int64_t total = 0;
    for(uint32_t i = 0; i < args.n_args; ++i)
    {
        if(!args[i].is_smi())
        {
            throw std::runtime_error(
                "builtin_sum received unexpected arguments");
        }
        total += args[i].get_smi();
    }
    return Value::from_smi(total);
}

static Value builtin_identity(ThreadState *, const CallArguments &args)
{
    if(args.n_args != 1)
    {
        throw std::runtime_error("builtin_identity expected exactly one arg");
    }
    return args[0];
}

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

static Value builtin_next_counter(ThreadState *, const CallArguments &args)
{
    if(args.n_args != 0)
    {
        throw std::runtime_error("builtin_next_counter expected no arguments");
    }
    return Value::from_smi(g_next_counter++);
}

TEST(Interpreter, simple)
{
    Value expected = Value::from_smi(15);
    test::FileRunner file_runner(L"1 + 2  *  (4 + 3)");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple2)
{
    Value expected = Value::from_smi(19);
    test::FileRunner file_runner(L"(1 << 4) + 3");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple3)
{
    Value expected = Value::False();
    test::FileRunner file_runner(L"not True");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple4)
{
    Value expected = Value::from_smi(-13);
    test::FileRunner file_runner(L"1 - 2  *  (4 + 3)");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, assignment1)
{
    Value expected = Value::from_smi(7);
    test::FileRunner file_runner(L"a = 4\n"
                                 "a + 3");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, assignment2)
{
    Value expected = Value::from_smi(11);
    test::FileRunner file_runner(L"a = 4\n"
                                 "a += 7\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, while1)
{
    Value expected = Value::from_smi(4950);
    test::FileRunner file_runner(L"b = 0\n"
                                 "a = 100\n"
                                 "while a:\n"
                                 "    a -= 1\n"
                                 "    b += a\n"
                                 "b\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, if_elif_branch)
{
    Value expected = Value::from_smi(2);
    test::FileRunner file_runner(L"a = False\n"
                                 "b = True\n"
                                 "if a:\n"
                                 "    1\n"
                                 "elif b:\n"
                                 "    2\n"
                                 "else:\n"
                                 "    3\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, if_else_branch)
{
    Value expected = Value::from_smi(3);
    test::FileRunner file_runner(L"a = False\n"
                                 "b = False\n"
                                 "if a:\n"
                                 "    1\n"
                                 "elif b:\n"
                                 "    2\n"
                                 "else:\n"
                                 "    3\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, while_else_runs_after_normal_exit)
{
    Value expected = Value::from_smi(7);
    test::FileRunner file_runner(L"a = 2\n"
                                 "b = 0\n"
                                 "while a:\n"
                                 "    a -= 1\n"
                                 "else:\n"
                                 "    b = 7\n"
                                 "b\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, while_else_skipped_after_break)
{
    Value expected = Value::from_smi(0);
    test::FileRunner file_runner(L"a = 2\n"
                                 "b = 0\n"
                                 "while a:\n"
                                 "    break\n"
                                 "else:\n"
                                 "    b = 7\n"
                                 "b\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_multiple_parameters)
{
    Value expected = Value::from_smi(6);
    test::FileRunner file_runner(L"def add3(a, b, c):\n"
                                 "    return a + b + c\n"
                                 "add3(1, 2, 3)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_implicit_return_none)
{
    Value expected = Value::None();
    test::FileRunner file_runner(L"def f():\n"
                                 "    a = 1\n"
                                 "f()\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
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
    EXPECT_EQ(Value::from_smi(7), cls->lookup_class_chain(value_name));

    Shape *shape = cls->get_shape();
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    StorageLocation value_location =
        shape->resolve_present_property(value_name);
    ASSERT_TRUE(value_location.is_found());
    EXPECT_EQ(StorageKind::Inline, value_location.kind);
    EXPECT_EQ(int32_t(ClassObject::kClassMetadataSlotCount),
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
        EXPECT_EQ(int32_t(ClassObject::kClassMetadataSlotCount + idx),
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

TEST(Interpreter, class_call_allocates_instance_with_initial_class_slot)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.__class__ is Cls\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::True(), actual);
}

TEST(Interpreter, class_body_can_read_earlier_class_binding)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    x = 1\n"
                                 L"    y = x + 2\n"
                                 L"Cls.y\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(Interpreter, class_definition_inside_function_preserves_caller_frame)
{
    test::FileRunner file_runner(L"def outer(seed):\n"
                                 L"    a = seed + 1\n"
                                 L"    b = seed + 2\n"
                                 L"    c = (a + b) * (seed + 3)\n"
                                 L"    class Cls:\n"
                                 L"        x = 11\n"
                                 L"        y = x + 13\n"
                                 L"    d = (a + b) * (c + Cls.y)\n"
                                 L"    return d + a + b + c\n"
                                 L"outer(4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(1199), actual);
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

TEST(Interpreter, class_method_call_works_from_source)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method(self, x):\n"
                                 L"        return self.value + x\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 3\n"
                                 L"obj.method(4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, string_literal_value)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"\"abc\"\n");

    EXPECT_STREQ(L"abc", string_as_wchar_t(TValue<String>(actual)));
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
    Value builtin = test_context.thread()->make_object_value<BuiltinFunction>(
        builtin_next_counter, 0, 0);
    code_obj->module_scope.extract()->set_by_name(name, builtin);

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

TEST(Interpreter, subscript_load_reads_list_item)
{
    test::FileRunner file_runner(L"xs = [4, 7, 9]\n"
                                 L"xs[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
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

TEST(Interpreter, subscript_load_reads_dict_item)
{
    test::FileRunner file_runner(L"key = \"alpha\"\n"
                                 L"xs = {key: 4, \"beta\": 7}\n"
                                 L"xs[key]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(4), actual);
}

TEST(Interpreter, subscript_store_writes_dict_item)
{
    test::FileRunner file_runner(L"xs = {\"alpha\": 4, \"beta\": 7}\n"
                                 L"xs[\"beta\"] = 11\n"
                                 L"xs[\"beta\"]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(11), actual);
}

TEST(Interpreter, subscript_augmented_assignment_updates_dict_item)
{
    test::FileRunner file_runner(L"xs = {\"alpha\": 4, \"beta\": 7}\n"
                                 L"xs[\"beta\"] += 5\n"
                                 L"xs[\"beta\"]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
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
    Value builtin = test_context.thread()->make_object_value<BuiltinFunction>(
        builtin_next_counter, 0, 0);
    code_obj->module_scope.extract()->set_by_name(name, builtin);

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
    EXPECT_EQ(1, g_next_counter);
}

TEST(Interpreter, subscript_store_writes_list_item)
{
    test::FileRunner file_runner(L"xs = [4, 7, 9]\n"
                                 L"xs[1] = 11\n"
                                 L"xs[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(11), actual);
}

TEST(Interpreter, subscript_augmented_assignment_updates_list_item)
{
    test::FileRunner file_runner(L"xs = [4, 7, 9]\n"
                                 L"xs[1] += 5\n"
                                 L"xs[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
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
    Value builtin = test_context.thread()->make_object_value<BuiltinFunction>(
        builtin_next_counter, 0, 0);
    code_obj->module_scope.extract()->set_by_name(name, builtin);

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
    EXPECT_EQ(1, g_next_counter);
}

TEST(Interpreter, subscript_load_rejects_non_integer_list_index)
{
    expect_runtime_error(L"xs = [1, 2, 3]\n"
                         L"xs[False]\n",
                         "TypeError: list indices must be integers");
}

TEST(Interpreter, subscript_load_rejects_non_subscriptable_receiver)
{
    expect_runtime_error(L"1[0]\n", "TypeError: object is not subscriptable");
}

TEST(Interpreter, attribute_load_through_list_subscript)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 7\n"
                                 L"lst = [obj]\n"
                                 L"lst[0].value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, attribute_store_through_list_subscript)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 7\n"
                                 L"lst = [obj]\n"
                                 L"lst[0].value = 11\n"
                                 L"obj.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(11), actual);
}

TEST(Interpreter, attribute_augmented_assignment_through_list_subscript)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 7\n"
                                 L"lst = [obj]\n"
                                 L"lst[0].value += 5\n"
                                 L"obj.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(Interpreter, subscript_load_through_object_attribute)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.lst = [4, 7, 9]\n"
                                 L"obj.lst[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, subscript_store_through_object_attribute)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.lst = [4, 7, 9]\n"
                                 L"obj.lst[1] = 11\n"
                                 L"obj.lst[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(11), actual);
}

TEST(Interpreter, subscript_augmented_assignment_through_object_attribute)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.lst = [4, 7, 9]\n"
                                 L"obj.lst[1] += 5\n"
                                 L"obj.lst[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
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

TEST(Interpreter, direct_method_call_inserts_self_for_class_functions)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method(self, x):\n"
                                 L"        return self.value + x\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 3\n"
                                 L"obj.method(4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, direct_method_call_on_class_does_not_insert_self)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method(x):\n"
                                 L"        return x + 3\n"
                                 L"Cls.method(4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, class_base_lists_are_rejected_in_codegen)
{
    expect_runtime_error(L"class Derived(Base):\n"
                         L"    pass\n",
                         "Class base lists are not supported yet");
}

TEST(Interpreter, class_call_rejects_arguments)
{
    expect_runtime_error(L"class Cls:\n"
                         L"    pass\n"
                         L"Cls(1)\n",
                         "TypeError: wrong number of arguments");
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

TEST(Interpreter,
     direct_method_call_does_not_insert_self_for_non_function_callables)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> method_name(
        test_context.vm().get_or_create_interned_string_value(L"method"));

    TValue<BuiltinFunction> identity =
        test_context.thread()->make_object_value<BuiltinFunction>(
            builtin_identity, 1, 1);

    CodeObject *setup_code = test_context.compile_file(L"class Cls:\n"
                                                       L"    pass\n"
                                                       L"obj = Cls()\n");
    (void)test_context.thread()->run(setup_code);
    Scope *module_scope = setup_code->module_scope.extract();
    Value cls_value = module_scope->get_by_name(cls_name);
    ASSERT_TRUE(cls_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              cls_value.get_ptr<Object>()->native_layout_id());
    cls_value.get_ptr<ClassObject>()->set_own_property(method_name, identity);

    CodeObject *code_obj = test_context.compile_file(L"obj.method(4)\n");
    code_obj->module_scope = module_scope;

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(4), actual);
}

TEST(Interpreter, function_local_shadows_global)
{
    Value expected = Value::from_smi(11);
    test::FileRunner file_runner(L"a = 10\n"
                                 "def f():\n"
                                 "    a = 1\n"
                                 "    return a\n"
                                 "f() + a\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_reads_global_when_not_shadowed)
{
    Value expected = Value::from_smi(10);
    test::FileRunner file_runner(L"a = 10\n"
                                 "def f():\n"
                                 "    return a\n"
                                 "f()\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_nested_control_flow)
{
    Value expected = Value::from_smi(2);
    test::FileRunner file_runner(L"def pick(n):\n"
                                 "    if n:\n"
                                 "        while n:\n"
                                 "            return 1\n"
                                 "    else:\n"
                                 "        return 2\n"
                                 "pick(0)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, recursive_fibonacci)
{
    Value expected = Value::from_smi(10946);
    test::FileRunner file_runner(L"def fib(n):\n"
                                 "    if n <= 2:\n"
                                 "        return n\n"
                                 "    return fib(n-2) + fib(n-1)\n"
                                 "\n"
                                 "fib(20)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, name_error)
{
    expect_runtime_error(L"missing_name\n", "NameError");
}

TEST(Interpreter, call_non_callable)
{
    expect_runtime_error(L"1()\n", "TypeError: object is not callable");
}

TEST(Interpreter, call_builtin_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_add(4, 7)\n");

    Scope *module_scope = code_obj->module_scope.extract();
    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"native_add");
    Value builtin = test_context.thread()->make_object_value<BuiltinFunction>(
        builtin_add, 2, 2);
    module_scope->set_by_name(name, builtin);

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(11), actual);
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
    EXPECT_EQ(NativeLayoutId::BuiltinFunction,
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
        {NativeLayoutId::Dict, L"dict"},
        {NativeLayoutId::Function, L"function"},
        {NativeLayoutId::BuiltinFunction, L"builtin_function"},
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
        ASSERT_TRUE(can_convert_to<List>(bases_value));
        ASSERT_TRUE(can_convert_to<List>(mro_value));
        EXPECT_EQ(test_context.vm().list_class(),
                  bases_value.get_ptr<Object>()->get_class().extract());
        EXPECT_EQ(test_context.vm().list_class(),
                  mro_value.get_ptr<Object>()->get_class().extract());

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
    ASSERT_TRUE(can_convert_to<BuiltinFunction>(str_method));
    ASSERT_TRUE(can_convert_to<BuiltinFunction>(add_method));
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

TEST(Interpreter, for_loop_iterates_range)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(5):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(10), actual);
}

TEST(Interpreter, for_loop_iterates_two_argument_range)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(1, 4):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(6), actual);
}

TEST(Interpreter, for_loop_iterates_positive_step_range)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(1, 8, 3):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(Interpreter, for_loop_iterates_negative_step_range)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(5, -1, -2):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(9), actual);
}

TEST(Interpreter, direct_range_for_loop_reports_integer_argument_errors)
{
    expect_runtime_error(L"for x in range(False):\n"
                         L"    0\n",
                         "TypeError: range() arguments must be integers");
}

TEST(Interpreter, direct_range_for_loop_reports_zero_step_errors)
{
    expect_runtime_error(L"for x in range(1, 4, 0):\n"
                         L"    0\n",
                         "ValueError: range() arg 3 must not be zero");
}

TEST(Interpreter, for_else_runs_after_normal_exhaustion)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(3):\n"
                                 "    total += x\n"
                                 "else:\n"
                                 "    total += 10\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(13), actual);
}

TEST(Interpreter, for_else_skipped_after_break)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(5):\n"
                                 "    if x == 3:\n"
                                 "        break\n"
                                 "    total += x\n"
                                 "else:\n"
                                 "    total += 100\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(Interpreter, for_continue_jumps_to_next_iteration)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(5):\n"
                                 "    if x == 2:\n"
                                 "        continue\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(8), actual);
}

TEST(Interpreter, nested_for_loops_execute_correctly)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(3):\n"
                                 "    for y in range(2):\n"
                                 "        total += x + y\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(9), actual);
}

TEST(Interpreter, for_loop_executes_inside_function)
{
    test::FileRunner file_runner(L"def sum_range(n):\n"
                                 "    total = 0\n"
                                 "    for x in range(n):\n"
                                 "        total += x\n"
                                 "    return total\n"
                                 "sum_range(5)\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(10), actual);
}

TEST(Interpreter, nested_for_loops_execute_inside_function)
{
    test::FileRunner file_runner(L"def sum_pairs(n):\n"
                                 "    total = 0\n"
                                 "    for x in range(n):\n"
                                 "        for y in range(2):\n"
                                 "            total += x + y\n"
                                 "    return total\n"
                                 "sum_pairs(3)\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(9), actual);
}

TEST(Interpreter, shadowed_range_for_loop_uses_generic_fallback)
{
    test::FileRunner file_runner(L"real_range = range\n"
                                 "def range(n):\n"
                                 "    return real_range(1, n)\n"
                                 "total = 0\n"
                                 "for x in range(4):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(6), actual);
}

TEST(Interpreter, module_scope_can_shadow_builtin_scope)
{
    test::FileRunner file_runner(L"range = 42\n"
                                 "range\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, builtin_wrong_arity)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_add(4)\n");

    Scope *module_scope = code_obj->module_scope.extract();
    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"native_add");
    Value builtin = test_context.thread()->make_object_value<BuiltinFunction>(
        builtin_add, 2, 2);
    module_scope->set_by_name(name, builtin);

    try
    {
        (void)test_context.thread()->run(code_obj);
        FAIL() << "Expected std::runtime_error with message: "
               << "TypeError: wrong number of arguments";
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ("TypeError: wrong number of arguments", err.what());
    }
}

TEST(Interpreter, range_builtin_requires_integer_argument)
{
    expect_runtime_error(L"range(False)\n",
                         "TypeError: range() arguments must be integers");
    expect_runtime_error(L"range(1, False)\n",
                         "TypeError: range() arguments must be integers");
    expect_runtime_error(L"range(1, 2, False)\n",
                         "TypeError: range() arguments must be integers");
}

TEST(Interpreter, range_builtin_rejects_zero_step)
{
    expect_runtime_error(L"range(1, 2, 0)\n",
                         "ValueError: range() arg 3 must not be zero");
}

TEST(Interpreter, for_loop_rejects_non_iterable)
{
    expect_runtime_error(L"for x in 1:\n"
                         L"    x\n",
                         "TypeError: object is not iterable");
}

TEST(Interpreter, builtin_multiple_arities)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    Scope *module_scope = nullptr;
    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"native_sum");
    Value builtin = test_context.thread()->make_object_value<BuiltinFunction>(
        builtin_sum, 1, 3);

    CodeObject *one_arg = test_context.compile_file(L"native_sum(4)\n");
    module_scope = one_arg->module_scope.extract();
    module_scope->set_by_name(name, builtin);
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(one_arg));

    CodeObject *three_args =
        test_context.compile_file(L"native_sum(4, 5, 6)\n");
    module_scope = three_args->module_scope.extract();
    module_scope->set_by_name(name, builtin);
    EXPECT_EQ(Value::from_smi(15), test_context.thread()->run(three_args));
}

TEST(Interpreter, builtin_varargs)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"native_sum");
    Value builtin = test_context.thread()->make_object_value<BuiltinFunction>(
        builtin_sum, 0, BuiltinFunction::VarArgs);

    CodeObject *zero_args = test_context.compile_file(L"native_sum()\n");
    zero_args->module_scope.extract()->set_by_name(name, builtin);
    EXPECT_EQ(Value::from_smi(0), test_context.thread()->run(zero_args));

    CodeObject *four_args =
        test_context.compile_file(L"native_sum(1, 2, 3, 4)\n");
    four_args->module_scope.extract()->set_by_name(name, builtin);
    EXPECT_EQ(Value::from_smi(10), test_context.thread()->run(four_args));
}

TEST(Interpreter, negate_expression)
{
    Value expected = Value::from_smi(-7);
    test::FileRunner file_runner(L"-(3 + 4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, left_shift_negative_count)
{
    expect_runtime_error(L"a = 1\n"
                         L"b = -1\n"
                         L"a << b\n",
                         "ValueError: negative shift count");
}

TEST(Interpreter, right_shift_negative_count)
{
    expect_runtime_error(L"a = 8\n"
                         L"b = -1\n"
                         L"a >> b\n",
                         "ValueError: negative shift count");
}

TEST(Interpreter, left_shift_boundary)
{
    Value expected = Value::from_smi(kMinSmi);
    test::FileRunner file_runner(L"(-1) << 58\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
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

TEST(Interpreter, right_shift_negative_value)
{
    Value expected = Value::from_smi(-5);
    test::FileRunner file_runner(L"(-9) >> 1\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
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

/*
TEST(Interpreter, small_bench)
{
    const wchar_t *test_case =
        L"counter = 100000001\n"
        "acc = 1245\n"
        "while counter:\n"
        "    acc = (-acc*64 + 64)>>6\n"
        "    counter -= 1\n"
        "acc\n";

    Value expected = Value::from_smi(-1244);
    test::FileRunner file_runner(test_case);
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}
*/
