#include "builtin_types/dict.h"
#include "builtin_types/list.h"
#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "bytecode/code_object_print.h"
#include "compiler/codegen.h"
#include "object_model/function.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
#include <cassert>
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace cl;

namespace
{
    static Value make_string(test::VmTestContext &context, const wchar_t *text)
    {
        return context.thread()->make_object_value<String>(text).raw_value();
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
        EXPECT_STREQ(
            message,
            cl::test::native_exception_message(exception).extract()->data);
    }

    static Function *dict_method_function(test::VmTestContext &context,
                                          const wchar_t *name)
    {
        TValue<String> method_name =
            context.vm().get_or_create_interned_string_value(name);
        Value method = context.vm().dict_class()->get_own_property(method_name);
        assert(can_convert_to<Function>(method));
        return method.get_ptr<Function>();
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

TEST(Dict, SemanticApiGetItemIfPresentDistinguishesNoneFromMissing)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Value key = make_string(context, L"key");
    Value missing = make_string(context, L"missing");

    ASSERT_FALSE(dict->set_item(thread, key, Value::None()).has_exception());

    Dict::ItemResult found = dict->get_item_if_present(thread, key).value();
    EXPECT_TRUE(found.found);
    EXPECT_EQ(Value::None(), found.value);

    Dict::ItemResult absent =
        dict->get_item_if_present(thread, missing).value();
    EXPECT_FALSE(absent.found);
    EXPECT_EQ(Value::None(), absent.value);
    EXPECT_FALSE(thread->has_pending_exception());
}

TEST(Dict, SemanticApiPopItemIfPresentDistinguishesNoneFromMissing)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::None())
                     .has_exception());

    Dict::ItemResult found =
        dict->pop_item_if_present(thread, Value::True()).value();
    EXPECT_TRUE(found.found);
    EXPECT_EQ(Value::None(), found.value);

    Dict::ItemResult absent =
        dict->pop_item_if_present(thread, Value::from_smi(1)).value();
    EXPECT_FALSE(absent.found);
    EXPECT_EQ(Value::None(), absent.value);
    EXPECT_FALSE(thread->has_pending_exception());
}

TEST(Dict, SemanticApiSetdefaultWithPresenceReportsInsertionAndHit)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    Dict::SetDefaultResult inserted =
        dict->setdefault_with_presence(thread, Value::from_smi(1),
                                       Value::None())
            .value();
    EXPECT_FALSE(inserted.was_present);
    EXPECT_EQ(Value::None(), inserted.value);

    Dict::SetDefaultResult present =
        dict->setdefault_with_presence(thread, Value::True(),
                                       Value::from_smi(2))
            .value();
    EXPECT_TRUE(present.was_present);
    EXPECT_EQ(Value::None(), present.value);
    EXPECT_EQ(1u, dict->size());
}

TEST(Dict, SemanticApiGetItemIfPresentPropagatesHashFailure)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Value bad_hash_key = context.run_file(L"class BadHash:\n"
                                          L"    def __hash__(self):\n"
                                          L"        return 'bad'\n"
                                          L"BadHash()\n");
    ASSERT_FALSE(thread->has_pending_exception());

    EXPECT_TRUE(
        dict->get_item_if_present(thread, bad_hash_key).has_exception());
    expect_pending_exception(thread, L"TypeError",
                             L"__hash__ method should return an integer");
}

TEST(Dict, StringSubclassKeysPromoteEverySemanticEntryPoint)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<Value> key(context.run_file(L"class S(str): pass\nS('key')\n"));
    ASSERT_FALSE(key.value().is_exception_marker());
    Owned<TValue<String>> exact(thread->make_object_value<String>(L"key"));
    Owned<TValue<String>> kept(thread->make_object_value<String>(L"kept"));
    for(int operation = 0; operation < 15; ++operation)
    {
        SCOPED_TRACE(operation);
        Owned<TValue<Dict>> dict(thread->make_object_value<Dict>());
        ASSERT_FALSE(
            dict.extract()
                ->set_item(thread, exact.raw_value(), Value::from_smi(7))
                .has_exception());
        ASSERT_FALSE(
            dict.extract()
                ->set_item(thread, kept.raw_value(), Value::from_smi(9))
                .has_exception());
        ASSERT_EQ(thread->get_exact_dict_string_key_shape(),
                  dict.extract()->get_shape());
        TValue<String> typed = TValue<String>::from_value_assumed(key.value());
        switch(operation)
        {
            case 0:
                ASSERT_FALSE(
                    dict.extract()
                        ->set_item(thread, key.value(), Value::from_smi(8))
                        .has_exception());
                break;
            case 1:
                ASSERT_FALSE(dict.extract()
                                 ->get_item(thread, key.value())
                                 .has_exception());
                break;
            case 2:
                ASSERT_TRUE(
                    dict.extract()->contains(thread, key.value()).value());
                break;
            case 3:
                ASSERT_FALSE(dict.extract()
                                 ->del_item(thread, key.value())
                                 .has_exception());
                break;
            case 4:
                ASSERT_FALSE(
                    dict.extract()->pop(thread, key.value()).has_exception());
                break;
            case 5:
                ASSERT_FALSE(
                    dict.extract()
                        ->setdefault(thread, key.value(), Value::None())
                        .has_exception());
                break;
            case 6:
                ASSERT_FALSE(
                    dict.extract()
                        ->set_item_for_str(thread, typed, Value::from_smi(8))
                        .has_exception());
                break;
            case 7:
                ASSERT_FALSE(dict.extract()
                                 ->get_item_for_str(thread, typed)
                                 .has_exception());
                break;
            case 8:
                ASSERT_TRUE(
                    dict.extract()->contains_for_str(thread, typed).value());
                break;
            case 9:
                ASSERT_FALSE(dict.extract()
                                 ->del_item_for_str(thread, typed)
                                 .has_exception());
                break;
            case 10:
                ASSERT_FALSE(
                    dict.extract()->pop_for_str(thread, typed).has_exception());
                break;
            case 11:
                ASSERT_FALSE(
                    dict.extract()
                        ->setdefault_for_str(thread, typed, Value::None())
                        .has_exception());
                break;
            case 12:
                {
                    Owned<TValue<Dict>> source(
                        thread->make_object_value<Dict>());
                    ASSERT_FALSE(
                        source.extract()
                            ->set_item(thread, key.value(), Value::from_smi(8))
                            .has_exception());
                    ASSERT_FALSE(
                        dict.extract()
                            ->update_from_dict(thread, source.extract())
                            .has_exception());
                    break;
                }
            case 13:
                EXPECT_EQ(TrustedDictBytecodeAccess::PopGeneral,
                          TrustedDictBytecodeAccess::try_string_keyed_pop(
                              thread, dict.extract(), key.value())
                              .status);
                break;
            case 14:
                EXPECT_FALSE(
                    TrustedDictBytecodeAccess::try_string_keyed_setdefault(
                        thread, dict.extract(), key.value(), Value::None())
                        .handled);
                break;
        }
        EXPECT_EQ(thread->get_exact_dict_general_shape(),
                  dict.extract()->get_shape());
        EXPECT_EQ(1u, dict.extract()->table_generation());
        auto value = dict.extract()->get_item(thread, kept.raw_value());
        ASSERT_FALSE(value.has_exception());
        EXPECT_EQ(Value::from_smi(9), value.value());
    }
}

TEST(Dict, StringSubclassHashFailuresPreservePromotionAndContents)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<Value> key(context.run_file(LR"(
class S(str):
    def __hash__(self): raise ValueError("hash failed")
S("key")
)"));
    ASSERT_FALSE(key.value().is_exception_marker());
    Owned<TValue<String>> exact(thread->make_object_value<String>(L"key"));
    for(int operation = 0; operation < 6; ++operation)
    {
        SCOPED_TRACE(operation);
        Owned<TValue<Dict>> dict(thread->make_object_value<Dict>());
        ASSERT_FALSE(
            dict.extract()
                ->set_item(thread, exact.raw_value(), Value::from_smi(7))
                .has_exception());
        switch(operation)
        {
            case 0:
                EXPECT_TRUE(dict.extract()
                                ->set_item(thread, key.value(), Value::None())
                                .has_exception());
                break;
            case 1:
                EXPECT_TRUE(dict.extract()
                                ->get_item(thread, key.value())
                                .has_exception());
                break;
            case 2:
                EXPECT_TRUE(dict.extract()
                                ->contains(thread, key.value())
                                .has_exception());
                break;
            case 3:
                EXPECT_TRUE(dict.extract()
                                ->del_item(thread, key.value())
                                .has_exception());
                break;
            case 4:
                EXPECT_TRUE(
                    dict.extract()->pop(thread, key.value()).has_exception());
                break;
            case 5:
                EXPECT_TRUE(dict.extract()
                                ->setdefault(thread, key.value(), Value::None())
                                .has_exception());
                break;
        }
        expect_pending_exception(thread, L"ValueError", L"hash failed");
        thread->clear_pending_exception();
        EXPECT_EQ(thread->get_exact_dict_general_shape(),
                  dict.extract()->get_shape());
        EXPECT_EQ(1u, dict.extract()->table_generation());
        EXPECT_EQ(1u, dict.extract()->size());
        auto value = dict.extract()->get_item(thread, exact.raw_value());
        ASSERT_FALSE(value.has_exception());
        EXPECT_EQ(Value::from_smi(7), value.value());
    }
}

TEST(Dict, StringSubclassProtocolsAndWarmedCaches)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
events = []
class Key(str):
    def __hash__(self):
        events.append("hash")
        return str.__hash__(self)
    def __eq__(self, other):
        events.append("eq")
        return False
key = Key("key")
# Same string payload and hash, but the subclass comparison says unequal.
d = {"key": 1, key: 2}
assert len(d) == 2
assert "hash" in events
assert "eq" in events
assert d["key"] == 1
assert d[key] == 2
assert d.copy()[key] == 2
class DifferentHash(str):
    def __hash__(self): return str.__hash__(self) ^ 1
other = DifferentHash("key")
d = {"key": 1, other: 2}
assert len(d) == 2
assert d[other] == 2
class EqualKey(str):
    def __hash__(self):
        events.append("hash")
        return str.__hash__(self)
    def __eq__(self, other):
        events.append("eq")
        return str.__eq__(self, other)
equal = EqualKey("key")
def exercise(d, key, mode):
    if mode == 0: return d[key]
    if mode == 1: return key in d
    if mode == 2: return d.get(key)
    if mode == 3: return d.setdefault(key, 8)
    if mode == 4: return d.pop(key)
    if mode == 5:
        d[key] = 8
        return None
    del d[key]
for mode in range(7):
    for repeat in range(3):
        exercise({"key": 7}, "key", mode)
    events.clear()
    d = {"key": 7}
    exercise(d, equal, mode)
    assert "hash" in events
    assert "eq" in events
    # Exact string keys must still work after promotion.
    if mode == 4 or mode == 6:
        assert len(d) == 0
    else:
        assert d["key"] == 7 or d["key"] == 8
