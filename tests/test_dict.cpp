#include "builtin_types/dict.h"
#include "builtin_types/str.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
#include <cassert>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace cl;

namespace
{
    static Value make_string(test::VmTestContext &context, const wchar_t *text)
    {
        return context.thread()->make_internal_value<String>(text).raw_value();
    }

    static TValue<String> string_key(Value key)
    {
        return TValue<String>::from_value_unchecked(key);
    }

    static void set_dict_item(ThreadState *thread, Dict *dict, Value key,
                              Value value)
    {
        ASSERT_FALSE(dict->set_item_for_str(thread, string_key(key), value)
                         .has_exception());
    }

    static Value get_dict_item(ThreadState *thread, Dict *dict, Value key)
    {
        Expected<Value> result =
            dict->get_item_for_str(thread, string_key(key));
        return result.has_exception() ? Value::exception_marker()
                                      : result.value();
    }

    static Value del_dict_item(ThreadState *thread, Dict *dict, Value key)
    {
        Expected<void> result = dict->del_item_for_str(thread, string_key(key));
        return result.has_exception() ? Value::exception_marker()
                                      : Value::None();
    }

    static bool dict_contains(ThreadState *thread, Dict *dict, Value key)
    {
        Expected<bool> result = dict->contains_for_str(thread, string_key(key));
        assert(!result.has_exception());
        return result.value();
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
        EXPECT_STREQ(message, exception.extract()->message.extract()->data);
    }
}  // namespace

TEST(Dict, SetGetAndContainsWorkForStringKeys)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value alpha = make_string(context, L"alpha");
    Value beta = make_string(context, L"beta");

    set_dict_item(context.thread(), dict, alpha, Value::from_smi(11));
    set_dict_item(context.thread(), dict, beta, Value::from_smi(22));

    EXPECT_EQ(2u, dict->size());
    EXPECT_FALSE(dict->empty());
    EXPECT_TRUE(dict_contains(context.thread(), dict, alpha));
    EXPECT_TRUE(dict_contains(context.thread(), dict, beta));
    EXPECT_EQ(Value::from_smi(11),
              get_dict_item(context.thread(), dict, alpha));
    EXPECT_EQ(Value::from_smi(22), get_dict_item(context.thread(), dict, beta));
}

TEST(Dict, SemanticApiAcceptsStringValueKeys)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    Value key = make_string(context, L"key");

    ASSERT_FALSE(
        dict->set_item(thread, key, Value::from_smi(42)).has_exception());
    EXPECT_TRUE(dict->contains(thread, key).value());
    EXPECT_EQ(Value::from_smi(42), dict->get_item(thread, key).value());
    ASSERT_FALSE(dict->del_item(thread, key).has_exception());
    EXPECT_FALSE(dict->contains(thread, key).value());
}

TEST(Dict, SemanticApiPopAndSetdefaultAcceptStringValueKeys)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    Value key = make_string(context, L"key");

    EXPECT_EQ(Value::from_smi(10),
              dict->setdefault(thread, key, Value::from_smi(10)).value());
    EXPECT_EQ(Value::from_smi(10), dict->get_item(thread, key).value());
    EXPECT_EQ(Value::from_smi(10),
              dict->setdefault(thread, key, Value::from_smi(20)).value());
    EXPECT_EQ(Value::from_smi(10), dict->get_item(thread, key).value());

    EXPECT_EQ(Value::from_smi(10), dict->pop(thread, key).value());
    EXPECT_FALSE(dict->contains(thread, key).value());
}

