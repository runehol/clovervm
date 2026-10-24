#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/function.h"
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

TEST(Tuple, SizedConstructorInitializesEntriesToNotPresent)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple =
        context.thread()->make_internal_raw<Tuple>(BootstrapObjectTag{}, 3);

    ASSERT_EQ(3u, tuple->size());
    EXPECT_TRUE(tuple->item_unchecked(0).is_not_present());
    EXPECT_TRUE(tuple->item_unchecked(1).is_not_present());
    EXPECT_TRUE(tuple->item_unchecked(2).is_not_present());
}

TEST(Tuple, ObjectAllocationUsesTupleClass)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple = context.thread()->make_object_raw<Tuple>(2);

    ASSERT_NE(nullptr, context.vm().tuple_class());
    EXPECT_EQ(context.vm().tuple_class(), tuple->get_shape()->get_class());
    EXPECT_EQ(2u, tuple->size());
}

TEST(Tuple, CustomInitRemainsUnsupportedAndDoesNotRunSourceCallbacks)
{
    test::FileRunner runner(LR"PY(
events = []
class Source:
    def __iter__(self):
        events.append("iter")
        return iter([1])
class T(tuple):
    def __init__(self, source):
        events.append("init")
try:
    T(Source())
except TypeError:
    pass
else:
    assert False
assert events == []
# Explicit allocation still works without running the deferred initializer.
allocated = tuple.__new__(T, Source())
assert allocated.__class__ is T
assert allocated == (1,)
assert events == ["iter"]
del T.__init__
assert T([2]) == (2,)
)PY");
    EXPECT_FALSE(runner.return_value.is_exception_marker());
}

TEST(Tuple, SubclassConstructionRetainsPayloadAndReleasesFailedBuilder)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<Value> functions(context.run_file(LR"PY(
class T(tuple):
    pass
class Broken:
    def __init__(self, source):
        self.source = source
        self.index = 0
    def __iter__(self):
        return self
    def __next__(self):
        if self.index == 0:
            self.index = 1
            return self.source[0]
        raise ValueError("failed after yielding")
def build(source):
    return T(source)
def fail(source):
    return T(Broken(source))
(build, fail)
)PY"));
    ASSERT_TRUE(has_native_layout<Tuple>(functions.value()));
    Tuple *pair = native_cast<Tuple>(functions.value());
    TValue<Function> build =
        TValue<Function>::from_value_assumed(pair->item_unchecked(0));
    TValue<Function> fail =
        TValue<Function>::from_value_assumed(pair->item_unchecked(1));

    Owned<TValue<String>> child(
        thread->make_object_value<String>(L"owned payload"));
    Owned<Value> result(Value::None());
    {
        Owned<TValue<Tuple>> source(thread->make_object_value<Tuple>(1));
        source.extract()->initialize_item_unchecked(0, child.raw_value());
        result = thread->call_clovervm_function(build, source.raw_value());
        ASSERT_TRUE(has_native_layout<Tuple>(result.value()));
        Tuple *tuple = native_cast<Tuple>(result.value());
        EXPECT_NE(context.vm().tuple_class(), tuple->get_shape()->get_class());
        EXPECT_EQ(Tuple::size_for(1), object_size_in_bytes(tuple));
        EXPECT_EQ(child.raw_value(), tuple->item_unchecked(0));
        EXPECT_EQ(3, child.extract()->refcount);

        EXPECT_TRUE(thread->call_clovervm_function(fail, source.raw_value())
                        .is_exception_marker());
        thread->clear_pending_exception();
        // The epoch scan can enqueue a builder's backing storage after the
        // zero-count-table phase; the following pass releases that storage.
        context.vm().run_heap_reclamation();
        context.vm().run_heap_reclamation();
        EXPECT_EQ(3, child.extract()->refcount);
    }
    // Dropping an Owned handle enqueues its tuple; reclamation releases items.
    context.vm().run_heap_reclamation();
    EXPECT_EQ(2, child.extract()->refcount);
    result = Value::None();
    context.vm().run_heap_reclamation();
    EXPECT_EQ(1, child.extract()->refcount);
}

TEST(Tuple, EmptyTupleHasNoItems)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple =
        context.thread()->make_internal_raw<Tuple>(BootstrapObjectTag{}, 0);

    EXPECT_TRUE(tuple->empty());
    EXPECT_EQ(0u, tuple->size());
}

TEST(Tuple, InitializeItemUncheckedStoresValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple =
        context.thread()->make_internal_raw<Tuple>(BootstrapObjectTag{}, 2);

    String *first = make_string(context, L"first");
    String *second = make_string(context, L"second");

    tuple->initialize_item_unchecked(0, Value::from_oop(first));
    tuple->initialize_item_unchecked(1, Value::from_oop(second));

    EXPECT_EQ(Value::from_oop(first), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_oop(second), tuple->item_unchecked(1));
}

TEST(Tuple, CheckedIndexingSupportsNegativeIndices)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple =
        context.thread()->make_internal_raw<Tuple>(BootstrapObjectTag{}, 3);

    tuple->initialize_item_unchecked(0, Value::from_smi(10));
    tuple->initialize_item_unchecked(1, Value::from_smi(20));
    tuple->initialize_item_unchecked(2, Value::from_smi(30));

    EXPECT_EQ(Value::from_smi(30), tuple->get_item(-1));
    EXPECT_EQ(Value::from_smi(20), tuple->get_item(-2));
}

TEST(Tuple, CheckedIndexingRaisesIndexError)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Tuple *tuple = thread->make_internal_raw<Tuple>(BootstrapObjectTag{}, 1);

    tuple->initialize_item_unchecked(0, Value::from_smi(1));

    EXPECT_TRUE(tuple->get_item(1).is_exception_marker());
    expect_pending_exception(thread, L"IndexError",
                             L"tuple index out of range");
    thread->clear_pending_exception();

    EXPECT_TRUE(tuple->get_item(-2).is_exception_marker());
    expect_pending_exception(thread, L"IndexError",
                             L"tuple index out of range");
}