class Explodes(str):
    def __hash__(self): return str.__hash__(self)
    def __eq__(self, other): raise ValueError("comparison failed")
d = {"key": 7}
try:
    d.setdefault(Explodes("key"), 9)
except ValueError as error:
    assert error.args[0] == "comparison failed"
else:
    raise AssertionError
assert d["key"] == 7
assert len(d) == 1
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Dict, SemanticApiLookupAndContainsPromoteNonStringMisses)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    EXPECT_TRUE(dict->get_item(thread, Value::from_smi(1)).has_exception());
    expect_pending_exception(thread, L"KeyError", L"");
    thread->clear_pending_exception();

    EXPECT_FALSE(dict->contains(thread, Value::from_smi(1)).value());
    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
}

TEST(Dict, SemanticApiDeleteAndPopPromoteNonStringMisses)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    EXPECT_TRUE(dict->del_item(thread, Value::from_smi(1)).has_exception());
    expect_pending_exception(thread, L"KeyError", L"");
    thread->clear_pending_exception();

    EXPECT_TRUE(dict->pop(thread, Value::from_smi(1)).has_exception());
    expect_pending_exception(thread, L"KeyError", L"");
    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
}

TEST(Dict, SemanticApiLookupKeepsPromotionWhenHashFails)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    Value bad_hash_key = context.run_file(L"class BadHash:\n"
                                          L"    def __hash__(self):\n"
                                          L"        return 'bad'\n"
                                          L"BadHash()\n");
    ASSERT_FALSE(thread->has_pending_exception());

    EXPECT_TRUE(dict->get_item(thread, bad_hash_key).has_exception());
    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
    expect_pending_exception(thread, L"TypeError",
                             L"__hash__ method should return an integer");
}

TEST(Dict, SemanticApiSetItemPromotesNonStringKeys)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    ASSERT_EQ(string_key_shape, dict->get_shape());
    EXPECT_EQ(0u, dict->table_generation());

    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                     .has_exception());

    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
    EXPECT_EQ(1u, dict->size());

    Dict::EntryView entry = {Value::not_present(), Value::not_present()};
    ASSERT_TRUE(dict->entry_at(0, entry));
    EXPECT_EQ(Value::from_smi(1), entry.key);
    EXPECT_EQ(Value::from_smi(11), entry.value);
}

TEST(Dict, PublicAssignmentPromotesNonStringKeys)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(2), context.run_file(L"d = {}\n"
                                                   L"d[1] = 'one'\n"
                                                   L"d['two'] = 2\n"
                                                   L"len(d)\n"));
}

TEST(Dict, SemanticApiSetItemKeepsStringShapeForStringKeys)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();
    Value key = make_string(context, L"key");

    ASSERT_FALSE(
        dict->set_item(thread, key, Value::from_smi(42)).has_exception());

    EXPECT_EQ(string_key_shape, dict->get_shape());
    EXPECT_EQ(0u, dict->table_generation());
    EXPECT_EQ(Value::from_smi(42), dict->get_item(thread, key).value());
}

TEST(Dict, SemanticApiSetItemUsesGeneralSemanticsAfterPromotion)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                     .has_exception());
    Owned<TValue<String>> string_key(
        thread->make_object_value<String>(L"later"));
    ASSERT_FALSE(
        dict->set_item(thread, string_key.raw_value(), Value::from_smi(33))
            .has_exception());

    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(2u, dict->size());

    Dict::EntryView first = {Value::not_present(), Value::not_present()};
    Dict::EntryView second = {Value::not_present(), Value::not_present()};
    ASSERT_TRUE(dict->entry_at(0, first));
    ASSERT_TRUE(dict->entry_at(1, second));
    EXPECT_EQ(Value::from_smi(1), first.key);
    EXPECT_EQ(Value::from_smi(11), first.value);
    EXPECT_EQ(string_key.raw_value(), second.key);
    EXPECT_EQ(Value::from_smi(33), second.value);
}

TEST(Dict, SemanticApiLookupUsesGeneralSemanticsAfterPromotion)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                     .has_exception());

    EXPECT_EQ(Value::from_smi(11),
              dict->get_item(thread, Value::from_smi(1)).value());
    EXPECT_EQ(Value::from_smi(11),
              dict->get_item(thread, Value::True()).value());
    EXPECT_TRUE(dict->contains(thread, Value::from_smi(1)).value());
    EXPECT_TRUE(dict->contains(thread, Value::True()).value());
    EXPECT_FALSE(dict->contains(thread, Value::from_smi(2)).value());
}

TEST(Dict, PublicLookupAndContainsPromotedNonStringKeys)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"d[1] = 'one'\n"
                               L"d[1] == 'one' and d.get(1) == 'one' and "
                               L"1 in d and True in d and not (2 in d)\n"));
}

TEST(Dict, PublicGetUsesSingleGeneralLookup)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"calls = 0\n"
                               L"class Stored:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"    def __eq__(self, other):\n"
                               L"        global calls\n"
                               L"        calls = calls + 1\n"
                               L"        if calls > 1:\n"
                               L"            raise ValueError\n"
                               L"        return True\n"
                               L"class Probe:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"d[Stored()] = 'value'\n"
                               L"d.get(Probe()) == 'value' and calls == 1\n"));
}

TEST(Dict, PublicGetPropagatesEqualityKeyError)
{
    test::VmTestContext context;

    Value result = context.run_file(L"d = {}\n"
                                    L"class Stored:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise KeyError\n"
                                    L"class Probe:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"d[Stored()] = 'value'\n"
                                    L"d.get(Probe(), 'fallback')\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
}

TEST(Dict, PublicNonStringLookupMisses)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"d.get(1, 'fallback') == 'fallback' and "
                               L"not (1 in d)\n"));

    Value result = context.run_file(L"d = {}\n"
                                    L"d[1]\n");
    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
}

TEST(Dict, PromotionDoesNotInvalidateExistingIterator)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(), context.run_file(L"d = {'key': 1}\n"
                                              L"iterator = iter(d.keys())\n"
                                              L"d.get(1)\n"
                                              L"next(iterator) == 'key'\n"));
}

TEST(Dict, TrustedStringLookupFallsBackAfterPromotion)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(2), context.run_file(L"def read(d):\n"
                                                   L"    return d['key']\n"
                                                   L"d = {'key': 1}\n"
                                                   L"first = read(d)\n"
                                                   L"d.get(1)\n"
                                                   L"first + read(d)\n"));
}

TEST(Dict, DisplayEvaluatesExpressionsBeforeHashingAndPromotes)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"events = []\n"
                               L"class Key:\n"
                               L"    def __init__(self, name, hash_value):\n"
                               L"        self.name = name\n"
                               L"        self.hash_value = hash_value\n"
                               L"    def __hash__(self):\n"
                               L"        events.append('hash ' + self.name)\n"
                               L"        return self.hash_value\n"
                               L"def key(name, hash_value):\n"
                               L"    events.append('key ' + name)\n"
                               L"    return Key(name, hash_value)\n"
                               L"def value(name):\n"
                               L"    events.append('value ' + name)\n"
                               L"    return name\n"
                               L"d = {key('a', 4): value('a'), "
                               L"key('b', 5): value('b')}\n"
                               L"len(d) == 2 and len(events) == 6 and "
                               L"events[0] == 'key a' and "
                               L"events[1] == 'value a' and "
                               L"events[2] == 'key b' and "
                               L"events[3] == 'value b' and "
                               L"events[4] == 'hash a' and "
                               L"events[5] == 'hash b'\n"));
}

TEST(Dict, MixedDisplayPreservesPairsAcrossHashCalls)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {'a': 1, 2: 'two', 'b': 3}\n"
                               L"len(d) == 3 and d['a'] == 1 and "
                               L"d[2] == 'two' and d['b'] == 3\n"));
}

TEST(Dict, SemanticApiDeleteUsesGeneralSemanticsAfterPromotion)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                     .has_exception());

    EXPECT_EQ(Value::from_smi(11), dict->pop(thread, Value::True()).value());
    EXPECT_EQ(0u, dict->size());

    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                     .has_exception());
    ASSERT_FALSE(dict->del_item(thread, Value::True()).has_exception());
    EXPECT_EQ(0u, dict->size());
    EXPECT_FALSE(dict->contains(thread, Value::from_smi(1)).value());
}

TEST(Dict, PublicDeleteAndPopPromotedNonStringKeys)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"d[1] = 'one'\n"
                               L"popped = d.pop(True)\n"
                               L"after_pop = len(d) == 0 and not (1 in d)\n"
                               L"d[1] = 'one'\n"
                               L"del d[True]\n"
                               L"popped == 'one' and after_pop and "
                               L"len(d) == 0 and not (1 in d)\n"));
}

TEST(Dict, PublicPopReturnsDefaultOnlyWhenProvided)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(), context.run_file(L"d = {}\n"
                                              L"d.pop('missing', None) "
                                              L"is None\n"));

    Value result = context.run_file(L"d = {}\n"
                                    L"d.pop('missing')\n");
    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
}

TEST(Dict, PublicStringPopKeepsStringKeyShape)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Shape *string_key_shape =
        context.thread()->get_exact_dict_string_key_shape();

    Value result = context.run_file(L"value = []\n"
                                    L"d = {'key': value}\n"
                                    L"popped = d.pop('key')\n"
                                    L"popped.append(11)\n"
                                    L"assert len(popped) == 1\n"
                                    L"assert popped[0] == 11\n"
                                    L"assert d.pop('missing', 22) == 22\n"
                                    L"d\n");

    ASSERT_TRUE(can_convert_to<Dict>(result));
    EXPECT_EQ(string_key_shape, result.get_ptr<Dict>()->get_shape());
    EXPECT_EQ(0u, result.get_ptr<Dict>()->size());
}

TEST(Dict, PublicCopyPreservesPromotedNonStringKeys)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"d[1] = 'one'\n"
                               L"copy = d.copy()\n"
                               L"copy[1] == 'one' and len(copy) == 1\n"));
}

TEST(Dict, PublicPopitemRemovesPromotedNonStringKey)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"d[1] = 'one'\n"
                               L"item = d.popitem()\n"
                               L"item[0] == 1 and item[1] == 'one' and "
                               L"len(d) == 0\n"));
}

TEST(Dict, SemanticApiSetItemKeepsPromotionWhenHashFails)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    Value bad_hash_key = context.run_file(L"class BadHash:\n"
                                          L"    def __hash__(self):\n"
                                          L"        return 'bad'\n"
                                          L"BadHash()\n");
    ASSERT_FALSE(thread->has_pending_exception());

    EXPECT_TRUE(dict->set_item(thread, bad_hash_key, Value::from_smi(1))
                    .has_exception());

    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
    EXPECT_EQ(0u, dict->size());
    expect_pending_exception(thread, L"TypeError",
                             L"__hash__ method should return an integer");
}

