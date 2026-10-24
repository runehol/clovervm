#include "runtime/expanded_call.h"

#include "builtin_types/dict.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/owned.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace cl;

namespace
{
    class ExpandedCallBinding : public testing::Test
    {
    protected:
        void load(const wchar_t *source)
        {
            inputs = context.run_file(source);
            ASSERT_FALSE(inputs.value().is_exception_marker());
            ASSERT_TRUE(can_convert_to<Tuple>(inputs.value()));
        }

        Value input(size_t idx) const
        {
            return inputs.value().get_ptr<Tuple>()->item_unchecked(idx);
        }

        TValue<Function> function() const
        {
            return TValue<Function>::from_value_assumed(input(0));
        }

        TValue<Dict> keywords() const
        {
            return TValue<Dict>::from_value_assumed(input(1));
        }

        Expected<ExpandedCallBindingPlan> build(uint32_t positional_count)
        {
            return build_expanded_call_binding_plan(
                context.thread(), function(), positional_count, keywords());
        }

        void expect_type_error(const Expected<ExpandedCallBindingPlan> &result,
                               const wchar_t *message)
        {
            ASSERT_TRUE(result.has_exception());
            TValue<Exception> exception =
                context.thread()->pending_exception_object();
            EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
                      exception.extract()->get_shape()->get_class());
            EXPECT_EQ(
                std::wstring(message),
                string_as_wchar_t(test::native_exception_message(exception)));
        }

        void check_python_state()
        {
            EXPECT_EQ(Value::True(),
                      context.thread()->call_clovervm_function(
                          TValue<Function>::from_value_assumed(input(2))));
        }

        test::VmTestContext context;
        ThreadState::ActivationScope activation{context.thread()};
        Owned<Value> inputs{Value::None()};
    };
}  // namespace

TEST_F(ExpandedCallBinding, RecordsLogicalDestinationsDefaultsAndLeftovers)
{
    load(LR"(
def f(a, b=2, *args, c, d=4, **rest): pass
(f, {'c': 3, 'extra': 5, 'b': 6})
)");
    auto result = build(1);
    ASSERT_FALSE(result.has_exception());
    EXPECT_EQ((std::vector<uint32_t>{
                  3, ExpandedCallBindingPlan::UnmatchedKeyword, 1}),
              result.value().keyword_destinations);
    EXPECT_EQ((std::vector<uint32_t>{4}), result.value().default_slots);
    EXPECT_EQ(KwargsAction::BuildLeftovers, result.value().kwargs_action);
    EXPECT_EQ(3u, keywords().extract()->size());
}

TEST_F(ExpandedCallBinding, ReusesTransportWhenNoKeywordsMatch)
{
    load(LR"(
def f(a=1, **rest): pass
(f, {'extra': 5})
)");
    auto result = build(0);
    ASSERT_FALSE(result.has_exception());
    EXPECT_EQ(
        (std::vector<uint32_t>{ExpandedCallBindingPlan::UnmatchedKeyword}),
        result.value().keyword_destinations);
    EXPECT_EQ((std::vector<uint32_t>{0}), result.value().default_slots);
    EXPECT_EQ(KwargsAction::ReuseTransport, result.value().kwargs_action);
    result = build(1);
    ASSERT_FALSE(result.has_exception());
    EXPECT_TRUE(result.value().default_slots.empty());
    EXPECT_EQ(KwargsAction::ReuseTransport, result.value().kwargs_action);
}

TEST_F(ExpandedCallBinding, PureCollectorsAcceptEmptyTransportAndExtraPositions)
{
    load(LR"(
def f(*args, **rest): pass
(f, {})
)");
    auto result = build(1000);
    ASSERT_FALSE(result.has_exception());
    EXPECT_TRUE(result.value().keyword_destinations.empty());
    EXPECT_TRUE(result.value().default_slots.empty());
    EXPECT_EQ(KwargsAction::ReuseTransport, result.value().kwargs_action);
}

TEST_F(ExpandedCallBinding, AllMatchedKeywordsStillRequireFreshLeftovers)
{
    load(LR"(
def f(a, **rest): pass
(f, {'a': 5})
)");
    auto result = build(0);
    ASSERT_FALSE(result.has_exception());
    EXPECT_EQ((std::vector<uint32_t>{0}), result.value().keyword_destinations);
    EXPECT_EQ(KwargsAction::BuildLeftovers, result.value().kwargs_action);
}

