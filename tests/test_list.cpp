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
    [[nodiscard]] static Value
    run_trusted_list_test(test::VmTestContext &context, const wchar_t *source)
    {
        auto name = context.vm().get_or_create_interned_string_value(
            L"<container-conversion-test>");
        Owned<TValue<ModuleObject>> module(
            TValue<ModuleObject>::from_oop(context.make_test_module_object(
                name, context.vm().global_builtins_module().raw_value())));
        auto compiled = context.thread()->compile_in_module(
            source, StartRule::File, module.extract(),
            LanguageMode::TrustedCloverExtensions);
        if(compiled.has_exception())
        {
            return Value::exception_marker();
        }
        return context.thread()->run_clovervm_code_object(compiled.value());
    }

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
    EXPECT_EQ(1, item->refcount());
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
    ASSERT_EQ(HeapLifecycleState::Normal, old_string->lifecycle_state());

    list->set_item_unchecked(0, Value::from_oop(new_string));

    EXPECT_EQ(Value::from_oop(new_string), list->item_unchecked(0));
    EXPECT_EQ(0, old_string->refcount());
    EXPECT_EQ(HeapLifecycleState::InZct, old_string->lifecycle_state());
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
    int32_t refs_before = element.extract()->refcount();
    Owned<TValue<Tuple>> tuple(source.extract()->to_tuple(thread));
    EXPECT_EQ(refs_before + 1, element.extract()->refcount());
    ASSERT_EQ(2u, tuple.extract()->size());
    EXPECT_EQ(element.raw_value(), tuple.extract()->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(42), tuple.extract()->item_unchecked(1));
    EXPECT_EQ(2u, source.extract()->size());
    source.extract()->clear();
    EXPECT_EQ(element.raw_value(), tuple.extract()->item_unchecked(0));
    EXPECT_EQ(refs_before, element.extract()->refcount());
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

TEST(ExactContainerConversion, IdentityCopiesAndRepeatedFastSlowCalls)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_list_test(context, LR"(
def to_tuple(source):
    return __clover_to_exact_tuple__(source)
def to_list(source):
    return __clover_to_exact_list__(source)
item = []
xs = [item, 2]
ys = (item, 2)
for repeat in range(10):
    assert to_tuple(ys) is ys
    assert to_list(xs) is xs
    copied_tuple = to_tuple(xs)
    copied_list = to_list(ys)
    assert copied_tuple.__class__ is tuple
    assert copied_list.__class__ is list
    assert copied_tuple == ys
    assert copied_list == xs
    assert copied_tuple[0] is item
    assert copied_list[0] is item
    assert to_tuple(iter(xs)) == ys
    assert to_list(iter(ys)) == xs
    assert to_tuple(range(3)) == (0, 1, 2)
    assert to_list(range(3)) == [0, 1, 2]
assert to_tuple([]) == ()
assert to_list(()) == []
assert to_list(()) is not to_list(())
xs.append(3)
assert len(copied_tuple) == 2
assert len(copied_list) == 2
assert len(xs) == 3
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(ExactContainerConversion, SubclassesUseIteratorOverrides)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    auto name = context.vm().get_or_create_interned_string_value(
        L"<conversion-subclasses>");
    Owned<TValue<ModuleObject>> module(
        TValue<ModuleObject>::from_oop(context.make_test_module_object(
            name, context.vm().global_builtins_module().raw_value())));
    auto classes =
        thread->compile_in_module(LR"(
events = []
class L(list):
    def __iter__(self):
        events.append('list')
        return iter((7, 8))
class T(tuple):
    def __iter__(self):
        events.append('tuple')
        return iter((9, 10))
)",
                                  StartRule::File, module.extract(),
                                  LanguageMode::TrustedCloverExtensions);
    ASSERT_FALSE(classes.has_exception());
    ASSERT_FALSE(thread->run_clovervm_code_object(classes.value())
                     .is_exception_marker());
    auto cls = [&](const wchar_t *text) {
        return module.extract()
            ->get_own_property(
                context.vm().get_or_create_interned_string_value(text))
            .get_ptr<ClassObject>();
    };
    Owned<TValue<List>> list(thread->make_internal_value<List>(cls(L"L")));
    list.extract()->append(Value::from_smi(1));
    Owned<TValue<Tuple>> tuple(
        thread->make_internal_value<Tuple>(cls(L"T"), 1));
    tuple.extract()->initialize_item_unchecked(0, Value::from_smi(2));
    ASSERT_TRUE(module.extract()
                    ->set_own_property(
                        context.vm().get_or_create_interned_string_value(L"xs"),
                        list.raw_value())
                    .value());
    ASSERT_TRUE(module.extract()
                    ->set_own_property(
                        context.vm().get_or_create_interned_string_value(L"ys"),
                        tuple.raw_value())
                    .value());
    auto compiled =
        thread->compile_in_module(LR"(
a = __clover_to_exact_list__(xs)
b = __clover_to_exact_tuple__(ys)
assert a.__class__ is list and a == [7, 8]
assert b.__class__ is tuple and b == (9, 10)
assert a is not xs and b is not ys
assert __clover_to_exact_tuple__(xs) == (7, 8)
assert __clover_to_exact_list__(ys) == [9, 10]
assert events == ['list', 'tuple', 'list', 'tuple']
builder = []
assert __clover_list_extend__(builder, xs) is None
assert builder == [7, 8]
assert __clover_list_extend__(builder, ys) is None
assert builder == [7, 8, 9, 10]
assert events == ['list', 'tuple', 'list', 'tuple', 'list', 'tuple']
assert xs[0] == 1 and ys[0] == 2
)",
                                  StartRule::File, module.extract(),
                                  LanguageMode::TrustedCloverExtensions);
    ASSERT_FALSE(compiled.has_exception());
    Value result = thread->run_clovervm_code_object(compiled.value());
    ASSERT_FALSE(result.is_exception_marker()) << string_as_wchar_t(
        test::native_exception_message(thread->pending_exception_object()));
}