TEST(Dict, PublicAssignmentKeepsPromotionWhenHashFails)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    Value result = context.run_file(L"d = {}\n"
                                    L"class BadHash:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 'bad'\n"
                                    L"try:\n"
                                    L"    d[BadHash()] = 1\n"
                                    L"except TypeError:\n"
                                    L"    pass\n"
                                    L"d\n");

    ASSERT_TRUE(can_convert_to<Dict>(result));
    Dict *dict = result.get_ptr<Dict>();
    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
    EXPECT_EQ(0u, dict->size());
}

TEST(Dict, SemanticApiSetItemPropagatesEqualityExceptionAfterPromotion)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    Owned<Value> keys_value(context.run_file(L"class Stored:\n"
                                             L"    def __hash__(self):\n"
                                             L"        return 7\n"
                                             L"    def __eq__(self, other):\n"
                                             L"        raise ValueError\n"
                                             L"class Probe:\n"
                                             L"    def __hash__(self):\n"
                                             L"        return 7\n"
                                             L"(Stored(), Probe())\n"));
    ASSERT_FALSE(thread->has_pending_exception());
    ASSERT_TRUE(can_convert_to<Tuple>(keys_value.value()));
    Tuple *keys = keys_value.value().get_ptr<Tuple>();
    Value stored_key = keys->item_unchecked(0);
    Value probe_key = keys->item_unchecked(1);

    ASSERT_FALSE(
        dict->set_item(thread, stored_key, Value::from_smi(1)).has_exception());
    EXPECT_TRUE(
        dict->set_item(thread, probe_key, Value::from_smi(2)).has_exception());

    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
    EXPECT_EQ(1u, dict->size());

    Dict::EntryView entry = {Value::not_present(), Value::not_present()};
    ASSERT_TRUE(dict->entry_at(0, entry));
    EXPECT_EQ(stored_key, entry.key);
    EXPECT_EQ(Value::from_smi(1), entry.value);
    expect_pending_exception(thread, L"ValueError", L"");
}

TEST(Dict, PublicAssignmentPropagatesEqualityExceptionAfterPromotion)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"class Stored:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"    def __eq__(self, other):\n"
                               L"        raise ValueError\n"
                               L"class Probe:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"stored = Stored()\n"
                               L"d[stored] = 1\n"
                               L"caught = False\n"
                               L"try:\n"
                               L"    d[Probe()] = 2\n"
                               L"except ValueError:\n"
                               L"    caught = True\n"
                               L"caught and len(d) == 1 and d[stored] == 1\n"));
}

TEST(Dict, PublicLookupPropagatesHashExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class BadHash:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 'bad'\n"
                                    L"d = {}\n"
                                    L"d[1] = 'one'\n"
                                    L"d[BadHash()]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"__hash__ method should return an integer");
}

TEST(Dict, PublicContainsPropagatesHashExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class BadHash:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 'bad'\n"
                                    L"d = {}\n"
                                    L"d[1] = 'one'\n"
                                    L"BadHash() in d\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"__hash__ method should return an integer");
}

TEST(Dict, PublicLookupPropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class Stored:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"class Probe:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"d = {}\n"
                                    L"d[Stored()] = 1\n"
                                    L"d[Probe()]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(Dict, PublicContainsPropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class Stored:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"class Probe:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"d = {}\n"
                                    L"d[Stored()] = 1\n"
                                    L"Probe() in d\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(Dict, PublicLookupRestartsAfterEqualityInsertionResize)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(99),
              context.run_file(L"d = {}\n"
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

TEST(Dict, PublicContainsRestartsAfterEqualityInsertionResize)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(), context.run_file(L"d = {}\n"
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
                                              L"Probe() in d\n"));
}

TEST(Dict, PublicDeletePropagatesHashExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class BadHash:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 'bad'\n"
                                    L"d = {}\n"
                                    L"d[1] = 'one'\n"
                                    L"del d[BadHash()]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"__hash__ method should return an integer");
}

TEST(Dict, PublicPopPropagatesHashExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class BadHash:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 'bad'\n"
                                    L"d = {}\n"
                                    L"d[1] = 'one'\n"
                                    L"d.pop(BadHash())\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"__hash__ method should return an integer");
}

TEST(Dict, PublicDeletePropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class Stored:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"class Probe:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"d = {}\n"
                                    L"d[Stored()] = 1\n"
                                    L"del d[Probe()]\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(Dict, PublicPopPropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class Stored:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"class Probe:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"d = {}\n"
                                    L"d[Stored()] = 1\n"
                                    L"d.pop(Probe())\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(Dict, PublicDeleteRestartsAfterEqualityInsertionResize)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
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

TEST(Dict, PublicPopRestartsAfterEqualityInsertionResize)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
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
                               L"popped = d.pop(Probe())\n"
                               L"popped == 99 and len(d) == 20 and "
                               L"d[39] == 39\n"));
}

TEST(Dict, SemanticApiSetdefaultPromotesNonStringMiss)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    EXPECT_EQ(Value::from_smi(11),
              dict->setdefault(thread, Value::from_smi(1), Value::from_smi(11))
                  .value());

    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
    EXPECT_EQ(1u, dict->size());

    Dict::EntryView entry = {Value::not_present(), Value::not_present()};
    ASSERT_TRUE(dict->entry_at(0, entry));
    EXPECT_EQ(Value::from_smi(1), entry.key);
    EXPECT_EQ(Value::from_smi(11), entry.value);
}

TEST(Dict, SemanticApiSetdefaultDoesNotOverwritePromotedHit)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();

    ASSERT_FALSE(dict->set_item(thread, Value::from_smi(1), Value::from_smi(11))
                     .has_exception());

    EXPECT_EQ(
        Value::from_smi(11),
        dict->setdefault(thread, Value::True(), Value::from_smi(22)).value());
    EXPECT_EQ(1u, dict->size());

    Dict::EntryView entry = {Value::not_present(), Value::not_present()};
    ASSERT_TRUE(dict->entry_at(0, entry));
    EXPECT_EQ(Value::from_smi(1), entry.key);
    EXPECT_EQ(Value::from_smi(11), entry.value);
}

TEST(Dict, PublicSetdefaultPromotesNonStringMiss)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(1), context.run_file(L"d = {}\n"
                                                   L"d.setdefault(1, 'one')\n"
                                                   L"len(d)\n"));
}

TEST(Dict, PublicSetdefaultUsesNoneWhenDefaultIsOmitted)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(), context.run_file(L"d = {}\n"
                                              L"result = d.setdefault(1)\n"
                                              L"result is None and "
                                              L"d[1] is None\n"));
}

TEST(Dict, PublicStringSetdefaultKeepsStringKeyShape)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Shape *string_key_shape =
        context.thread()->get_exact_dict_string_key_shape();

    Value result = context.run_file(L"d = {}\n"
                                    L"assert d.setdefault('key', 11) == 11\n"
                                    L"assert d.setdefault('key', 22) == 11\n"
                                    L"d\n");

    ASSERT_TRUE(can_convert_to<Dict>(result));
    EXPECT_EQ(string_key_shape, result.get_ptr<Dict>()->get_shape());
    EXPECT_EQ(1u, result.get_ptr<Dict>()->size());
}

TEST(Dict, PublicSetdefaultUsesSingleGeneralLookup)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"calls = 0\n"
                               L"class Stored:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"    def __eq__(self, other):\n"
                               L"        global calls\n"
                               L"        calls = calls + 1\n"
                               L"        if calls > 1:\n"
                               L"            raise ValueError\n"
                               L"        return False\n"
                               L"class Probe:\n"
                               L"    def __hash__(self):\n"
                               L"        return 7\n"
                               L"d[Stored()] = 'stored'\n"
                               L"d.setdefault(Probe(), 'new') == 'new' and "
                               L"calls == 1 and len(d) == 2\n"));
}

TEST(Dict, PublicSetdefaultPropagatesHashExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class BadHash:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 'bad'\n"
                                    L"d = {}\n"
                                    L"d.setdefault(BadHash(), 1)\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"__hash__ method should return an integer");
}

TEST(Dict, PublicSetdefaultPropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class Stored:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"class Probe:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"d = {}\n"
                                    L"d[Stored()] = 1\n"
                                    L"d.setdefault(Probe(), 2)\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(Dict, SemanticApiUpdatePromotesFromNonStringSourceKey)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *target = thread->make_object_raw<Dict>();
    Dict *source = thread->make_object_raw<Dict>();
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    ASSERT_FALSE(
        source->set_item(thread, Value::from_smi(1), Value::from_smi(11))
            .has_exception());

    ASSERT_FALSE(target->update_from_dict(thread, source).has_exception());

    EXPECT_NE(string_key_shape, target->get_shape());
    EXPECT_EQ(1u, target->size());

    Dict::EntryView entry = {Value::not_present(), Value::not_present()};
    ASSERT_TRUE(target->entry_at(0, entry));
    EXPECT_EQ(Value::from_smi(1), entry.key);
    EXPECT_EQ(Value::from_smi(11), entry.value);
}

TEST(Dict, PublicUpdateAcceptsMappingsPairIterablesAndKeywords)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"class Mapping:\n"
                               L"    def keys(self):\n"
                               L"        return ['mapped']\n"
                               L"    def __getitem__(self, key):\n"
                               L"        return 1\n"
                               L"d = {}\n"
                               L"d.update(Mapping())\n"
                               L"d.update([['paired', 2]], keyword=3)\n"
                               L"len(d) == 3 and d['mapped'] == 1 and "
                               L"d['paired'] == 2 and d['keyword'] == 3\n"));
}

TEST(Dict, PublicDictConstructorAcceptsMappingsPairIterablesAndKeywords)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"class Mapping:\n"
                               L"    def keys(self):\n"
                               L"        return ['mapped']\n"
                               L"    def __getitem__(self, key):\n"
                               L"        return 1\n"
                               L"mapped = dict(Mapping(), keyword=2)\n"
                               L"paired = dict([['paired', 3]], keyword=4)\n"
                               L"len(mapped) == 2 and mapped['mapped'] == 1 "
                               L"and mapped['keyword'] == 2 and "
                               L"len(paired) == 2 and paired['paired'] == 3 "
                               L"and paired['keyword'] == 4\n"));
}

TEST(Dict, PublicUpdateProcessesPositionalInputBeforeKeywords)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"d.update([['b', 1], ['a', 0]], a=2)\n"
                               L"str(d) == \"{'b': 1, 'a': 2}\"\n"));
}

TEST(Dict, PublicUpdateEvaluatesCallArgumentsBeforeReadingMapping)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"events = []\n"
                               L"def keyword_value():\n"
                               L"    events.append('keyword')\n"
                               L"    return 2\n"
                               L"class Mapping:\n"
                               L"    def keys(self):\n"
                               L"        events.append('keys')\n"
                               L"        return ['mapped']\n"
                               L"    def __getitem__(self, key):\n"
                               L"        events.append('getitem')\n"
                               L"        return 1\n"
                               L"d = {}\n"
                               L"d.update(Mapping(), keyword=keyword_value())\n"
                               L"len(events) == 3 and "
                               L"events[0] == 'keyword' and "
                               L"events[1] == 'keys' and "
                               L"events[2] == 'getitem'\n"));
}