TEST(Dict, SemanticApiRejectsNonStringKeysUntilPromotionStage)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    EXPECT_TRUE(dict->get_item(thread, Value::from_smi(1)).has_exception());
    expect_pending_exception(thread, L"TypeError", L"dict keys must be str");
    thread->clear_pending_exception();

    EXPECT_TRUE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                    .has_exception());
    expect_pending_exception(thread, L"TypeError", L"dict keys must be str");
    thread->clear_pending_exception();

    EXPECT_TRUE(dict->del_item(thread, Value::from_smi(1)).has_exception());
    expect_pending_exception(thread, L"TypeError", L"dict keys must be str");
    thread->clear_pending_exception();

    EXPECT_TRUE(dict->contains(thread, Value::from_smi(1)).has_exception());
    expect_pending_exception(thread, L"TypeError", L"dict keys must be str");
}

TEST(Dict, SemanticApiPopAndSetdefaultRejectNonStringKeysUntilPromotionStage)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    EXPECT_TRUE(dict->pop(thread, Value::from_smi(1)).has_exception());
    expect_pending_exception(thread, L"TypeError", L"dict keys must be str");
    thread->clear_pending_exception();

    EXPECT_TRUE(
        dict->setdefault(thread, Value::from_smi(1), Value::from_smi(11))
            .has_exception());
    expect_pending_exception(thread, L"TypeError", L"dict keys must be str");
}

TEST(Dict, ExactBuiltinDictShapesAreCached)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);

    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();
    Shape *general_shape = thread->get_exact_dict_general_shape();

    ASSERT_NE(nullptr, string_key_shape);
    ASSERT_NE(nullptr, general_shape);
    EXPECT_NE(string_key_shape, general_shape);
    EXPECT_EQ(context.vm().dict_class(), string_key_shape->get_class());
    EXPECT_EQ(context.vm().dict_class(), general_shape->get_class());
}

TEST(Dict, ExactBuiltinDictConstructionStartsStringKeyed)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    Dict *native_dict = thread->make_object_raw<Dict>();
    EXPECT_EQ(string_key_shape, native_dict->get_shape());

    Value literal = context.run_file(L"{}\n");
    ASSERT_TRUE(can_convert_to<Dict>(literal));
    EXPECT_EQ(string_key_shape, literal.get_ptr<Dict>()->get_shape());

    Value constructed = context.run_file(L"dict()\n");
    ASSERT_TRUE(can_convert_to<Dict>(constructed));
    EXPECT_EQ(string_key_shape, constructed.get_ptr<Dict>()->get_shape());
}

TEST(Dict, TableGenerationChangesOnlyWhenProbeStructureChanges)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value alpha = make_string(context, L"alpha");
    Value beta = make_string(context, L"beta");

    EXPECT_EQ(0u, dict->table_generation());
    set_dict_item(context.thread(), dict, alpha, Value::from_smi(11));
    EXPECT_EQ(0u, dict->table_generation());
    set_dict_item(context.thread(), dict, alpha, Value::from_smi(99));
    EXPECT_EQ(0u, dict->table_generation());
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, alpha));
    EXPECT_EQ(0u, dict->table_generation());

    dict->clear();
    EXPECT_EQ(1u, dict->table_generation());
    set_dict_item(context.thread(), dict, beta, Value::from_smi(22));
    EXPECT_EQ(1u, dict->table_generation());

    for(int64_t key = 0; key < 20; ++key)
    {
        std::wstring text = L"key";
        text += std::to_wstring(key);
        set_dict_item(context.thread(), dict,
                      make_string(context, text.c_str()), Value::from_smi(key));
    }

    EXPECT_GT(dict->table_generation(), 1u);
}

TEST(Dict, ReinsertReusesTombstoneWithoutChangingEntryOrder)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value first = make_string(context, L"first");
    Value second = make_string(context, L"second");
    Value third = make_string(context, L"third");

    set_dict_item(context.thread(), dict, first, Value::from_smi(1));
    set_dict_item(context.thread(), dict, second, Value::from_smi(2));
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, first));
    set_dict_item(context.thread(), dict, third, Value::from_smi(3));

    EXPECT_EQ(2u, dict->size());
    EXPECT_EQ(3u, dict->entry_storage_size());
    Dict::EntryView second_entry = {Value::not_present(), Value::not_present()};
    Dict::EntryView third_entry = {Value::not_present(), Value::not_present()};
    ASSERT_FALSE(dict->entry_at(0, second_entry));
    ASSERT_TRUE(dict->entry_at(1, second_entry));
    ASSERT_TRUE(dict->entry_at(2, third_entry));
    EXPECT_EQ(second, second_entry.key);
    EXPECT_EQ(third, third_entry.key);
}

