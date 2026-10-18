#include "builtin_types/list.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "compiler/codegen.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
#include <gtest/gtest.h>
#include <stdexcept>

using namespace cl;

namespace
{
    static String *make_string(test::VmTestContext &context,
                               const wchar_t *text)
    {
        return context.thread()->make_internal_raw<String>(text);
    }

    static void expect_pending_exception(ThreadState *thread,
                                         const wchar_t *class_name,
                                         const wchar_t *message)
    {
        ASSERT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        TValue<Exception> exception = thread->pending_exception_object();
        EXPECT_STREQ(class_name, exception.extract()
                                     ->get_shape()
                                     ->get_class()
                                     ->get_name()
                                     .extract()
                                     ->data);
        EXPECT_STREQ(
            message,
            cl::test::native_exception_message(exception).extract()->data);
    }
}  // namespace

TEST(List, UncheckedOperationsSupportAppendSetInsertAndErase)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    String *first = make_string(context, L"first");
    String *second = make_string(context, L"second");
    String *middle = make_string(context, L"middle");

    list->append(Value::from_oop(first));
    list->append(Value::from_oop(second));
    list->insert_item_unchecked(1, Value::from_oop(middle));

    ASSERT_EQ(3u, list->size());
    EXPECT_EQ(Value::from_oop(first), list->item_unchecked(0));
    EXPECT_EQ(Value::from_oop(middle), list->item_unchecked(1));
    EXPECT_EQ(Value::from_oop(second), list->item_unchecked(2));

    list->set_item_unchecked(2, Value::from_smi(42));
    EXPECT_EQ(Value::from_smi(42), list->item_unchecked(2));

    Value removed = list->pop_item_unchecked(1);
    EXPECT_EQ(Value::from_oop(middle), removed);
    ASSERT_EQ(2u, list->size());
    EXPECT_EQ(Value::from_oop(first), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(42), list->item_unchecked(1));
}

TEST(List, SizedConstructorInitializesEntriesToNotPresent)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>(3);

    ASSERT_EQ(3u, list->size());
    EXPECT_TRUE(list->item_unchecked(0).is_not_present());
    EXPECT_TRUE(list->item_unchecked(1).is_not_present());
    EXPECT_TRUE(list->item_unchecked(2).is_not_present());
}

TEST(List, ReserveTransfersBackingElementOwnership)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();
    String *item = make_string(context, L"kept");

    list->append(Value::from_oop(item));
    list->reserve(8);

    EXPECT_EQ(Value::from_oop(item), list->item_unchecked(0));
    EXPECT_EQ(1, item->refcount);
}

TEST(List, CheckedIndexingSupportsNegativeIndices)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    list->append(Value::from_smi(10));
    list->append(Value::from_smi(20));
    list->append(Value::from_smi(30));

    EXPECT_EQ(Value::from_smi(30), list->get_item(-1));
    EXPECT_EQ(Value::from_smi(20), list->get_item(-2));

    EXPECT_EQ(Value::None(), list->set_item(-1, Value::from_smi(99)));
    EXPECT_EQ(Value::from_smi(99), list->get_item(2));
}

TEST(List, SetItemUncheckedEnqueuesOverwrittenObject)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    List *list = thread->make_object_raw<List>();
    String *old_string = make_string(context, L"old-list");
    String *new_string = make_string(context, L"new-list");
    Owned<Value> keep_list(Value::from_oop(list));
    Owned<Value> keep_new(Value::from_oop(new_string));
    list->append(Value::from_oop(old_string));
    ASSERT_FALSE(thread->zero_count_table_contains_for_testing(old_string));
    ASSERT_EQ(HeapLifecycleState::Normal, old_string->lifecycle_state);

    list->set_item_unchecked(0, Value::from_oop(new_string));

    EXPECT_EQ(Value::from_oop(new_string), list->item_unchecked(0));
    EXPECT_EQ(0, old_string->refcount);
    EXPECT_EQ(HeapLifecycleState::InZct, old_string->lifecycle_state);
    EXPECT_TRUE(thread->zero_count_table_contains_for_testing(old_string));
    EXPECT_FALSE(thread->zero_count_table_contains_for_testing(new_string));
}

TEST(List, CheckedInsertClampsToPythonListSemantics)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    list->append(Value::from_smi(2));
    list->append(Value::from_smi(3));

    list->insert_item(-10, Value::from_smi(1));
    list->insert_item(100, Value::from_smi(4));

    ASSERT_EQ(4u, list->size());
    EXPECT_EQ(Value::from_smi(1), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), list->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(3), list->item_unchecked(2));
    EXPECT_EQ(Value::from_smi(4), list->item_unchecked(3));
}