TEST(Dict, PublicUpdateRejectsInvalidInputs)
{
    const wchar_t *expressions[] = {
        L"{}.update(None)\n", L"{}.update([], [])\n",
        L"{}.update([['only-one']])\n", L"{}.update([['one', 2, 'three']])\n"};
    const wchar_t *exception_names[] = {L"TypeError", L"TypeError",
                                        L"ValueError", L"ValueError"};
    const wchar_t *messages[] = {L"object is not iterable", L"", L"", L""};

    for(size_t idx = 0; idx < std::size(expressions); ++idx)
    {
        test::VmTestContext context;
        Value result = context.run_file(expressions[idx]);
        EXPECT_TRUE(result.is_exception_marker());
        expect_pending_exception(context.thread(), exception_names[idx],
                                 messages[idx]);
    }
}

TEST(Dict, PublicDictConstructorRejectsInvalidInputs)
{
    const wchar_t *expressions[] = {L"dict(None)\n", L"dict([], [])\n"};
    const wchar_t *messages[] = {L"object is not iterable", L""};

    for(size_t idx = 0; idx < std::size(expressions); ++idx)
    {
        test::VmTestContext context;
        Value result = context.run_file(expressions[idx]);
        EXPECT_TRUE(result.is_exception_marker());
        expect_pending_exception(context.thread(), L"TypeError", messages[idx]);
    }
}

TEST(Dict, PublicUpdatePropagatesInsertionExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class BadHash:\n"
                                    L"    def __hash__(self):\n"
                                    L"        raise ValueError\n"
                                    L"{}.update([[BadHash(), 1]])\n");
    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(Dict, PublicUpdatePropagatesEqualityExceptions)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class Stored:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"    def __eq__(self, other):\n"
                                    L"        raise ValueError\n"
                                    L"class Probe:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 7\n"
                                    L"d = {}\n"
                                    L"d[Stored()] = 1\n"
                                    L"d.update([[Probe(), 2]])\n");
    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"ValueError", L"");
}

TEST(Dict, PublicUpdateMergesEqualKeys)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"d.update([[1, 'one'], [True, 'true']])\n"
                               L"len(d) == 1 and d[1] == 'true'\n"));
}

TEST(Dict, ConstructorPreservesRequestedSubclassAndPayload)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class D(dict): pass
class E(D): pass
class Sibling(dict): pass
class Mixed(E, Sibling): pass
class Mapping:
    def keys(self): return ("key",)
    def __getitem__(self, key): return 7
for cls in (dict, D, E, Mixed):
    empty = cls()
    assert empty.__class__ is cls
    assert len(empty) == 0
    for source in ({"key": 7}, (("key", 7),), [("key", 7)], Mapping()):
        value = cls(source, key=9, extra=11)
        assert value.__class__ is cls
        assert len(value) == 2
        assert value["key"] == 9
        assert value["extra"] == 11
        value[1] = "integer key"
        assert value[1] == "integer key"
        assert value.get("key") == 9
        assert value.copy().__class__ is dict
        assert value.pop(1) == "integer key"
        if cls is not dict:
            value.attribute = 42
            assert value.__dict__["attribute"] == 42
            assert "attribute" not in value
    value = cls(cls=1, self=2, args=3, source=4)
    assert value.__class__ is cls
    assert value["cls"] == 1
    assert value["self"] == 2
    assert value["args"] == 3
    assert value["source"] == 4
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Dict, ConstructorAndUpdateBypassSubclassInsertionOverrides)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class D(dict):
    def update(self, source): raise AssertionError
    def __setitem__(self, key, value): raise AssertionError
value = D((("first", 1),), second=2)
assert value["first"] == 1
assert value["second"] == 2
assert dict.update(value, third=3) is None
assert value["third"] == 3
assert dict.update(value, {"fourth": 4}) is None
assert value["fourth"] == 4
assert dict.update(value, [("fifth", 5)]) is None
assert value["fifth"] == 5
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Dict, ConstructorPrivateDependenciesSurviveBuiltinRebinding)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
import builtins
Dict = dict
IsInstance = isinstance
Len = len
HasAttr = hasattr
Iter = iter
Next = next
class D(Dict): pass
builtins.dict = None
builtins.isinstance = None
builtins.len = None
builtins.hasattr = None
builtins.iter = None
builtins.next = None
try:
    value = D((("first", 1),), second=2)
    assert value.__class__ is D
    assert value["first"] == 1
    assert value["second"] == 2
    Dict.update(value, third=3)
    assert value["third"] == 3
finally:
    builtins.dict = Dict
    builtins.isinstance = IsInstance
    builtins.len = Len
    builtins.hasattr = HasAttr
    builtins.iter = Iter
    builtins.next = Next
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Dict, ConstructorRejectsArgumentsAndPropagatesSourceErrors)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class D(dict): pass
for mode in range(5):
    try:
        if mode == 0: D({}, {})
        elif mode == 1: D(None)
        elif mode == 2: D(((1,),))
        elif mode == 3: D(((1, 2, 3),))
        else: dict.__new__(list)
    except TypeError:
        pass
    except ValueError:
        pass
    else:
        raise AssertionError
class BadMapping:
    def keys(self): return ("bad",)
    def __getitem__(self, key): raise ValueError("source error")
try:
    D(BadMapping())
except ValueError as error:
    assert error.args[0] == "source error"
else:
    raise AssertionError
value = D()
try:
    dict.update(value, (("good", 1), ("bad",)))
except ValueError:
    pass
else:
    raise AssertionError
assert value["good"] == 1
assert len(value) == 1
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Dict, ConstructorInheritedNewInvalidationAndCombinedBoundary)
{
    test::VmTestContext context;
    Value result = context.run_file(LR"(
class D(dict): pass
class E(D): pass
assert E(key=1).__class__ is E
def replacement(cls, value=7): return value
D.__new__ = replacement
assert E() == 7
assert E(value=9) == 9
del D.__new__
value = E(key=11)
assert value.__class__ is E
assert value["key"] == 11
class Combined(dict):
    def __init__(self): raise AssertionError
try:
    Combined()
except TypeError:
    pass
else:
    raise AssertionError
)");
    ASSERT_FALSE(result.is_exception_marker());
}

TEST(Dict, PythonDictBootstrapHelpersAreHidden)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::False(),
              context.run_file(L"hasattr(__import__('builtins'), "
                               L"'_clover_dict_update') or "
                               L"hasattr(__import__('builtins'), "
                               L"'_clover_dict_new')\n"));
}

TEST(Dict, PublicUpdatePromotesFromNonStringSourceKey)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(1), context.run_file(L"target = {}\n"
                                                   L"source = {}\n"
                                                   L"source[1] = 'one'\n"
                                                   L"target.update(source)\n"
                                                   L"len(target)\n"));
}

TEST(Dict, SemanticApiUpdateReusesStoredSourceHashes)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *target = thread->make_object_raw<Dict>();
    Dict *source = thread->make_object_raw<Dict>();
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    Owned<TValue<String>> good_key(thread->make_object_value<String>(L"good"));
    Owned<Value> bad_hash_key(context.run_file(L"hash_count = 0\n"
                                               L"class MaybeBadHash:\n"
                                               L"    def __hash__(self):\n"
                                               L"        global hash_count\n"
                                               L"        hash_count = "
                                               L"hash_count + 1\n"
                                               L"        if hash_count > 1:\n"
                                               L"            return 'bad'\n"
                                               L"        return 42\n"
                                               L"MaybeBadHash()\n"));
    ASSERT_FALSE(thread->has_pending_exception());

    ASSERT_FALSE(
        source->set_item(thread, good_key.raw_value(), Value::from_smi(1))
            .has_exception());
    ASSERT_FALSE(
        source->set_item(thread, bad_hash_key.value(), Value::from_smi(2))
            .has_exception());

    EXPECT_FALSE(target->update_from_dict(thread, source).has_exception());

    EXPECT_NE(string_key_shape, target->get_shape());
    EXPECT_EQ(2u, target->size());
    EXPECT_EQ(Value::from_smi(1),
              target->get_item(thread, good_key.raw_value()).value());
    bool found_bad_hash_key = false;
    for(size_t idx = 0; idx < target->entry_storage_size(); ++idx)
    {
        Dict::EntryView entry = {Value::not_present(), Value::not_present()};
        if(target->entry_at(idx, entry) && entry.key == bad_hash_key.value())
        {
            found_bad_hash_key = true;
            EXPECT_EQ(Value::from_smi(2), entry.value);
        }
    }
    EXPECT_TRUE(found_bad_hash_key);
    EXPECT_FALSE(thread->has_pending_exception());
}

TEST(Dict, FromTupleKeysPromotesForNonStringKeys)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    Owned<TValue<Tuple>> keys(thread->make_object_value<Tuple>(2));
    Owned<TValue<String>> string_key(thread->make_object_value<String>(L"a"));
    keys.extract()->initialize_item_unchecked(0, Value::from_smi(1));
    keys.extract()->initialize_item_unchecked(1, string_key.raw_value());

    Value result = Dict::from_tuple_keys(keys.extract(), Value::from_smi(7));

    ASSERT_FALSE(result.is_exception_marker());
    ASSERT_TRUE(can_convert_to<Dict>(result));
    Dict *dict = result.get_ptr<Dict>();
    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
    EXPECT_EQ(2u, dict->size());

    Dict::EntryView first = {Value::not_present(), Value::not_present()};
    Dict::EntryView second = {Value::not_present(), Value::not_present()};
    ASSERT_TRUE(dict->entry_at(0, first));
    ASSERT_TRUE(dict->entry_at(1, second));
    EXPECT_EQ(Value::from_smi(1), first.key);
    EXPECT_EQ(Value::from_smi(7), first.value);
    EXPECT_EQ(string_key.raw_value(), second.key);
    EXPECT_EQ(Value::from_smi(7), second.value);
}

TEST(Dict, FromListKeysPromotesForNonStringKeys)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    Owned<TValue<List>> keys(thread->make_object_value<List>());
    keys.extract()->append(Value::from_smi(1));
    keys.extract()->append(Value::True());

    Value result = Dict::from_list_keys(keys.extract(), Value::from_smi(7));

    ASSERT_FALSE(result.is_exception_marker());
    ASSERT_TRUE(can_convert_to<Dict>(result));
    Dict *dict = result.get_ptr<Dict>();
    EXPECT_NE(string_key_shape, dict->get_shape());
    EXPECT_EQ(1u, dict->table_generation());
    EXPECT_EQ(1u, dict->size());

    Dict::EntryView entry = {Value::not_present(), Value::not_present()};
    ASSERT_TRUE(dict->entry_at(0, entry));
    EXPECT_EQ(Value::from_smi(1), entry.key);
    EXPECT_EQ(Value::from_smi(7), entry.value);
}