TEST(GeneralDict, ConstructsAsInternalClassAndStartsEmpty)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    GeneralDict *dict = context.thread()->make_object_raw<GeneralDict>();

    EXPECT_EQ(context.vm().general_dict_class(),
              dict->get_shape()->get_class());
    EXPECT_STREQ(L"__clover_general_dict",
                 dict->get_shape()->get_class()->get_name().extract()->data);
    EXPECT_EQ(0u, dict->size());
    EXPECT_TRUE(dict->empty());
}

TEST(GeneralDict, LenMethodReturnsLiveEntryCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    GeneralDict *dict = context.thread()->make_object_raw<GeneralDict>();
    TValue<String> dunder_len_name =
        context.vm().get_or_create_interned_string_value(L"__len__");

    Value result = context.thread()->call_clovervm_method(Value::from_oop(dict),
                                                          dunder_len_name);

    EXPECT_EQ(Value::from_smi(0), result);
    EXPECT_FALSE(context.thread()->has_pending_exception());
}

TEST(GeneralDict, TemporaryBuiltinBindingCanConstructAndLen)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(0),
              context.run_file(L"len(__clover_general_dict())\n"));
}

TEST(GeneralDict, SetItemInsertsIntegerKeysThroughTemporaryBuiltin)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(2),
              context.run_file(L"d = __clover_general_dict()\n"
                               L"d[1] = 'one'\n"
                               L"d[2] = 'two'\n"
                               L"len(d)\n"));
}

TEST(GeneralDict, SetItemOverwritesEqualBoolAndIntKey)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    GeneralDict *dict = thread->make_object_raw<GeneralDict>();

    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                     .has_exception());
    ASSERT_FALSE(dict->set_item(thread, Value::True(), Value::from_smi(22))
                     .has_exception());

    EXPECT_EQ(1u, dict->size());
    ASSERT_EQ(1u, dict->entry_storage_size());
    GeneralDict::EntryView entry = {Value::not_present(), Value::not_present()};
    ASSERT_TRUE(dict->entry_at(0, entry));
    EXPECT_EQ(Value::from_smi(1), entry.key);
    EXPECT_EQ(Value::from_smi(22), entry.value);
}

TEST(GeneralDict, SetItemOverwritePreservesEntryOrder)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    GeneralDict *dict = thread->make_object_raw<GeneralDict>();

    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                     .has_exception());
    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(2), Value::from_smi(22))
                     .has_exception());
    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(99))
                     .has_exception());

    EXPECT_EQ(2u, dict->size());
    ASSERT_EQ(2u, dict->entry_storage_size());
    GeneralDict::EntryView first = {Value::not_present(), Value::not_present()};
    GeneralDict::EntryView second = {Value::not_present(),
                                     Value::not_present()};
    ASSERT_TRUE(dict->entry_at(0, first));
    ASSERT_TRUE(dict->entry_at(1, second));
    EXPECT_EQ(Value::from_smi(1), first.key);
    EXPECT_EQ(Value::from_smi(99), first.value);
    EXPECT_EQ(Value::from_smi(2), second.key);
    EXPECT_EQ(Value::from_smi(22), second.value);
}

TEST(GeneralDict, SetItemPropagatesHashExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class C:\n"
                                    L"    def __hash__(self):\n"
                                    L"        raise ValueError\n"
                                    L"d = __clover_general_dict()\n"
                                    L"d[C()] = 1\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(GeneralDict, SetItemPropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class C:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"d = __clover_general_dict()\n"
                                    L"d[C()] = 1\n"
                                    L"d[C()] = 2\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(GeneralDict, GetItemReturnsInsertedIntegerKey)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(), context.run_file(L"d = __clover_general_dict()\n"
                                              L"d[1] = 'one'\n"
                                              L"d[1] == 'one'\n"));
}

