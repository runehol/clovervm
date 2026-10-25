#include "builtin_types/bigint.h"
#include "builtin_types/sequence_iterator.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "object_model/function.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
#include <cstddef>
#include <gtest/gtest.h>
#include <limits>

using namespace cl;

TEST(SequenceIterator, ReleasesSourceOnExhaustionAndDuringReclamation)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<Value> functions(context.run_file(LR"PY(
class Sequence:
    def __init__(self, item): self.item = item
    def __getitem__(self, index):
        if index == 0: return self.item
        raise IndexError
def make(item): return iter(Sequence(item))
def advance(iterator): return next(iterator, None)
(make, advance)
)PY"));
    ASSERT_TRUE(has_native_layout<Tuple>(functions.value()));
    TValue<Function> make = TValue<Function>::from_value_assumed(
        native_cast<Tuple>(functions.value())->item_unchecked(0));
    TValue<Function> advance = TValue<Function>::from_value_assumed(
        native_cast<Tuple>(functions.value())->item_unchecked(1));
    Owned<TValue<String>> payload(
        thread->make_object_value<String>(L"payload"));
    Owned<Value> iterator(
        thread->call_clovervm_function(make, payload.raw_value()));
    ASSERT_TRUE(has_native_layout<SequenceIterator>(iterator.value()));
    EXPECT_FALSE(native_cast<SequenceIterator>(iterator.value())
                     ->source.value()
                     .is_not_present());
    context.vm().run_heap_reclamation();
    EXPECT_EQ(payload.raw_value(),
              thread->call_clovervm_function(advance, iterator.value()));
    EXPECT_TRUE(
        thread->call_clovervm_function(advance, iterator.value()).is_none());
    EXPECT_TRUE(native_cast<SequenceIterator>(iterator.value())
                    ->source.value()
                    .is_not_present());
    context.vm().run_heap_reclamation();
    context.vm().run_heap_reclamation();
    EXPECT_EQ(1u, payload.extract()->refcount);
    EXPECT_TRUE(
        thread->call_clovervm_function(advance, iterator.value()).is_none());
    iterator = Value::None();
    context.vm().run_heap_reclamation();
}

TEST(SequenceIterator, IndexOverflowDoesNotCallSourceOrClearIt)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<Value> functions(context.run_file(LR"PY(
class Source:
    def __getitem__(self, index): raise AssertionError('must not call source')
def make(): return iter(Source())
def overflow(iterator):
    try: next(iterator)
    except OverflowError: return True
    return False
(make, overflow)
)PY"));
    ASSERT_TRUE(has_native_layout<Tuple>(functions.value()));
    Tuple *pair = native_cast<Tuple>(functions.value());
    TValue<Function> make =
        TValue<Function>::from_value_assumed(pair->item_unchecked(0));
    TValue<Function> overflow =
        TValue<Function>::from_value_assumed(pair->item_unchecked(1));
    Owned<Value> iterator(thread->call_clovervm_function(make));
    ASSERT_TRUE(has_native_layout<SequenceIterator>(iterator.value()));
    native_cast<SequenceIterator>(iterator.value())->index =
        std::numeric_limits<ptrdiff_t>::max();
    EXPECT_EQ(Value::True(),
              thread->call_clovervm_function(overflow, iterator.value()));
    EXPECT_EQ(Value::True(),
              thread->call_clovervm_function(overflow, iterator.value()));
    EXPECT_FALSE(native_cast<SequenceIterator>(iterator.value())
                     ->source.value()
                     .is_not_present());
}

TEST(SequenceIterator, ManagedCallbacksSurviveEverySafepointReclamation)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<CodeObject>> code(
        TValue<CodeObject>::from_oop(context.compile_file(LR"PY(
class Source:
    def __getitem__(self, index):
        if index == 8: raise IndexError
        return (index, [index + 1])
def exercise():
    result = []
    for item in Source():
        result.append(item)
    assert len(result) == 8
    assert result[7] == (7, [8])
    assert tuple(Source())[0] == (0, [1])
exercise()
)PY")));
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    Value result = thread->run_clovervm_code_object(code.extract());
    EXPECT_FALSE(result.is_exception_marker());
}

TEST(SequenceIterator, IndexCanCrossSmiBoundary)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<Value> functions(context.run_file(LR"PY(
class Source:
    def __getitem__(self, index): return index
def make(): return iter(Source())
def advance(iterator): return next(iterator)
(make, advance)
)PY"));
    ASSERT_TRUE(has_native_layout<Tuple>(functions.value()));
    Tuple *pair = native_cast<Tuple>(functions.value());
    TValue<Function> make =
        TValue<Function>::from_value_assumed(pair->item_unchecked(0));
    TValue<Function> advance =
        TValue<Function>::from_value_assumed(pair->item_unchecked(1));
    Owned<Value> iterator(thread->call_clovervm_function(make));
    ASSERT_TRUE(has_native_layout<SequenceIterator>(iterator.value()));
    native_cast<SequenceIterator>(iterator.value())->index = value_smi_max + 1;
    Owned<Value> result(
        thread->call_clovervm_function(advance, iterator.value()));
    ASSERT_TRUE(has_native_layout<BigInt>(result.value()));
    EXPECT_EQ(
        value_smi_max + 1,
        bigint_to_int64(native_cast<BigInt>(result.value())->view()).value());
    EXPECT_EQ(value_smi_max + 2,
              native_cast<SequenceIterator>(iterator.value())->index);
}