TEST(Dict, FromTupleKeysKeepsStringShapeForStringKeys)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Shape *string_key_shape = thread->get_exact_dict_string_key_shape();

    Owned<TValue<Tuple>> keys(thread->make_object_value<Tuple>(1));
    Owned<TValue<String>> string_key(thread->make_object_value<String>(L"a"));
    keys.extract()->initialize_item_unchecked(0, string_key.raw_value());

    Value result = Dict::from_tuple_keys(keys.extract(), Value::from_smi(7));

    ASSERT_FALSE(result.is_exception_marker());
    ASSERT_TRUE(can_convert_to<Dict>(result));
    Dict *dict = result.get_ptr<Dict>();
    EXPECT_EQ(string_key_shape, dict->get_shape());
    EXPECT_EQ(0u, dict->table_generation());
    EXPECT_EQ(Value::from_smi(7),
              dict->get_item(thread, string_key.raw_value()).value());
}

TEST(Dict, PublicFromkeysPromotesNonStringTupleAndListKeys)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(2),
              context.run_file(L"d = dict.fromkeys((1, 'a'), 0)\n"
                               L"len(d)\n"));
    EXPECT_EQ(Value::from_smi(1),
              context.run_file(L"d = dict.fromkeys([1, True], 0)\n"
                               L"len(d)\n"));
}

TEST(Dict, PublicFromkeysPropagatesHashFailure)
{
    test::VmTestContext context;

    Value result = context.run_file(L"class BadHash:\n"
                                    L"    def __hash__(self):\n"
                                    L"        return 'bad'\n"
                                    L"dict.fromkeys(('good', BadHash()), 0)\n");

    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"__hash__ method should return an integer");
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

TEST(Dict, GeneratedReadMethodsContainProtocolCacheSites)
{
    test::VmTestContext context;

    const wchar_t *method_names[] = {L"__getitem__", L"get", L"__contains__"};
    for(const wchar_t *method_name: method_names)
    {
        Function *method = dict_method_function(context, method_name);
        std::string bytecode = fmt::to_string(*method->code_object.extract());
        EXPECT_NE(std::string::npos, bytecode.find("CallSpecialMethod"));
        EXPECT_NE(std::string::npos, bytecode.find("CanonicalizeHash"));
        EXPECT_NE(std::string::npos, bytecode.find("TestEqual"));
        EXPECT_NE(std::string::npos, bytecode.find("ToBool"));
        EXPECT_NE(std::string::npos, bytecode.find("DictPromoteStringKeyed"));
        EXPECT_NE(std::string::npos, bytecode.find("DictProbeStart"));
        EXPECT_NE(std::string::npos, bytecode.find("DictProbeForLookup"));
        EXPECT_NE(std::string::npos, bytecode.find("DictEntryStillMatches"));
        EXPECT_NE(std::string::npos, bytecode.find("JumpIfEqualSmi"));
    }
}

TEST(Dict, GeneratedSetItemContainsProtocolCacheAndMutationSites)
{
    test::VmTestContext context;

    Function *method = dict_method_function(context, L"__setitem__");
    std::string bytecode = fmt::to_string(*method->code_object.extract());
    EXPECT_NE(std::string::npos, bytecode.find("CallSpecialMethod0"));
    EXPECT_NE(std::string::npos, bytecode.find("CanonicalizeHash"));
    EXPECT_NE(std::string::npos, bytecode.find("TestEqual"));
    EXPECT_NE(std::string::npos, bytecode.find("ToBool"));
    EXPECT_NE(std::string::npos, bytecode.find("DictPromoteStringKeyed"));
    EXPECT_NE(std::string::npos, bytecode.find("DictResizeForInsert"));
    EXPECT_NE(std::string::npos, bytecode.find("DictProbeStart"));
    EXPECT_NE(std::string::npos, bytecode.find("DictProbeForInsert"));
    EXPECT_NE(std::string::npos, bytecode.find("DictEntryStillMatches"));
    EXPECT_NE(std::string::npos, bytecode.find("DictInsertNew"));
    EXPECT_NE(std::string::npos, bytecode.find("DictOverwriteEntry"));
    EXPECT_NE(std::string::npos, bytecode.find("JumpIfEqualSmi"));
}

TEST(Dict, GeneratedSetdefaultContainsProtocolCacheAndMutationSites)
{
    test::VmTestContext context;

    Function *method = dict_method_function(context, L"setdefault");
    std::string bytecode = fmt::to_string(*method->code_object.extract());
    EXPECT_NE(std::string::npos, bytecode.find("CallSpecialMethod0"));
    EXPECT_NE(std::string::npos, bytecode.find("CanonicalizeHash"));
    EXPECT_NE(std::string::npos, bytecode.find("TestEqual"));
    EXPECT_NE(std::string::npos, bytecode.find("ToBool"));
    EXPECT_NE(std::string::npos, bytecode.find("DictTryStringKeyedSetDefault"));
    EXPECT_NE(std::string::npos, bytecode.find("DictResizeForInsert"));
    EXPECT_NE(std::string::npos, bytecode.find("DictProbeStart"));
    EXPECT_NE(std::string::npos, bytecode.find("DictProbeForInsert"));
    EXPECT_NE(std::string::npos, bytecode.find("DictEntryStillMatches"));
    EXPECT_NE(std::string::npos, bytecode.find("DictEntryValue"));
    EXPECT_NE(std::string::npos, bytecode.find("DictInsertNew"));
    EXPECT_EQ(std::string::npos, bytecode.find("DictOverwriteEntry"));
    EXPECT_NE(std::string::npos, bytecode.find("JumpIfEqualSmi"));
}

TEST(Dict, GeneratedDelItemContainsProtocolCacheAndMutationSites)
{
    test::VmTestContext context;

    Function *method = dict_method_function(context, L"__delitem__");
    std::string bytecode = fmt::to_string(*method->code_object.extract());
    EXPECT_NE(std::string::npos, bytecode.find("CallSpecialMethod0"));
    EXPECT_NE(std::string::npos, bytecode.find("CanonicalizeHash"));
    EXPECT_NE(std::string::npos, bytecode.find("TestEqual"));
    EXPECT_NE(std::string::npos, bytecode.find("ToBool"));
    EXPECT_NE(std::string::npos, bytecode.find("DictPromoteStringKeyed"));
    EXPECT_NE(std::string::npos, bytecode.find("DictProbeStart"));
    EXPECT_NE(std::string::npos, bytecode.find("DictProbeForLookup"));
    EXPECT_NE(std::string::npos, bytecode.find("DictEntryStillMatches"));
    EXPECT_NE(std::string::npos, bytecode.find("DictDeleteEntry"));
    EXPECT_NE(std::string::npos, bytecode.find("JumpIfEqualSmi"));
}

TEST(Dict, GeneratedPopContainsProtocolCacheAndMutationSites)
{
    test::VmTestContext context;

    Function *method = dict_method_function(context, L"pop");
    std::string bytecode = fmt::to_string(*method->code_object.extract());
    EXPECT_NE(std::string::npos, bytecode.find("CallSpecialMethod0"));
    EXPECT_NE(std::string::npos, bytecode.find("CanonicalizeHash"));
    EXPECT_NE(std::string::npos, bytecode.find("TestEqual"));
    EXPECT_NE(std::string::npos, bytecode.find("ToBool"));
    EXPECT_NE(std::string::npos, bytecode.find("DictTryStringKeyedPop"));
    EXPECT_NE(std::string::npos, bytecode.find("DictProbeStart"));
    EXPECT_NE(std::string::npos, bytecode.find("DictProbeForLookup"));
    EXPECT_NE(std::string::npos, bytecode.find("DictEntryStillMatches"));
    EXPECT_NE(std::string::npos, bytecode.find("DictEntryValue"));
    EXPECT_NE(std::string::npos, bytecode.find("DictDeleteEntry"));
    EXPECT_NE(std::string::npos, bytecode.find("JumpIfEqualSmi"));
}

TEST(Dict, TrustedPromotionConvertsStringKeyedShape)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Shape *general_shape = thread->get_exact_dict_general_shape();

    Dict *dict = thread->make_object_raw<Dict>();
    TrustedDictBytecodeAccess::promote_string_keyed(thread, dict);
    EXPECT_EQ(general_shape, dict->get_shape());
}

TEST(Dict, InsertProbeReadDistinguishesSlotStates)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);

    Dict *empty_dict = thread->make_object_raw<Dict>();
    Expected<TValue<SMI>> hash_one_result =
        thread->hash_value(Value::from_smi(1));
    ASSERT_FALSE(hash_one_result.has_exception());
    TValue<SMI> hash_one = hash_one_result.value();
    TValue<SMI> empty_generation = TValue<SMI>::from_smi(0);
    size_t empty_hash_idx;
    TrustedDictBytecodeAccess::probe_start(empty_dict, hash_one,
                                           &empty_generation, &empty_hash_idx);
    EXPECT_EQ(TrustedDictBytecodeAccess::InsertProbeEmpty,
              TrustedDictBytecodeAccess::probe_for_insert(empty_dict, hash_one,
                                                          empty_hash_idx));

    Dict *dict = thread->make_object_raw<Dict>();
    Expected<void> inserted =
        dict->set_item(thread, Value::from_smi(1), Value::None());
    ASSERT_FALSE(inserted.has_exception());

    TValue<SMI> generation = TValue<SMI>::from_smi(0);
    size_t hash_idx;
    TrustedDictBytecodeAccess::probe_start(dict, hash_one, &generation,
                                           &hash_idx);
    EXPECT_GE(
        TrustedDictBytecodeAccess::probe_for_insert(dict, hash_one, hash_idx),
        0);

    TValue<SMI> colliding_hash = TValue<SMI>::from_smi(0);
    size_t colliding_hash_idx = 0;
    bool found_collision = false;
    for(int64_t candidate = 2; candidate < 1024; ++candidate)
    {
        Expected<TValue<SMI>> candidate_hash_result =
            thread->hash_value(Value::from_smi(candidate));
        ASSERT_FALSE(candidate_hash_result.has_exception());
        TValue<SMI> candidate_hash = candidate_hash_result.value();
        TValue<SMI> candidate_generation = TValue<SMI>::from_smi(0);
        TrustedDictBytecodeAccess::probe_start(
            dict, candidate_hash, &candidate_generation, &colliding_hash_idx);
        if(colliding_hash_idx == hash_idx && candidate_hash != hash_one)
        {
            colliding_hash = candidate_hash;
            found_collision = true;
            break;
        }
    }
    ASSERT_TRUE(found_collision);
    EXPECT_EQ(TrustedDictBytecodeAccess::InsertProbeHashMiss,
              TrustedDictBytecodeAccess::probe_for_insert(dict, colliding_hash,
                                                          colliding_hash_idx));

    Expected<void> deleted = dict->del_item(thread, Value::from_smi(1));
    ASSERT_FALSE(deleted.has_exception());
    EXPECT_EQ(
        TrustedDictBytecodeAccess::InsertProbeTombstone,
        TrustedDictBytecodeAccess::probe_for_insert(dict, hash_one, hash_idx));
}