TEST(ExactContainerConversion, NestedCallbacksAndCallerExceptionHandlers)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_list_test(context, LR"(
class Source:
    def __init__(self): self.position = 0
    def __iter__(self): return self
    def __next__(self):
        if self.position == 3: raise StopIteration
        current = self.position
        self.position += 1
        # Re-enter the same conversion helper from an iterator callback.
        nested = __clover_to_exact_tuple__([current])
        assert nested == (current,)
        return nested
source = Source()
keep = []
# Earlier expression results must survive the conversion's implicit call window.
def pair(a, b, c):
    assert a is keep and c is keep
    return b
result = pair(keep, __clover_to_exact_list__(source), keep)
assert result == [(0,), (1,), (2,)]
assert source.position == 3
assert source.__class__ is Source
class BadIter:
    def __iter__(self): raise ValueError('iter failed')
class BadNext:
    def __init__(self): self.position = 0
    def __iter__(self): return self
    def __next__(self):
        self.position += 1
        if self.position == 1: return keep
        raise ValueError('next failed')
for mode in range(2):
    bad = BadNext()
    reached = False
    try:
        if mode == 0:
            __clover_to_exact_tuple__(bad)
        else:
            __clover_to_exact_list__(bad)
        reached = True
    except ValueError as error:
        assert error.args[0] == 'next failed'
    assert not reached
    assert bad.position == 2
    try:
        if mode == 0:
            __clover_to_exact_tuple__(BadIter())
        else:
            __clover_to_exact_list__(BadIter())
    except ValueError as error:
        assert error.args[0] == 'iter failed'
    else:
        raise AssertionError
    try:
        if mode == 0:
            __clover_to_exact_tuple__(42)
        else:
            __clover_to_exact_list__(42)
    except TypeError:
        pass
    else:
        raise AssertionError
assert __clover_to_exact_list__(Source()) == [(0,), (1,), (2,)]
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(ListExtension, NativePathsPreserveReferencesAndHandleSelfExtension)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_list_test(context, LR"(
builder = []
alias = builder
item = []
source = [item, 2]
assert __clover_list_extend__(builder, source) is None
assert builder is alias
assert builder == [item, 2]
assert builder[0] is item and source[0] is item
assert __clover_list_extend__(builder, (item, 3)) is None
assert builder == [item, 2, item, 3]
assert builder[2] is item
assert __clover_list_extend__(builder, []) is None
assert __clover_list_extend__(builder, ()) is None
assert builder == [item, 2, item, 3]
assert source == [item, 2]
large = []
for i in range(70):
    __clover_list_append__(large, [i])
assert __clover_list_extend__(large, large) is None
assert len(large) == 140
for i in range(70):
    assert large[i] is large[i + 70]
    assert large[i][0] == i
empty = []
assert __clover_list_extend__(empty, empty) is None
assert len(empty) == 0
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(ListExtension, GenericSourcesNestedCallbacksAndEvaluationOrder)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_list_test(context, LR"(
events = []
builder = []
def get_builder():
    events.append('builder')
    return builder
def get_source():
    events.append('source')
    return (1, 2)
assert __clover_list_extend__(get_builder(), get_source()) is None
assert events == ['builder', 'source']
assert builder == [1, 2]
class Source:
    def __init__(self): self.position = 0
    def __iter__(self): return self
    def __next__(self):
        if self.position == 3: raise StopIteration
        current = self.position
        self.position += 1
        nested = []
        assert __clover_list_extend__(nested, iter((current,))) is None
        return nested[0]
source = Source()
keep = []
def pair(a, b, c):
    assert a is keep and c is keep
    assert b is None
    return b
assert pair(keep, __clover_list_extend__(builder, source), keep) is None
assert builder == [1, 2, 0, 1, 2]
assert source.position == 3
assert __clover_list_extend__(builder, iter(())) is None
assert __clover_list_extend__(builder, range(2)) is None
assert builder == [1, 2, 0, 1, 2, 0, 1]
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(ListExtension, IterationFailuresKeepPartialDestinationAndUnwind)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_list_test(context, LR"(
item = []
class BadIter:
    def __iter__(self): raise ValueError('iter failed')
class BadNext:
    def __init__(self): self.position = 0
    def __iter__(self): return self
    def __next__(self):
        self.position += 1
        if self.position == 1: return item
        raise ValueError('next failed')
builder = [42]
alias = builder
bad = BadNext()
reached = False
try:
    __clover_list_extend__(builder, bad)
    reached = True
except ValueError as error:
    assert error.args[0] == 'next failed'
assert not reached
assert builder is alias
assert len(builder) == 2 and builder[0] == 42 and builder[1] is item
assert bad.position == 2
try:
    __clover_list_extend__(builder, BadIter())
except ValueError as error:
    assert error.args[0] == 'iter failed'
else:
    raise AssertionError
assert len(builder) == 2
try:
    __clover_list_extend__(builder, 42)
except TypeError:
    pass
else:
    raise AssertionError
assert len(builder) == 2
assert __clover_list_extend__(builder, iter((7,))) is None
assert len(builder) == 3 and builder[2] == 7
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(List, SortRetainsDetachedStorageAndKeysAcrossReclamation)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    CodeObject *code = context.compile_file(LR"(
source = []
key_calls = []
comparisons = []
fail_comparison = False
class Payload:
    def __init__(self, value):
        self.value = value
        self.text = 'payload-' + str(value)
    def __lt__(self, other):
        assert source == []
        garbage = []
        for i in range(20):
            garbage.append([i])
        garbage.clear()
        return self.value < other.value
class Key:
    def __init__(self, value):
        self.value = value
        self.text = 'key-' + str(value)
    def __lt__(self, other):
        assert source == []
        comparisons.append(1)
        garbage = []
        for i in range(20):
            garbage.append(Payload(i))
        garbage.clear()
        assert self.text == 'key-' + str(self.value)
        assert other.text == 'key-' + str(other.value)
        if fail_comparison:
            source.append(Payload(99))
            source.clear()
            raise failure
        return self.value < other.value

def key(item):
    assert source == []
    key_calls.append(item.value)
    garbage = []
    for i in range(20):
        garbage.append(Payload(i))
    garbage.clear()
    assert item.text == 'payload-' + str(item.value)
    return Key(item.value)

for reverse in (False, True):
    source = [Payload(3), Payload(1), Payload(2), Payload(1)]
    key_calls.clear()
    source.sort(key=key, reverse=reverse)
    assert key_calls == [3, 1, 2, 1]
    expected = [3, 2, 1, 1] if reverse else [1, 1, 2, 3]
    for i in range(4):
        assert source[i].value == expected[i]
        assert source[i].text == 'payload-' + str(expected[i])
assert len(comparisons) > 0

# Reclamation during failure must preserve original values and previously
# evaluated keys until the native restoration path releases them.
failure = RuntimeError('key failure')
def bad_key(item):
    result = key(item)
    if item.value == 1:
        source.append(Payload(99))
        source.clear()
        raise failure
    return result
source = [Payload(3), Payload(1), Payload(2)]
try:
    source.sort(key=bad_key)
except RuntimeError as caught:
    assert caught is failure
else:
    raise AssertionError('expected key failure')
assert source[0].value == 3
assert source[1].value == 1
assert source[2].value == 2
source.sort(key=key)
assert source[0].value == 1
assert source[1].value == 2
assert source[2].value == 3

fail_comparison = True
source = [Payload(3), Payload(1), Payload(2)]
try:
    source.sort(key=key)
except RuntimeError as caught:
    assert caught is failure
else:
    raise AssertionError('expected comparison failure')
assert source[0].value == 3
assert source[1].value == 1
assert source[2].value == 2
fail_comparison = False
source.sort(key=key)
assert source[0].value == 1
assert source[1].value == 2
assert source[2].value == 3

# With no key array retaining values, Python locals must keep the saved item
# alive across safepoints while permutation cycles overwrite detached slots.
for reverse in (False, True):
    source = [Payload(4), Payload(1), Payload(3), Payload(0), Payload(2)]
    source.sort(reverse=reverse)
    for i in range(5):
        expected = 4 - i if reverse else i
        assert source[i].value == expected
        assert source[i].text == 'payload-' + str(expected)
)");
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    ASSERT_FALSE(
        context.thread()->run_clovervm_code_object(code).is_exception_marker());
}

TEST(List, SortReleasesTemporaryKeyOwnership)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Owned<Value> keys(context.run_file(LR"(
keys = []
class Key:
    def __init__(self, value):
        self.value = value
    def __lt__(self, other):
        return self.value < other.value

def key(value):
    result = Key(value)
    keys.append(result)
    return result
[3, 1, 2].sort(key=key)
def failing_key(value):
    result = key(value)
    if value == 1:
        raise RuntimeError('key failed')
    return result
values = [3, 1, 2]
try:
    values.sort(key=failing_key)
except RuntimeError:
    pass
else:
    raise AssertionError('expected key failure')
assert values == [3, 1, 2]
keys
)"));
    ASSERT_TRUE(has_native_layout<List>(keys.value()));
    // Execution has returned to C++. Exclude obsolete interpreter slots from
    // the conservative scan before checking that temporary key arrays die.
    context.thread()->publish_safepoint_scan_record(
        SafepointScanRecord::idle(context.thread()->clover_frame_sentinel()));
    // Stack-only temporaries are discovered by the bitmap scan. Drain their
    // newly enqueued child backings in the following reclamation epoch.
    context.vm().run_heap_reclamation();
    context.vm().run_heap_reclamation();
    List *observed = keys.value().get_ptr<List>();
    ASSERT_EQ(5u, observed->size());
    for(size_t index = 0; index < observed->size(); ++index)
        EXPECT_EQ(1, observed->item_unchecked(index).get_ptr()->refcount());
}

TEST(List, SortPreservesExistingCallableLimitations)
{
    test::VmTestContext context;
    ASSERT_FALSE(context
                     .run_file(LR"(
class Callable:
    def __call__(self, item):
        raise AssertionError('sort must not add callable instance support')

values = [2, 1]
try:
    values.sort(key=Callable())
except TypeError:
    pass
else:
    raise AssertionError('expected existing callable instance limitation')
assert values == [2, 1]
)")
                     .is_exception_marker());
}