TEST_F(ExpandedCallBinding, PositionalOnlyNamesAreUnmatched)
{
    load(LR"(
def f(a, /, **rest): pass
(f, {'a': 5})
)");
    auto result = build(1);
    ASSERT_FALSE(result.has_exception());
    EXPECT_EQ(
        (std::vector<uint32_t>{ExpandedCallBindingPlan::UnmatchedKeyword}),
        result.value().keyword_destinations);
    EXPECT_EQ(KwargsAction::ReuseTransport, result.value().kwargs_action);
}

TEST_F(ExpandedCallBinding, PositionalOnlyNameWithoutCollectorIsRejected)
{
    load(LR"(
def f(a, /): pass
(f, {'a': 5})
)");
    expect_type_error(build(1), L"invalid keyword argument");
}

TEST_F(ExpandedCallBinding, NoCollectorAndSparseDefaults)
{
    load(LR"(
def f(a=1, *, b, c=3): pass
(f, {'b': 5})
)");
    auto result = build(0);
    ASSERT_FALSE(result.has_exception());
    EXPECT_EQ((std::vector<uint32_t>{1}), result.value().keyword_destinations);
    EXPECT_EQ((std::vector<uint32_t>{0, 2}), result.value().default_slots);
    EXPECT_EQ(KwargsAction::None, result.value().kwargs_action);
}

TEST_F(ExpandedCallBinding, TraversesGeneralStorageAndSkipsDeletedEntries)
{
    load(LR"(
def f(a, b): pass
d = {42: 0, 'b': 2, 'a': 1}
del d[42]
(f, d)
)");
    ASSERT_EQ(context.thread()->get_exact_dict_general_shape(),
              keywords().extract()->get_shape());
    auto result = build(0);
    ASSERT_FALSE(result.has_exception());
    EXPECT_EQ((std::vector<uint32_t>{1, 0}),
              result.value().keyword_destinations);
}

TEST_F(ExpandedCallBinding,
       SubclassComparisonsUseDeclarationOrderAndTruthConversion)
{
    load(LR"(
events = []
armed = False
class Truth:
    def __init__(self, result): self.result = result
    def __bool__(self):
        events.append('truth')
        return self.result
class S(str):
    def __hash__(self):
        if armed: raise AssertionError('binder hashed key')
        return 10
    def __eq__(self, other):
        events.append(other)
        return Truth(other == 'b')
def f(a=1, b=2): pass
d = {S('ignored'): 5}
armed = True
def check():
    return len(events) == 4 and events[0] == 'a' and events[1] == 'truth' and events[2] == 'b' and events[3] == 'truth'
(f, d, check)
)");
    auto result = build(0);
    ASSERT_FALSE(result.has_exception());
    EXPECT_EQ((std::vector<uint32_t>{1}), result.value().keyword_destinations);
    EXPECT_EQ((std::vector<uint32_t>{0}), result.value().default_slots);
    check_python_state();
}

TEST_F(ExpandedCallBinding, SubclassComparisonsRunInIncomingOrderOnEveryBuild)
{
    load(LR"(
events = []
class S(str):
    def __hash__(self): return 10 if self == 'b' else 11
    def __eq__(self, other):
        events.append(other)
        return str.__eq__(self, other)
def f(a, b): pass
d = {S('b'): 2, S('a'): 1}
events.clear()
def check():
    return len(events) == 6 and events[0] == 'a' and events[1] == 'b' and events[2] == 'a' and events[3] == 'a' and events[4] == 'b' and events[5] == 'a'
(f, d, check)
)");
    for(int idx = 0; idx < 2; ++idx)
    {
        auto result = build(0);
        ASSERT_FALSE(result.has_exception());
        EXPECT_EQ((std::vector<uint32_t>{1, 0}),
                  result.value().keyword_destinations);
    }
    check_python_state();
}

TEST_F(ExpandedCallBinding, SubclassNotImplementedFallsBackToStringEquality)
{
    load(LR"(
events = []
class S(str):
    def __hash__(self): return 10
    def __eq__(self, other):
        events.append(other)
        return NotImplemented
def f(a, b=2): pass
def check(): return len(events) == 1 and events[0] == 'a'
(f, {S('a'): 5}, check)
)");
    auto result = build(0);
    ASSERT_FALSE(result.has_exception());
    EXPECT_EQ((std::vector<uint32_t>{0}), result.value().keyword_destinations);
    EXPECT_EQ((std::vector<uint32_t>{1}), result.value().default_slots);
    check_python_state();
}