TEST(Dict, GeneralStringLookupCachesTrustedHashHandler)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(1), context.run_file(L"d = {'alpha': 1}\n"
                                                   L"0 in d\n"
                                                   L"d['alpha']\n"));

    Function *method = dict_method_function(context, L"__getitem__");
    ASSERT_GE(
        method->code_object.extract()->inline_caches.operator_caches.size(),
        2u);
    const OperatorInlineCache &hash_cache =
        method->code_object.extract()->inline_caches.operator_caches[1];
    EXPECT_FALSE(hash_cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, hash_cache.function);
}

TEST(Dict, GeneralStringAssignmentCachesTrustedHashHandler)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::from_smi(1), context.run_file(L"d = {}\n"
                                                   L"d[0] = 0\n"
                                                   L"d['alpha'] = 1\n"
                                                   L"d['alpha']\n"));

    Function *method = dict_method_function(context, L"__setitem__");
    ASSERT_GE(
        method->code_object.extract()->inline_caches.operator_caches.size(),
        2u);
    const OperatorInlineCache &hash_cache =
        method->code_object.extract()->inline_caches.operator_caches[1];
    EXPECT_FALSE(hash_cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, hash_cache.function);
}

TEST(Dict, GeneralStringDeletionCachesTrustedHashHandler)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(), context.run_file(L"d = {}\n"
                                              L"d[0] = 0\n"
                                              L"d['alpha'] = 1\n"
                                              L"del d['alpha']\n"
                                              L"'alpha' not in d\n"));

    Function *method = dict_method_function(context, L"__delitem__");
    ASSERT_GE(
        method->code_object.extract()->inline_caches.operator_caches.size(),
        2u);
    const OperatorInlineCache &hash_cache =
        method->code_object.extract()->inline_caches.operator_caches[1];
    EXPECT_FALSE(hash_cache.trusted_handler.is_null());
    EXPECT_EQ(nullptr, hash_cache.function);
}

TEST(Dict, TrustedStringOperationsKeepStringKeyShape)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Shape *string_key_shape =
        context.thread()->get_exact_dict_string_key_shape();

    Value result = context.run_file(L"d = {}\n"
                                    L"d['key'] = 1\n"
                                    L"assert d['key'] == 1\n"
                                    L"assert 'key' in d\n"
                                    L"del d['key']\n"
                                    L"d\n");

    ASSERT_TRUE(can_convert_to<Dict>(result));
    EXPECT_EQ(0u, result.get_ptr<Dict>()->size());
    EXPECT_EQ(string_key_shape, result.get_ptr<Dict>()->get_shape());
}

TEST(Dict, GeneratedReadMethodsRejectWrongReceiver)
{
    const wchar_t *expressions[] = {L"dict.__getitem__(1, 1)\n",
                                    L"dict.get(1, 1)\n",
                                    L"dict.__contains__(1, 1)\n"};
    const wchar_t *messages[] = {L"dict.__getitem__ expects a dict receiver",
                                 L"dict.get expects a dict receiver",
                                 L"dict.__contains__ expects a dict receiver"};

    for(size_t idx = 0; idx < 3; ++idx)
    {
        test::VmTestContext context;
        Value result = context.run_file(expressions[idx]);
        EXPECT_TRUE(result.is_exception_marker());
        expect_pending_exception(context.thread(), L"TypeError", messages[idx]);
    }
}

TEST(Dict, GeneratedSetItemRejectsWrongReceiver)
{
    test::VmTestContext context;

    Value result = context.run_file(L"dict.__setitem__(1, 1, 1)\n");
    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"dict.__setitem__ expects a dict receiver");
}

TEST(Dict, GeneratedSetdefaultRejectsWrongReceiver)
{
    test::VmTestContext context;

    Value result = context.run_file(L"dict.setdefault(1, 1)\n");
    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"dict.setdefault expects a dict receiver");
}

TEST(Dict, GeneratedDelItemRejectsWrongReceiver)
{
    test::VmTestContext context;

    Value result = context.run_file(L"dict.__delitem__(1, 1)\n");
    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"dict.__delitem__ expects a dict receiver");
}

TEST(Dict, GeneratedPopRejectsWrongReceiver)
{
    test::VmTestContext context;

    Value result = context.run_file(L"dict.pop(1, 1)\n");
    EXPECT_TRUE(result.is_exception_marker());
    expect_pending_exception(context.thread(), L"TypeError",
                             L"dict.pop expects a dict receiver");
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

TEST(Dict, PublicOverwritePreservesEntryOrderAfterPromotion)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"d[1] = 11\n"
                               L"d[2] = 22\n"
                               L"d[1] = 99\n"
                               L"last = d.popitem()\n"
                               L"last[0] == 2 and last[1] == 22 and "
                               L"d[1] == 99\n"));
}

TEST(Dict, PublicTombstoneReusePreservesGeneralProbeChain)
{
    test::VmTestContext context;

    EXPECT_EQ(
        Value::True(),
        context.run_file(L"d = {}\n"
                         L"d[1] = 1\n"
                         L"d[17] = 17\n"
                         L"del d[1]\n"
                         L"d[33] = 33\n"
                         L"len(d) == 2 and d[17] == 17 and d[33] == 33\n"));
}

TEST(Dict, PublicGeneralTombstoneResizeKeepsEntriesReachable)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
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

TEST(Dict, PublicClearRemovesPromotedEntries)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
                               L"d[1] = 'one'\n"
                               L"d[2] = 'two'\n"
                               L"d.clear()\n"
                               L"len(d) == 0 and not (1 in d) and "
                               L"not (2 in d)\n"));
}

TEST(Dict, PublicSetItemRevalidatesTombstoneFilledDuringEquality)
{
    test::VmTestContext context;

    EXPECT_EQ(Value::True(),
              context.run_file(L"d = {}\n"
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

TEST(Dict, SetItemOverwriteWithSameObjectDoesNotChangeOwnership)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Value key = make_string(context, L"shared");
    Owned<Value> value(make_string(context, L"same-value"));
    HeapObject *value_object = value.value().as.ptr;

    set_dict_item(thread, dict, key, value.value());
    int64_t refcount_before = value_object->refcount;
    set_dict_item(thread, dict, key, value.value());

    EXPECT_EQ(refcount_before, value_object->refcount);
    EXPECT_FALSE(thread->zero_count_table_contains_for_testing(value_object));
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

TEST(DictKeywordStorage, CopyPreservesStringEntriesAndOwnsReferences)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<Dict>> source(thread->make_object_value<Dict>());
    Owned<TValue<String>> removed(
        thread->make_object_value<String>(L"removed"));
    Owned<TValue<String>> first(thread->make_object_value<String>(L"first"));
    Owned<TValue<String>> second(thread->make_object_value<String>(L"second"));
    Owned<TValue<List>> item(thread->make_object_value<List>());
    source.extract()->string_keyed_insert(removed.value(), Value::None());
    source.extract()->string_keyed_insert(first.value(), item.raw_value());
    source.extract()->string_keyed_insert(second.value(), Value::from_smi(2));
    ASSERT_EQ(Value::None(),
              source.extract()->string_keyed_delete(removed.value()));

    Owned<TValue<Dict>> copy(source.extract()->copy());
    EXPECT_NE(source.extract(), copy.extract());
    EXPECT_EQ(thread->get_exact_dict_string_key_shape(),
              copy.extract()->get_shape());
    source.extract()->clear();
    ASSERT_EQ(2u, copy.extract()->size());
    auto it = copy.extract()->begin();
    EXPECT_EQ(first.raw_value(), (*it).key);
    EXPECT_EQ(item.raw_value(), (*it).value);
    ++it;
    EXPECT_EQ(second.raw_value(), (*it).key);
    EXPECT_EQ(Value::from_smi(2), (*it).value);
    EXPECT_EQ(item.raw_value(),
              copy.extract()->string_keyed_lookup(first.value()));
    EXPECT_TRUE(source.extract()->empty());
}

TEST(DictKeywordStorage, CopyGeneralStorageDoesNotCallKeyProtocols)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<Value> result(context.run_file(LR"(
armed = False
class Key:
    def __hash__(self):
        if armed: raise AssertionError('hash called during copy')
        return 7
    def __eq__(self, other):
        if armed: raise AssertionError('equality called during copy')
        return self is other
class S(str):
    def __hash__(self):
        if armed: raise AssertionError('subclass hash called during copy')
        return 7
    def __eq__(self, other):
        if armed: raise AssertionError('subclass equality called during copy')
        return self is other
d = {Key(): [], Key(): [], S('name'): [], 42: []}
armed = True
d
)"));
    ASSERT_FALSE(result.value().is_exception_marker());
    Owned<TValue<Dict>> source(
        TValue<Dict>::from_value_assumed(result.value()));
    ASSERT_EQ(thread->get_exact_dict_general_shape(),
              source.extract()->get_shape());
    std::vector<Dict::EntryView> original;
    for(Dict::EntryView entry: *source.extract())
    {
        original.push_back(entry);
    }
    Owned<TValue<Dict>> copy(source.extract()->copy());
    EXPECT_NE(source.extract(), copy.extract());
    EXPECT_EQ(thread->get_exact_dict_general_shape(),
              copy.extract()->get_shape());
    ASSERT_EQ(original.size(), copy.extract()->size());
    source.extract()->clear();
    size_t index = 0;
    for(Dict::EntryView entry: *copy.extract())
    {
        EXPECT_EQ(original[index].key, entry.key);
        EXPECT_EQ(original[index].value, entry.value);
        ++index;
    }
    EXPECT_EQ(PendingExceptionKind::None, thread->pending_exception_kind());
}

TEST(DictKeywordStorage, UniqueInsertionPreservesDuplicateAndReusesTombstone)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<Dict>> dict(thread->make_object_value<Dict>());
    Owned<TValue<String>> first(thread->make_object_value<String>(L"same"));
    Owned<TValue<String>> equal(thread->make_object_value<String>(L"same"));
    Owned<TValue<List>> value(thread->make_object_value<List>());
    ASSERT_NE(first.raw_value(), equal.raw_value());
    ASSERT_TRUE(dict.extract()->string_keyed_insert_if_absent(
        first.value(), value.raw_value()));
    EXPECT_FALSE(dict.extract()->string_keyed_insert_if_absent(equal.value(),
                                                               Value::None()));
    ASSERT_EQ(1u, dict.extract()->size());
    EXPECT_EQ(first.raw_value(), (*dict.extract()->begin()).key);
    EXPECT_EQ(value.raw_value(), (*dict.extract()->begin()).value);
    ASSERT_EQ(Value::None(),
              dict.extract()->string_keyed_delete(first.value()));
    EXPECT_TRUE(dict.extract()->string_keyed_insert_if_absent(
        equal.value(), Value::from_smi(9)));
    EXPECT_EQ(equal.raw_value(), (*dict.extract()->begin()).key);
    EXPECT_EQ(Value::from_smi(9), (*dict.extract()->begin()).value);
}

TEST(DictKeywordStorage, UniqueMergeStopsAtFirstConflictAndHandlesSelfMerge)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<Dict>> destination(thread->make_object_value<Dict>());
    Owned<TValue<Dict>> source(thread->make_object_value<Dict>());
    Owned<TValue<String>> existing(
        thread->make_object_value<String>(L"conflict"));
    Owned<TValue<String>> equal(thread->make_object_value<String>(L"conflict"));
    Owned<TValue<String>> prefix(thread->make_object_value<String>(L"prefix"));
    Owned<TValue<String>> suffix(thread->make_object_value<String>(L"suffix"));
    destination.extract()->string_keyed_insert(existing.value(),
                                               Value::from_smi(1));
    source.extract()->string_keyed_insert(prefix.value(), Value::from_smi(2));
    source.extract()->string_keyed_insert(equal.value(), Value::from_smi(3));
    source.extract()->string_keyed_insert(suffix.value(), Value::from_smi(4));
    auto conflict =
        destination.extract()->merge_string_keyed_unique(source.extract());
    ASSERT_TRUE(conflict.has_value());
    EXPECT_EQ(equal.raw_value(), conflict.value().raw_value());
    ASSERT_EQ(2u, destination.extract()->size());
    auto it = destination.extract()->begin();
    EXPECT_EQ(existing.raw_value(), (*it).key);
    EXPECT_EQ(Value::from_smi(1), (*it).value);
    ++it;
    EXPECT_EQ(prefix.raw_value(), (*it).key);
    EXPECT_EQ(Value::from_smi(2), (*it).value);
    EXPECT_FALSE(destination.extract()->string_keyed_contains(suffix.value()));
    EXPECT_EQ(3u, source.extract()->size());
    conflict =
        destination.extract()->merge_string_keyed_unique(destination.extract());
    ASSERT_TRUE(conflict.has_value());
    EXPECT_EQ(existing.raw_value(), conflict.value().raw_value());
    EXPECT_EQ(2u, destination.extract()->size());
    source.extract()->clear();
    EXPECT_FALSE(source.extract()
                     ->merge_string_keyed_unique(source.extract())
                     .has_value());
    EXPECT_FALSE(destination.extract()
                     ->merge_string_keyed_unique(source.extract())
                     .has_value());
}