TEST(GeneralDict, GetItemRaisesKeyErrorForMissingKey)
{
    test::VmTestContext context;

    Value result = context.run_file(L"d = __clover_general_dict()\n"
                                    L"d[1]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
}

TEST(GeneralDict, ContainsReportsPresentAndMissingKeys)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(), context.run_file(L"d = __clover_general_dict()\n"
                                              L"d[1] = 'one'\n"
                                              L"(1 in d) and not (2 in d)\n"));
}

TEST(GeneralDict, GetItemFindsEqualBoolAndIntKey)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(), context.run_file(L"d = __clover_general_dict()\n"
                                              L"d[True] = 'truthy'\n"
                                              L"d[1] == 'truthy'\n"));
}

TEST(GeneralDict, ContainsFindsEqualBoolAndIntKey)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(), context.run_file(L"d = __clover_general_dict()\n"
                                              L"d[1] = 'one'\n"
                                              L"True in d\n"));
}

TEST(GeneralDict, GetItemPropagatesHashExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class C:\n"
                                    L"    def __hash__(self):\n"
                                    L"        raise ValueError\n"
                                    L"d = __clover_general_dict()\n"
                                    L"d[C()]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(GeneralDict, ContainsPropagatesHashExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class C:\n"
                                    L"    def __hash__(self):\n"
                                    L"        raise ValueError\n"
                                    L"d = __clover_general_dict()\n"
                                    L"C() in d\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(GeneralDict, GetItemPropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class C:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"d = __clover_general_dict()\n"
                                    L"d[C()] = 1\n"
                                    L"d[C()]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(GeneralDict, ContainsPropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class C:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"d = __clover_general_dict()\n"
                                    L"d[C()] = 1\n"
                                    L"C() in d\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(GeneralDict, GetItemRestartsAfterEqualityInsertionResize)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(99),
              context.run_file(L"d = __clover_general_dict()\n"
                               L"mutated = False\n"
                               L"class Stored:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"    def __eq__(self, other):\n"
                               L"        global mutated\n"
                               L"        if not mutated:\n"
                               L"            mutated = True\n"
                               L"            i = 20\n"
                               L"            while i < 40:\n"
                               L"                d[i] = i\n"
                               L"                i = i + 1\n"
                               L"        return True\n"
                               L"class Probe:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"d[Stored()] = 99\n"
                               L"d[Probe()]\n"));
}

TEST(GeneralDict, DelItemRemovesKeyAndPreservesCollidingLookup)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = __clover_general_dict()\n"
                               L"d[1] = 'one'\n"
                               L"d[17] = 'seventeen'\n"
                               L"del d[1]\n"
                               L"len(d) == 1 and not (1 in d) and "
                               L"d[17] == 'seventeen'\n"));
}

TEST(GeneralDict, DelItemRaisesKeyErrorForMissingKey)
{
    test::VmTestContext context;

    Value result = context.run_file(L"d = __clover_general_dict()\n"
                                    L"del d[1]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
}

TEST(GeneralDict, DelItemFindsEqualBoolAndIntKey)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = __clover_general_dict()\n"
                               L"d[True] = 'truthy'\n"
                               L"del d[1]\n"
                               L"len(d) == 0 and not (True in d)\n"));
}