TEST_F(ExpandedCallBinding,
       ExactNamesNeedNotBeInternedAndFailuresLeaveTransportIntact)
{
    load(LR"(
def f(a): pass
(f, {})
)");
    Owned<TValue<String>> key(
        context.thread()->make_object_value<String>(L"a"));
    keywords().extract()->string_keyed_insert(key.value(), Value::from_smi(5));
    auto success = build(0);
    ASSERT_FALSE(success.has_exception());
    EXPECT_EQ((std::vector<uint32_t>{0}), success.value().keyword_destinations);
    expect_type_error(build(1), L"invalid keyword argument");
    context.thread()->clear_pending_exception();
    EXPECT_EQ(1u, keywords().extract()->size());
    Dict::EntryView entry;
    ASSERT_TRUE(keywords().extract()->entry_at(0, entry));
    EXPECT_EQ(key.raw_value(), entry.key);
    EXPECT_EQ(Value::from_smi(5), entry.value);
    auto rebuilt = build(0);
    ASSERT_FALSE(rebuilt.has_exception());
    EXPECT_EQ(success.value().keyword_destinations,
              rebuilt.value().keyword_destinations);
}

TEST_F(ExpandedCallBinding,
       AllKeysValidatedBeforeComparisonEvenForPureCollectors)
{
    load(LR"(
events = []
class S(str):
    def __hash__(self): return 10
    def __eq__(self, other):
        events.append(other)
        raise AssertionError('comparison before validation')
def f(a): pass
def collector(*args, **rest): pass
def check(): return len(events) == 0
(f, {S('a'): 1, 42: 2}, check, collector)
)");
    expect_type_error(build(10), L"keywords must be strings");
    context.thread()->clear_pending_exception();
    expect_type_error(build_expanded_call_binding_plan(
                          context.thread(),
                          TValue<Function>::from_value_assumed(input(3)), 0,
                          keywords()),
                      L"keywords must be strings");
    context.thread()->clear_pending_exception();
    check_python_state();
}

TEST_F(ExpandedCallBinding, DistinctSubclassKeysCannotFillSameParameter)
{
    load(LR"(
class S(str):
    def __hash__(self): return 10 if str.__eq__(self, 'x') else 11
    def __eq__(self, other): return other == 'a'
def f(a): pass
(f, {S('x'): 1, S('y'): 2})
)");
    ASSERT_EQ(2u, keywords().extract()->size());
    expect_type_error(build(0), L"invalid keyword argument");
}

TEST_F(ExpandedCallBinding, KeywordErrorsPrecedeExcessAndMissingPositions)
{
    load(LR"(
def f(a, *, required): pass
(f, {'a': 5})
)");
    expect_type_error(build(2), L"invalid keyword argument");
    context.thread()->clear_pending_exception();
    keywords().extract()->clear();
    expect_type_error(build(2), L"wrong number of arguments");
    context.thread()->clear_pending_exception();
    expect_type_error(build(0), L"wrong number of arguments");
}

TEST_F(ExpandedCallBinding,
       ComparisonExceptionPrecedesExcessAndMissingArguments)
{
    load(LR"(
error = ValueError('comparison failed')
class S(str):
    def __hash__(self): return 10
    def __eq__(self, other): raise error
def f(a, *, required): pass
(f, {S('a'): 5}, error)
)");
    auto result = build(2);
    ASSERT_TRUE(result.has_exception());
    EXPECT_EQ(input(2),
              context.thread()->pending_exception_object().raw_value());
}

TEST_F(ExpandedCallBinding, TruthConversionExceptionPropagatesUnchanged)
{
    load(LR"(
error = KeyError('truth failed')
class Truth:
    def __bool__(self): raise error
class S(str):
    def __hash__(self): return 10
    def __eq__(self, other): return Truth()
def f(a): pass
(f, {S('a'): 5}, error)
)");
    auto result = build(0);
    ASSERT_TRUE(result.has_exception());
    EXPECT_EQ(input(2),
              context.thread()->pending_exception_object().raw_value());
}