TEST(DictKeywordStorage, UniqueMergeGrowsAndRetainsReferencesInOrder)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<Dict>> destination(thread->make_object_value<Dict>());
    Owned<TValue<Dict>> source(thread->make_object_value<Dict>());
    Owned<TValue<List>> item(thread->make_object_value<List>());
    for(int64_t i = 0; i < 80; ++i)
    {
        Owned<TValue<String>> key(thread->make_object_value<String>(
            (L"key" + std::to_wstring(i)).c_str()));
        ASSERT_TRUE(source.extract()->string_keyed_insert_if_absent(
            key.value(), item.raw_value()));
    }
    ASSERT_FALSE(destination.extract()
                     ->merge_string_keyed_unique(source.extract())
                     .has_value());
    ASSERT_EQ(80u, destination.extract()->size());
    source.extract()->clear();
    int64_t index = 0;
    for(Dict::EntryView entry: *destination.extract())
    {
        EXPECT_EQ(L"key" + std::to_wstring(index),
                  std::wstring(entry.key.get_ptr<String>()->data));
        EXPECT_EQ(item.raw_value(), entry.value);
        Owned<TValue<String>> equal_key(thread->make_object_value<String>(
            (L"key" + std::to_wstring(index)).c_str()));
        EXPECT_EQ(item.raw_value(), destination.extract()->string_keyed_lookup(
                                        equal_key.value()));
        ++index;
    }
    EXPECT_EQ(80, index);
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

TEST(Dict, ViewIteratorsRemainInvalidAfterSizeIsRestored)
{
    const wchar_t *view_names[] = {L"keys", L"values", L"items"};
    for(const wchar_t *view_name: view_names)
    {
        test::VmTestContext context;
        std::wstring source = L"d = {'a': 1, 'b': 2}\niterator = d.";
        source += view_name;
        source += L"().__iter__()\n"
                  L"iterator.__next__()\n"
                  L"d['c'] = 3\n"
                  L"first_error = 0\n"
                  L"try:\n"
                  L"    iterator.__next__()\n"
                  L"except RuntimeError:\n"
                  L"    first_error = 1\n"
                  L"del d['c']\n"
                  L"second_error = 0\n"
                  L"try:\n"
                  L"    iterator.__next__()\n"
                  L"except RuntimeError:\n"
                  L"    second_error = 1\n"
                  L"first_error * 10 + second_error\n";

        EXPECT_EQ(Value::from_smi(11), context.run_file(source.c_str()));
    }
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
namespace
{
    static Value run_trusted_keyword_test(test::VmTestContext &context,
                                          const wchar_t *source)
    {
        auto name = context.vm().get_or_create_interned_string_value(
            L"<call-keyword-test>");
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
}  // namespace

TEST(CallKeywordConstruction, NativePathsFreshnessOrderAndDuplicateErrors)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
item = []
source = {'removed': 0, 'a': item, 'b': 2}
del source['removed']
builder = __clover_build_call_keywords__(target, source)
assert builder is not source
assert len(builder) == 2 and builder['b'] == 2
assert builder['a'] is item
alias = builder
assert __clover_merge_call_keywords__(target, builder, {'c': 3}) is None
assert __clover_add_call_keyword__(target, builder, 'd', item) is None
assert builder is alias and builder['d'] is item
assert len(source) == 2 and source['a'] is item and source['b'] == 2
assert __clover_to_exact_tuple__(builder.keys()) == ('a', 'b', 'c', 'd')
try:
    __clover_merge_call_keywords__(target, builder, {'prefix': item, 'a': 99, 'suffix': 100})
except TypeError as error:
    assert "target() got multiple values for keyword argument 'a'" == str(error)
else:
    raise AssertionError
assert builder['prefix'] is item and builder['a'] is item
assert 'suffix' not in builder
try:
    __clover_add_call_keyword__(target, builder, 'b', 99)
except TypeError:
    pass
else:
    raise AssertionError
assert builder['b'] == 2
class Target: pass
try:
    __clover_add_call_keyword__(Target, builder, 'b', 99)
except TypeError as error:
    assert str(error) == "Target() got multiple values for keyword argument 'b'"
else:
    raise AssertionError
assert __clover_merge_call_keywords__(target, builder, {}) is None
assert len(__clover_build_call_keywords__(target, {})) == 0
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordConstruction, GeneralCopyMergePreserveKeysAndReuseStoredHashes)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
hash_calls = 0
class Key(str):
    def __hash__(self):
        global hash_calls
        hash_calls += 1
        return str.__hash__(self)
key = Key('custom')
item = []
source = {key: item, 42: 9}
before = hash_calls
copy = __clover_build_call_keywords__(target, source)
assert copy is not source and hash_calls == before
builder = {}
assert __clover_merge_call_keywords__(target, builder, source) is None
assert hash_calls == before
keys = __clover_to_exact_tuple__(builder.keys())
assert keys[0] is key and keys[1] == 42
assert builder[key] is item
assert copy[key] is item
assert __clover_add_call_keyword__(target, builder, 'ordinary', 7) is None
assert builder['ordinary'] == 7
assert __clover_merge_call_keywords__(target, builder, {'later': item}) is None
assert builder['later'] is item
try:
    __clover_add_call_keyword__(target, builder, 'custom', 99)
except TypeError:
    pass
else:
    raise AssertionError
assert __clover_to_exact_tuple__(builder.keys())[0] is key and builder[key] is item
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordConstruction, NonStringDuplicateFormattingUsesManagedCalls)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
calls = []
class Key:
    def __hash__(self): return 7
    def __str__(self):
        calls.append('str')
        nested = __clover_build_call_keywords__(target, {'nested': 1})
        assert __clover_add_call_keyword__(target, nested, 'extra', 2) is None
        return 'odd'
key = Key()
builder = {key: 1}
try:
    __clover_merge_call_keywords__(target, builder, {key: 2})
except TypeError as error:
    assert str(error) == "target() got multiple values for keyword argument 'odd'"
else:
    raise AssertionError
assert calls == ['str'] and builder[key] == 1
class Bad(Key):
    def __str__(self): raise ValueError('format failed')
bad = Bad()
try:
    __clover_merge_call_keywords__(target, {bad: 1}, {bad: 2})
except ValueError as error:
    assert str(error) == 'format failed'
else:
    raise AssertionError
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordConstruction, SourceMutationAndErrorPrecedence)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
source = {}
mode = 0
class Key(str):
    armed = False
    def __hash__(self): return 7
    def __eq__(self, other):
        if self.armed:
            self.armed = False
            if mode == 0 or mode == 4 or mode == 5:
                source['new'] = 'new'
            elif mode == 1:
                del source['tail']
            elif mode == 2:
                source['tail'] = 'changed'
            elif mode == 3:
                del source['tail']
                source['new'] = 'new'
            if mode == 5: raise ValueError('comparison failed')
        return mode == 4
for mode in range(6):
    left = Key('left')
    right = Key('right')
    builder = {left: 'base'}
    source = {right: 'first', 'tail': 'old'}
    left.armed = True
    reached = False
    if mode == 0 or mode == 1:
        try:
            __clover_merge_call_keywords__(target, builder, source)
            reached = True
        except RuntimeError as error:
            assert str(error) == 'dict mutated during update'
        else:
            raise AssertionError
        assert not reached
        assert __clover_to_exact_tuple__(builder.keys())[1] is right
    elif mode == 4:
        try:
            __clover_merge_call_keywords__(target, builder, source)
        except TypeError as error:
            assert str(error) == "target() got multiple values for keyword argument 'right'"
        else:
            raise AssertionError
        assert len(builder) == 1
    elif mode == 5:
        try:
            __clover_merge_call_keywords__(target, builder, source)
        except ValueError as error:
            assert str(error) == 'comparison failed'
        else:
            raise AssertionError
        assert len(builder) == 1
    else:
        assert __clover_merge_call_keywords__(target, builder, source) is None
        if mode == 2: assert builder['tail'] == 'changed'
        if mode == 3:
            assert 'tail' not in builder and builder['new'] == 'new'
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordConstruction,
     DestinationMutationRestartsProbeAndPreservesCallerRegisters)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
builder = {}
calls = []
class Stored(str):
    armed = False
    def __hash__(self): return str.__hash__(self)
    def __eq__(self, other):
        if self.armed:
            self.armed = False
            builder.clear()
            for i in range(40): builder[i] = i
            nested = {}
            assert __clover_merge_call_keywords__(target, nested, {'nested': 1}) is None
            calls.append('compare')
        return False