TEST(List, PopItemSupportsDefaultAndNegativeIndices)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    String *first = make_string(context, L"first");
    String *second = make_string(context, L"second");
    String *third = make_string(context, L"third");
    list->append(Value::from_oop(first));
    list->append(Value::from_oop(second));
    list->append(Value::from_oop(third));

    Value last = list->pop_item();
    EXPECT_EQ(Value::from_oop(third), last);

    Value first_removed = list->pop_item(-2);
    EXPECT_EQ(Value::from_oop(first), first_removed);

    ASSERT_EQ(1u, list->size());
    EXPECT_EQ(Value::from_oop(second), list->item_unchecked(0));
}

TEST(List, CheckedIndexingRaisesIndexError)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    List *list = thread->make_object_raw<List>();

    list->append(Value::from_smi(1));

    EXPECT_TRUE(list->get_item(1).is_exception_marker());
    expect_pending_exception(thread, L"IndexError", L"list index out of range");
    thread->clear_pending_exception();

    EXPECT_TRUE(list->set_item(-2, Value::from_smi(0)).is_exception_marker());
    expect_pending_exception(thread, L"IndexError", L"list index out of range");
    thread->clear_pending_exception();

    EXPECT_TRUE(list->pop_item(3).is_exception_marker());
    expect_pending_exception(thread, L"IndexError", L"list index out of range");
}

TEST(List, ToTupleRetainsElementsAndPreservesSource)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<List>> source(thread->make_object_value<List>());
    Owned<TValue<List>> element(thread->make_object_value<List>());
    source.extract()->append(element.raw_value());
    source.extract()->append(Value::from_smi(42));
    int32_t refs_before = element.extract()->refcount;
    Owned<TValue<Tuple>> tuple(source.extract()->to_tuple(thread));
    EXPECT_EQ(refs_before + 1, element.extract()->refcount);
    ASSERT_EQ(2u, tuple.extract()->size());
    EXPECT_EQ(element.raw_value(), tuple.extract()->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(42), tuple.extract()->item_unchecked(1));
    EXPECT_EQ(2u, source.extract()->size());
    source.extract()->clear();
    EXPECT_EQ(element.raw_value(), tuple.extract()->item_unchecked(0));
    EXPECT_EQ(refs_before, element.extract()->refcount);
    Owned<TValue<Tuple>> empty(source.extract()->to_tuple(thread));
    EXPECT_TRUE(empty.extract()->empty());
    EXPECT_EQ(context.vm().tuple_class(),
              empty.extract()->get_shape()->get_class());
}

TEST(List, TrustedBuilderPrimitivesPreserveAliasesAndEvaluationOrder)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    auto name = context.vm().get_or_create_interned_string_value(
        L"<list-builder-test>");
    Owned<TValue<ModuleObject>> module(
        TValue<ModuleObject>::from_oop(context.make_test_module_object(
            name, context.vm().global_builtins_module().raw_value())));
    auto compiled =
        thread->compile_in_module(LR"(
events = []
builder = []
alias = builder
item = []
def get_builder():
    events.append(1)
    return builder
def get_item():
    events.append(2)
    return item
assert __clover_list_append__(get_builder(), get_item()) is None
assert events == [1, 2]
assert builder is alias
assert builder[0] is item
result = __clover_list_to_tuple__(builder)
assert result.__class__ is tuple
assert builder.__class__ is list
assert builder is alias
assert result[0] is item
__clover_list_append__(builder, 42)
assert len(builder) == 2
assert len(result) == 1
empty = []
assert __clover_list_to_tuple__(empty) == ()
assert empty.__class__ is list
for i in range(100):
    __clover_list_append__(empty, i)
converted = __clover_list_to_tuple__(empty)
assert len(converted) == 100
assert converted[99] == 99
assert len(empty) == 100
)",
                                  StartRule::File, module.extract(),
                                  LanguageMode::TrustedCloverExtensions);
    ASSERT_FALSE(compiled.has_exception());
    Value result = thread->run_clovervm_code_object(compiled.value());
    if(result.is_exception_marker())
    {
        FAIL() << string_as_wchar_t(
            test::native_exception_message(thread->pending_exception_object()));
    }
    ASSERT_FALSE(result.is_exception_marker());
}