TEST(GeneralDict, DelItemPropagatesHashExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class C:\n"
                                    L"    def __hash__(self):\n"
                                    L"        raise ValueError\n"
                                    L"d = __clover_general_dict()\n"
                                    L"del d[C()]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(GeneralDict, DelItemPropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class C:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"d = __clover_general_dict()\n"
                                    L"d[C()] = 1\n"
                                    L"del d[C()]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(GeneralDict, SetItemReusesTombstoneAndPreservesProbeChain)
{
    test::VmTestContext context;

    EXPECT_EQ(
        Value::True(),
        context.run_file(L"d = __clover_general_dict()\n"
                         L"d[1] = 1\n"
                         L"d[17] = 17\n"
                         L"del d[1]\n"
                         L"d[33] = 33\n"
                         L"len(d) == 2 and d[17] == 17 and d[33] == 33\n"));
}

TEST(GeneralDict, TombstoneResizeStressKeepsRemainingEntriesReachable)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = __clover_general_dict()\n"
                               L"i = 0\n"
                               L"while i < 40:\n"
                               L"    d[i] = i\n"
                               L"    i = i + 1\n"
                               L"i = 0\n"
                               L"while i < 40:\n"
                               L"    del d[i]\n"
                               L"    i = i + 2\n"
                               L"d[100] = 100\n"
                               L"len(d) == 21 and d[1] == 1 and d[39] == 39 "
                               L"and d[100] == 100 and not (2 in d)\n"));
}

TEST(GeneralDict, DelItemRestartsAfterEqualityInsertionResize)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = __clover_general_dict()\n"
                               L"mutated = False\n"
                               L"class Stored:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"    def __eq__(self, other):\n"
                               L"        global mutated\n"
                               L"        if not mutated:\n"
                               L"            mutated = True\n"
                               L"            i = 20\n"
                               L"            while i < 40:\n"
                               L"                d[i] = i\n"
                               L"                i = i + 1\n"
                               L"        return True\n"
                               L"class Probe:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"d[Stored()] = 99\n"
                               L"del d[Probe()]\n"
                               L"len(d) == 20 and d[39] == 39\n"));
}

TEST(GeneralDict, TableGenerationChangesOnlyWhenProbeStructureChanges)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    GeneralDict *dict = thread->make_object_raw<GeneralDict>();

    EXPECT_EQ(0u, dict->table_generation());
    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                     .has_exception());
    EXPECT_EQ(0u, dict->table_generation());
    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(99))
                     .has_exception());
    EXPECT_EQ(0u, dict->table_generation());
    ASSERT_FALSE(dict->del_item(thread, Value::from_smi(1)).has_exception());
    EXPECT_EQ(0u, dict->table_generation());

    dict->clear();
    EXPECT_EQ(1u, dict->table_generation());
    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(2), Value::from_smi(22))
                     .has_exception());
    EXPECT_EQ(1u, dict->table_generation());

    for(int64_t key = 0; key < 20; ++key)
    {
        ASSERT_FALSE(
            dict->set_item(thread, Value::from_smi(key), Value::from_smi(key))
                .has_exception());
    }

    EXPECT_GT(dict->table_generation(), 0u);
}

TEST(GeneralDict, ClearRemovesEntriesAndChangesTableGeneration)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = __clover_general_dict()\n"
                               L"d[1] = 'one'\n"
                               L"d[2] = 'two'\n"
                               L"d.clear()\n"
                               L"len(d) == 0 and not (1 in d) and "
                               L"not (2 in d)\n"));
}

TEST(GeneralDict, SetItemDoesNotReuseTombstoneFilledDuringEquality)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = __clover_general_dict()\n"
                               L"filler = None\n"
                               L"class Tomb:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"class Stored:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"    def __eq__(self, other):\n"
                               L"        global filler\n"
                               L"        if filler is None:\n"
                               L"            filler = Tomb()\n"
                               L"            d[filler] = 'filled'\n"
                               L"        return False\n"
                               L"class New:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"tomb = Tomb()\n"
                               L"stored = Stored()\n"
                               L"d[tomb] = 'deleted'\n"
                               L"d[stored] = 'stored'\n"
                               L"del d[tomb]\n"
                               L"d[New()] = 'new'\n"
                               L"len(d) == 3 and d[filler] == 'filled' and "
                               L"d[stored] == 'stored'\n"));
}

TEST(Dict, SetItemOverwritesExistingValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value key = make_string(context, L"shared");

    set_dict_item(context.thread(), dict, key, Value::from_smi(1));
    set_dict_item(context.thread(), dict, key, Value::from_smi(99));

    EXPECT_EQ(1u, dict->size());
    EXPECT_EQ(Value::from_smi(99), get_dict_item(context.thread(), dict, key));
}

TEST(Dict, SetItemOverwriteEnqueuesOverwrittenObject)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Value key = make_string(context, L"shared");
    Value old_value = make_string(context, L"old-dict");
    Value new_value = make_string(context, L"new-dict");
    Owned<Value> keep_dict(Value::from_oop(dict));
    Owned<Value> keep_new(new_value);
    HeapObject *old_object = old_value.as.ptr;
    HeapObject *new_object = new_value.as.ptr;
    set_dict_item(context.thread(), dict, key, old_value);
    ASSERT_FALSE(thread->zero_count_table_contains_for_testing(old_object));
    ASSERT_EQ(HeapLifecycleState::Normal, old_object->lifecycle_state);

    set_dict_item(context.thread(), dict, key, new_value);

    EXPECT_EQ(new_value, get_dict_item(context.thread(), dict, key));
    EXPECT_EQ(0, old_object->refcount);
    EXPECT_EQ(HeapLifecycleState::InZct, old_object->lifecycle_state);
    EXPECT_TRUE(thread->zero_count_table_contains_for_testing(old_object));
    EXPECT_FALSE(thread->zero_count_table_contains_for_testing(new_object));
}

TEST(Dict, DelItemRemovesKeyFromLookupAndLogicalSize)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value keep = make_string(context, L"keep");
    Value erase = make_string(context, L"erase");

    set_dict_item(context.thread(), dict, keep, Value::from_smi(1));
    set_dict_item(context.thread(), dict, erase, Value::from_smi(2));
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, erase));

    EXPECT_EQ(1u, dict->size());
    EXPECT_TRUE(dict_contains(context.thread(), dict, keep));
    EXPECT_FALSE(dict_contains(context.thread(), dict, erase));
    EXPECT_EQ(Value::from_smi(1), get_dict_item(context.thread(), dict, keep));
    EXPECT_TRUE(
        get_dict_item(context.thread(), dict, erase).is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
}

TEST(Dict, CopyConstructorPreservesLiveEntriesOnly)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value first = make_string(context, L"first");
    Value second = make_string(context, L"second");

    set_dict_item(context.thread(), dict, first, Value::from_smi(10));
    set_dict_item(context.thread(), dict, second, Value::from_smi(20));
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, first));

    Dict copy(context.thread()->class_for_native_layout(Dict::native_layout),
              *dict);

    EXPECT_EQ(1u, copy.size());
    EXPECT_FALSE(dict_contains(context.thread(), &copy, first));
    EXPECT_TRUE(dict_contains(context.thread(), &copy, second));
    EXPECT_EQ(Value::from_smi(20),
              get_dict_item(context.thread(), &copy, second));
    EXPECT_TRUE(
        get_dict_item(context.thread(), &copy, first).is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
}

TEST(Dict, IteratorVisitsLiveEntriesInInsertionOrder)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value first = make_string(context, L"first");
    Value second = make_string(context, L"second");
    Value third = make_string(context, L"third");
    Value fourth = make_string(context, L"fourth");

    set_dict_item(context.thread(), dict, first, Value::from_smi(10));
    set_dict_item(context.thread(), dict, second, Value::from_smi(20));
    set_dict_item(context.thread(), dict, third, Value::from_smi(30));
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, first));
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, third));
    set_dict_item(context.thread(), dict, fourth, Value::from_smi(40));

    Value expected_keys[] = {second, fourth};
    Value expected_values[] = {Value::from_smi(20), Value::from_smi(40)};
    size_t idx = 0;
    for(Dict::EntryView entry: *dict)
    {
        ASSERT_LT(idx, 2u);
        EXPECT_EQ(expected_keys[idx], entry.key);
        EXPECT_EQ(expected_values[idx], entry.value);
        ++idx;
    }
    EXPECT_EQ(2u, idx);
}