key = Stored('name')
builder[key] = 'old'
key.armed = True
keep = []
def check(a, b, c):
    assert a is keep and c is keep and b is None
check(keep, __clover_add_call_keyword__(target, builder, 'name', keep), keep)
assert calls == ['compare']
assert builder['name'] is keep and len(builder) == 41
for i in range(40): assert builder[i] == i
assert __clover_add_call_keyword__(target, builder, 'later', 9) is None
assert builder['later'] == 9
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordConstruction, EvaluationOrderAndMissingMappingErrors)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
events = []
builder = {}
def callable_value():
    events.append('callable')
    return target
def destination():
    events.append('destination')
    return builder
def item():
    events.append('item')
    return []
assert __clover_add_call_keyword__(callable_value(), destination(), 'name', item()) is None
assert events == ['callable', 'destination', 'item']
class Mapping:
    def keys(self): return []
class Subdict(dict): pass
assert len(__clover_build_call_keywords__(target, Mapping())) == 0
assert len(__clover_build_call_keywords__(target, Subdict())) == 0
assert __clover_merge_call_keywords__(target, builder, Mapping()) is None
for source in ([], [('a', 1)], 1):
    try:
        __clover_build_call_keywords__(target, source)
    except TypeError as error:
        assert "argument after ** must be a mapping" in str(error)
    else:
        raise AssertionError
    try:
        __clover_merge_call_keywords__(target, builder, source)
    except TypeError as error:
        assert "argument after ** must be a mapping" in str(error)
    else:
        raise AssertionError
assert __clover_to_exact_tuple__(builder.keys()) == ('name',)
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordMapping, MaterializesIteratorsBeforeFetchingValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
events = []
class Keys:
    def __init__(self): self.position = 0
    def __iter__(self):
        events.append('iter')
        return self
    def __next__(self):
        i = self.position
        self.position += 1
        events.append(i)
        if i == 2: raise StopIteration
        return ('a', 'b')[i]
class Mapping:
    def keys(self):
        events.append('keys')
        return Keys()
    def __getitem__(self, key):
        events.append(key)
        return key
source = Mapping()
builder = __clover_build_call_keywords__(target, source)
assert events == ['keys', 'iter', 0, 1, 2, 'a', 'b']
assert builder['a'] == 'a' and builder['b'] == 'b'
assert len(builder) == 2
second = __clover_build_call_keywords__(target, source)
assert second is not builder
class Empty:
    def keys(self): return []
assert __clover_merge_call_keywords__(target, builder, Empty()) is None
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordMapping, ExactKeyListsStayLiveDuringLookup)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
class Mapping:
    def __init__(self, keys, mode):
        self.key_list = keys
        self.mode = mode
        self.seen = []
    def keys(self): return self.key_list
    def __getitem__(self, key):
        self.seen.append(key)
        if len(self.seen) == 1:
            if self.mode == 0:
                self.key_list[1] = 'changed'
                self.key_list.append('new')
            elif self.mode == 1:
                self.key_list.clear()
            elif self.mode == 2:
                del self.key_list[0]
            elif self.mode == 3:
                self.key_list.insert(0, 'earlier')
        return key
source = Mapping(['a', 'b'], 0)
builder = __clover_build_call_keywords__(target, source)
assert source.seen == ['a', 'changed', 'new']
assert __clover_to_exact_tuple__(builder.keys()) == ('a', 'changed', 'new')
source = Mapping(['a', 'b'], 1)
builder = __clover_build_call_keywords__(target, source)
assert source.seen == ['a'] and len(builder) == 1
source = Mapping(['a', 'b', 'c'], 2)
builder = __clover_build_call_keywords__(target, source)
assert source.seen == ['a', 'c']
assert __clover_to_exact_tuple__(builder.keys()) == ('a', 'c')
source = Mapping(['a', 'b'], 3)
builder = {}
try:
    __clover_merge_call_keywords__(target, builder, source)
except TypeError:
    pass
else:
    raise AssertionError
assert source.seen == ['a'] and len(builder) == 1
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordMapping, ListAndDictSubclassesUseTheirOverrides)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
events = []
class KeyList(list):
    def __iter__(self):
        events.append('override iter')
        return iter(('a', 'b'))
keys = KeyList()
keys.append('wrong')
class Mapping:
    def keys(self): return keys
    def __getitem__(self, key):
        events.append(key)
        keys.clear()
        return key
builder = __clover_build_call_keywords__(target, Mapping())
assert events == ['override iter', 'a', 'b']
assert __clover_to_exact_tuple__(builder.keys()) == ('a', 'b')
class Subdict(dict):
    def keys(self):
        events.append('override keys')
        return ['exposed']
    def __getitem__(self, key):
        events.append(key)
        return 42
source = Subdict()
source['hidden'] = 9
builder = __clover_build_call_keywords__(target, source)
assert len(builder) == 1 and builder['exposed'] == 42
assert 'hidden' not in builder
assert __clover_merge_call_keywords__(target, builder, Mapping()) is None
assert events == ['override iter', 'a', 'b', 'override keys', 'exposed', 'override iter', 'a', 'b']
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordMapping, DuplicateCheckPrecedesValueLookupAndRetainsPrefix)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
class Mapping:
    def __init__(self, keys):
        self.key_list = keys
        self.seen = []
    def keys(self): return self.key_list
    def __getitem__(self, key):
        self.seen.append(key)
        return self.seen
source = Mapping(['prefix', 'a', 'suffix'])
builder = {'a': 7}
try:
    __clover_merge_call_keywords__(target, builder, source)
except TypeError as error:
    assert str(error) == "target() got multiple values for keyword argument 'a'"
else:
    raise AssertionError
assert source.seen == ['prefix']
assert builder['prefix'] is source.seen and builder['a'] == 7
assert 'suffix' not in builder
source = Mapping(['repeat', 'repeat'])
builder = {}
try:
    __clover_merge_call_keywords__(target, builder, source)
except TypeError:
    pass
else:
    raise AssertionError
assert source.seen == ['repeat'] and len(builder) == 1
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordMapping,
     KeysCallAttributeErrorsAreTranslatedButOtherFailuresPropagate)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
class Missing: pass
class KeysAttributeError:
    def keys(self): raise AttributeError('keys body')
for source in (Missing(), KeysAttributeError(), [('a', 1)]):
    try:
        __clover_build_call_keywords__(target, source)
    except TypeError as error:
        assert 'target() argument after ** must be a mapping, not ' in str(error)
    else:
        raise AssertionError
expected = ValueError('keys failed')
class KeysFailure:
    def keys(self): raise expected
try:
    __clover_build_call_keywords__(target, KeysFailure())
except ValueError as error:
    assert error is expected
else:
    raise AssertionError
class Noncallable: keys = 42
try:
    __clover_build_call_keywords__(target, Noncallable())
except TypeError as error:
    assert 'argument after **' not in str(error)
else:
    raise AssertionError
class Noniterable:
    def keys(self): return 42
try:
    __clover_build_call_keywords__(target, Noniterable())
except TypeError as error:
    assert 'argument after **' not in str(error)
else:
    raise AssertionError
class BadKeys:
    def __iter__(self): raise expected
class Materialization:
    def keys(self): return BadKeys()
    def __getitem__(self, key): raise AssertionError('lookup must not run')
for expected in (AttributeError('materialize'), KeyError('materialize')):
    builder = {'keep': 1}
    try:
        __clover_merge_call_keywords__(target, builder, Materialization())
    except Exception as error:
        assert error is expected
    else:
        raise AssertionError
    assert len(builder) == 1
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordMapping, ItemAndHashEqualityExceptionsRetainTheirIdentity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
class ItemFailure:
    def keys(self): return ['prefix', 'bad']
    def __getitem__(self, key):
        if key == 'bad': raise expected
        return 7
class HashKey:
    def __hash__(self): raise expected
class HashFailure:
    def keys(self): return [HashKey()]
    def __getitem__(self, key): raise AssertionError('hash must fail first')
class Stored(str):
    def __hash__(self): return str.__hash__(self)
    def __eq__(self, other): raise expected
class EqualityFailure:
    def keys(self): return ['collision']
    def __getitem__(self, key): raise AssertionError('equality must fail first')
for expected in (AttributeError('callback'), KeyError('callback')):
    builder = {}
    try:
        __clover_merge_call_keywords__(target, builder, ItemFailure())
    except Exception as error:
        assert error is expected
    else:
        raise AssertionError
    assert builder['prefix'] == 7 and len(builder) == 1
    try:
        __clover_merge_call_keywords__(target, builder, HashFailure())
    except Exception as error:
        assert error is expected
    else:
        raise AssertionError
    key = Stored('collision')
    builder = {key: 1}
    try:
        __clover_merge_call_keywords__(target, builder, EqualityFailure())
    except Exception as error:
        assert error is expected
    else:
        raise AssertionError
    assert len(builder) == 1
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordMapping,
     NonStringKeysAndNestedCallsPreserveRootsAndCallerRegisters)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
class Key(str):
    def __hash__(self): return str.__hash__(self)
key = Key('custom')
item = []
class Mapping:
    def keys(self):
        nested = __clover_build_call_keywords__(target, {'nested': item})
        assert nested['nested'] is item
        return [key, 42]
    def __getitem__(self, requested):
        nested = {}
        assert __clover_merge_call_keywords__(target, nested, Empty()) is None
        assert __clover_add_call_keyword__(target, nested, 'inner', item) is None
        assert nested['inner'] is item
        return item
class Empty:
    def keys(self): return []
source = Mapping()
builder = __clover_build_call_keywords__(target, source)
assert builder[key] is item and builder[42] is item
assert __clover_to_exact_tuple__(builder.keys())[0] is key
keep = []
def check(a, b, c):
    assert a is keep and c is keep and b is None
other = {}
check(keep, __clover_merge_call_keywords__(target, other, source), keep)
assert other[key] is item and other[42] is item
assert __clover_add_call_keyword__(target, other, 'later', item) is None
assert other['later'] is item
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}

TEST(CallKeywordMapping, FailedKeyMaterializationDoesNotFetchOrInsertValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = run_trusted_keyword_test(context, LR"(
def target(): pass
seen = []
class Keys:
    def __init__(self): self.position = 0
    def __iter__(self): return self
    def __next__(self):
        self.position += 1
        if self.position == 1: return 'first'
        raise expected
class Mapping:
    def keys(self): return Keys()
    def __getitem__(self, key):
        seen.append(key)
        raise AssertionError('materialization must finish first')
for expected in (AttributeError('next failed'), KeyError('next failed')):
    builder = {'keep': 7}
    try:
        __clover_merge_call_keywords__(target, builder, Mapping())
    except Exception as error:
        assert error is expected
    else:
        raise AssertionError
    assert seen == [] and len(builder) == 1 and builder['keep'] == 7
)");
    ASSERT_FALSE(result.is_exception_marker())
        << string_as_wchar_t(test::native_exception_message(
               context.thread()->pending_exception_object()));
}