TEST(Dict, GrowCompactsDeletedEntriesAndPreservesLiveEntries)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    std::vector<Value> keys;
    keys.reserve(16);
    for(int64_t idx = 0; idx < 16; ++idx)
    {
        keys.push_back(
            make_string(context, (L"key-" + std::to_wstring(idx)).c_str()));
        set_dict_item(context.thread(), dict, keys.back(),
                      Value::from_smi(idx));
    }

    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, keys[0]));
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, keys[2]));
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, keys[4]));
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, keys[6]));

    std::vector<Value> new_keys;
    new_keys.reserve(13);
    for(int64_t idx = 0; idx < 13; ++idx)
    {
        new_keys.push_back(
            make_string(context, (L"new-key-" + std::to_wstring(idx)).c_str()));
        set_dict_item(context.thread(), dict, new_keys.back(),
                      Value::from_smi(100 + idx));
    }

    EXPECT_EQ(25u, dict->size());
    for(size_t idx = 0; idx < keys.size(); ++idx)
    {
        if(idx % 2 == 0 && idx < 8)
        {
            EXPECT_FALSE(dict_contains(context.thread(), dict, keys[idx]));
            continue;
        }
        EXPECT_EQ(Value::from_smi(static_cast<int64_t>(idx)),
                  get_dict_item(context.thread(), dict, keys[idx]));
    }
    for(size_t idx = 0; idx < new_keys.size(); ++idx)
    {
        EXPECT_EQ(Value::from_smi(100 + static_cast<int64_t>(idx)),
                  get_dict_item(context.thread(), dict, new_keys[idx]));
    }

    std::vector<Value> expected_keys = {keys[1],  keys[3],  keys[5],  keys[7],
                                        keys[8],  keys[9],  keys[10], keys[11],
                                        keys[12], keys[13], keys[14], keys[15]};
    expected_keys.insert(expected_keys.end(), new_keys.begin(), new_keys.end());

    size_t idx = 0;
    for(Dict::EntryView entry: *dict)
    {
        ASSERT_LT(idx, expected_keys.size());
        EXPECT_EQ(expected_keys[idx], entry.key);
        ++idx;
    }
    EXPECT_EQ(expected_keys.size(), idx);
}

TEST(Dict, EmptyIteratorEqualsEnd)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    EXPECT_EQ(dict->begin(), dict->end());

    Value key = make_string(context, L"key");
    set_dict_item(context.thread(), dict, key, Value::from_smi(1));
    EXPECT_EQ(Value::None(), del_dict_item(context.thread(), dict, key));

    EXPECT_EQ(dict->begin(), dict->end());
}

TEST(Dict, ClearRemovesAllEntriesAndAllowsReuse)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value alpha = make_string(context, L"alpha");
    Value beta = make_string(context, L"beta");

    set_dict_item(context.thread(), dict, alpha, Value::from_smi(1));
    set_dict_item(context.thread(), dict, beta, Value::from_smi(2));
    dict->clear();

    EXPECT_EQ(0u, dict->size());
    EXPECT_TRUE(dict->empty());
    EXPECT_FALSE(dict_contains(context.thread(), dict, alpha));
    EXPECT_TRUE(
        get_dict_item(context.thread(), dict, alpha).is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
    context.thread()->clear_pending_exception();

    set_dict_item(context.thread(), dict, alpha, Value::from_smi(7));
    EXPECT_EQ(1u, dict->size());
    EXPECT_TRUE(dict_contains(context.thread(), dict, alpha));
    EXPECT_EQ(Value::from_smi(7), get_dict_item(context.thread(), dict, alpha));
}
