#include "runtime/expanded_call.h"

#include "builtin_types/dict.h"
#include "builtin_types/tuple.h"
#include "bytecode/bytecode_decoder.h"
#include "bytecode/code_object_builder.h"
#include "bytecode/code_object_print.h"
#include "import_system/module_global.h"
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

namespace
{
    class ExpandedCallExecution : public testing::Test
    {
    protected:
        void SetUp() override
        {
            auto name = context.vm().get_or_create_interned_string_value(
                L"<expanded-test>");
            Scope *scope =
                context.vm().make_immortal_internal_raw<Scope>(nullptr);
            CodeObjectBuilder code(&context.vm(), nullptr,
                                   context.vm().global_builtins_module(), scope,
                                   name);
            code.configure_positional_function(3);
            {
                CodeObjectBuilder::TemporaryReg outgoing(
                    code, 1, RegisterAlignment::CallFrame);
                ASSERT_TRUE(code.emit_call_expanded(0, 0, 1, outgoing));
            }
            ASSERT_TRUE(code.emit_return(0));
            auto function = code.finalize_immortal_function(&context.vm());
            ASSERT_FALSE(function.has_exception());
            caller = Optional<TValue<Function>>::some(function.value());
        }

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

        Value call()
        {
            return context.thread()->call_clovervm_function(
                caller.value().value(), input(0), input(1), input(2));
        }

        void check()
        {
            Owned<Value> result(call());
            ASSERT_FALSE(result.value().is_exception_marker());
            EXPECT_EQ(Value::True(),
                      context.thread()->call_clovervm_function(
                          TValue<Function>::from_value_assumed(input(3)),
                          result.value()));
        }

        void install_expanded_binding(TValue<Function> function)
        {
            ASSERT_TRUE(store_module_global(
                function.extract()
                    ->code_object.extract()
                    ->get_defining_module()
                    .extract(),
                context.vm().get_or_create_interned_string_value(L"expanded"),
                caller.value().value().raw_value()));
        }

        test::VmTestContext context;
        ThreadState::ActivationScope activation{context.thread()};
        Owned<Optional<TValue<Function>>> caller;
        Owned<Value> inputs{Value::None()};
    };
}  // namespace

TEST_F(ExpandedCallExecution, AppliesPositionalsKeywordsDefaultsAndCollectors)
{
    load(LR"(
def f(a, b=2, *args, c, d=4, **rest): return a, b, args, c, d, rest
def check(result):
    return result[0] == 1 and result[1] == 2 and result[2] == () and result[3] == 3 and result[4] == 4 and result[5]['extra'] == 5
(f, (1,), {'c': 3, 'extra': 5}, check)
)");
    check();
    check();
}

TEST_F(ExpandedCallExecution, PacksOnlyThePositionalSuffix)
{
    load(LR"(
def f(a, *args, b=9): return a, args, b
def check(result): return result[0] == 1 and result[1] == (2, 3) and result[2] == 9
(f, (1, 2, 3), {}, check)
)");
    check();
}

TEST_F(ExpandedCallExecution, PureCollectorsReceiveBothOriginalContainers)
{
    load(LR"(
def f(*args, **rest): return args, rest
args = (1, 2)
keys = {'x': 3}
def check(result): return result[0] is args and result[1] is keys
(f, args, keys, check)
)");
    check();
    const auto &cache = caller.value()
                            .value()
                            .extract()
                            ->code_object.extract()
                            ->inline_caches.expanded_call_caches[0];
    EXPECT_EQ(ExpandedCallMode::PureCollector, cache.mode);
}

TEST_F(ExpandedCallExecution,
       ReusesTransportWithNamedPositionalAndDefaultParameters)
{
    load(LR"(
def f(a, b=9, **rest): return a, b, rest
keys = {'x': 3}
def check(result): return result[0] == 1 and result[1] == 9 and result[2] is keys
(f, (1,), keys, check)
)");
    check();
}

TEST_F(ExpandedCallExecution, BuildsFreshLeftoversPreservingKeysAndOrder)
{
    load(LR"(
class S(str):
    def __hash__(self): return str.__hash__(self)
key = S('extra')
keys = {'a': 1, key: 2, 'last': 3}
def f(a, **rest): return a, rest
def check(result):
    rest = result[1]
    names = []
    for name in rest.keys(): names.append(name)
    return result[0] == 1 and rest is not keys and len(keys) == 3 and len(rest) == 2 and names[0] is key and names[1] == 'last' and rest[key] == 2
(f, (), keys, check)
)");
    check();
}

TEST_F(ExpandedCallExecution, AllMatchedKeywordsProduceFreshEmptyCollector)
{
    load(LR"(
keys = {'a': 1}
def f(a, **rest): return a, rest
def check(result): return result[0] == 1 and len(result[1]) == 0 and result[1] is not keys and len(keys) == 1
(f, (), keys, check)
)");
    check();
}

TEST_F(ExpandedCallExecution, NonStringKeywordsNeverEnterPureCollector)
{
    load(LR"(
entered = False
def f(*args, **rest):
    global entered
    entered = True
def check(): return not entered
(f, (), {42: 1}, check)
)");
    EXPECT_TRUE(call().is_exception_marker());
    EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
              context.thread()
                  ->pending_exception_object()
                  .extract()
                  ->get_shape()
                  ->get_class());
    context.thread()->clear_pending_exception();
    EXPECT_EQ(Value::True(),
              context.thread()->call_clovervm_function(
                  TValue<Function>::from_value_assumed(input(3))));
}

TEST_F(ExpandedCallExecution, SupportsInitAndNewOnlyConstructorThunks)
{
    load(LR"(
class C:
    def __init__(self, a, *, b=7): self.value = a + b
def check(result): return isinstance(result, C) and result.value == 10
(C, (), {'a': 3}, check)
)");
    check();
    check();
    load(LR"(
class C:
    def __new__(cls, a, *, b=7): return a + b
def check(result): return result == 10
(C, (3,), {}, check)
)");
    check();
}

TEST_F(ExpandedCallExecution, ConstructorLookupInvalidatesAfterInitReplacement)
{
    load(LR"(
class C:
    def __init__(self, a): self.value = a
def replace():
    def init(self, *, b): self.value = b + 10
    C.__init__ = init
def verify(value): return value.value == 14
(C, (), {'a': 3}, replace, verify)
)");
    Owned<Value> first(call());
    ASSERT_FALSE(first.value().is_exception_marker());
    EXPECT_EQ(Value::None(),
              context.thread()->call_clovervm_function(
                  TValue<Function>::from_value_assumed(input(3))));
    Owned<TValue<Dict>> keys(context.thread()->make_object_value<Dict>());
    keys.extract()->string_keyed_insert(
        context.vm().get_or_create_interned_string_value(L"b"),
        Value::from_smi(4));
    Owned<Value> second(context.thread()->call_clovervm_function(
        caller.value().value(), input(0), input(1), keys.raw_value()));
    ASSERT_FALSE(second.value().is_exception_marker());
    EXPECT_NE(first.value(), second.value());
    EXPECT_EQ(Value::True(), context.thread()->call_clovervm_function(
                                 TValue<Function>::from_value_assumed(input(4)),
                                 second.value()));
    const auto &cache = caller.value()
                            .value()
                            .extract()
                            ->code_object.extract()
                            ->inline_caches.expanded_call_caches[0];
    ASSERT_NE(nullptr, cache.validity_cell);
    EXPECT_TRUE(cache.validity_cell->is_valid());
}

TEST_F(ExpandedCallExecution,
       NestedExpandedCallsDuringComparisonPreserveCallerRegisters)
{
    load(LR"(
class S(str):
    def __hash__(self): return 123
    def __eq__(self, other):
        def nested(x): return x + 1
        assert expanded(nested, (40,), {}) == 41
        return str.__eq__(self, other)
def target(a, *, b=2): return a + b
keys = {S('a'): 3}
def outer():
    live = [10, 20]
    result = expanded(target, (), keys)
    assert live[0] == 10 and live[1] == 20
    return result
def check(result): return result == 5
(outer, (), {}, check)
)");
    install_expanded_binding(TValue<Function>::from_value_assumed(input(0)));
    check();
    check();
}

TEST_F(ExpandedCallExecution, BindingAndCalleeExceptionsUnwindToPythonCaller)
{
    load(LR"(
error = ValueError('comparison failed')
class S(str):
    def __hash__(self): return 123
    def __eq__(self, other): raise error
def target(a): raise KeyError('callee failed')
def outer():
    live = [10, 20]
    try:
        expanded(target, (), {S('a'): 3})
    except ValueError as caught:
        assert caught is error
    else:
        assert False
    try:
        expanded(target, (1,), {})
    except KeyError:
        pass
    else:
        assert False
    return live[0] + live[1]
def check(result): return result == 30
(outer, (), {}, check)
)");
    install_expanded_binding(TValue<Function>::from_value_assumed(input(0)));
    check();
}

TEST_F(ExpandedCallExecution,
       CollectorFailurePrecedesEntryAndPreservesTransport)
{
    load(LR"(
armed = False
entered = False
error = ValueError('collector hash failed')
class S(str):
    def __hash__(self):
        if armed: raise error
        return 123
    def __eq__(self, other): return False
key = S('extra')
keys = {'a': 1, key: 2}
armed = True
def target(a, **rest):
    global entered
    entered = True
def check(): return not entered and len(keys) == 2
(target, (), keys, check, error)
)");
    EXPECT_TRUE(call().is_exception_marker());
    EXPECT_EQ(input(4),
              context.thread()->pending_exception_object().raw_value());
    context.thread()->clear_pending_exception();
    EXPECT_EQ(Value::True(),
              context.thread()->call_clovervm_function(
                  TValue<Function>::from_value_assumed(input(3))));
}

TEST_F(ExpandedCallExecution, BytecodeMetadataDescribesAdjacentInputsAndCache)
{
    CodeObject *code = caller.value().value().extract()->code_object.extract();
    BytecodeDecoder decoder(*code);
    bool found = false;
    for(const auto &block: decoder.blocks())
    {
        for(const auto &instruction: block.instructions())
        {
            if(instruction.semantic_opcode() != Bytecode::CallExpanded)
                continue;
            found = true;
            EXPECT_EQ(5u,
                      instruction.next_pc_offset() - instruction.pc_offset());
            ASSERT_EQ(4u, instruction.operands().size());
            EXPECT_EQ(0u, instruction.operands()[0].value);
            EXPECT_EQ(1u, instruction.operands()[1].value);
            EXPECT_EQ(BytecodeOperandKind::ExpandedCallCache,
                      instruction.operands()[3].kind);
            EXPECT_EQ(3u, instruction.sources().size());
            EXPECT_EQ(1u, instruction.destinations().size());
            EXPECT_NE(nullptr, instruction.expanded_call_cache());
        }
    }
    EXPECT_TRUE(found);
    EXPECT_NE(std::string::npos, fmt::format("{}", *code).find("CallExpanded"));
}

TEST_F(ExpandedCallExecution, CachedCallableStillBindsChangingCountsAndNames)
{
    load(LR"(
def f(a=1, b=2): return a + b
(f, (), {'a': 3})
)");
    EXPECT_EQ(Value::from_smi(5), call());
    Owned<TValue<Tuple>> positional(
        context.thread()->make_object_value<Tuple>(1));
    positional.extract()->initialize_item_unchecked(0, Value::from_smi(7));
    Owned<TValue<Dict>> keywords(context.thread()->make_object_value<Dict>());
    keywords.extract()->string_keyed_insert(
        context.vm().get_or_create_interned_string_value(L"b"),
        Value::from_smi(8));
    EXPECT_EQ(Value::from_smi(15),
              context.thread()->call_clovervm_function(
                  caller.value().value(), input(0), positional.raw_value(),
                  keywords.raw_value()));
}

TEST_F(ExpandedCallExecution, CollectorCallbacksCanMakeNestedExpandedCalls)
{
    load(LR"(
armed = False
class S(str):
    def __hash__(self):
        if armed:
            def nested(*args, **rest): return args[0]
            assert expanded(nested, (41,), {}) == 41
        return 123
    def __eq__(self, other): return False
keys = {'b': 9, S('extra'): 2}
armed = True
def target(a, *args, b, **rest): return a, args, b, rest
def actual(): return expanded(target, (1, 2, 3), keys)
def check(result): return result[0] == 1 and result[1] == (2, 3) and result[2] == 9 and len(result[3]) == 1
(actual, (), {}, check)
)");
    install_expanded_binding(TValue<Function>::from_value_assumed(input(0)));
    check();
}

namespace
{
    class ExpandedCallSyntax : public testing::Test
    {
    protected:
        void run(const wchar_t *source)
        {
            Value result = context.run_file(source);
            if(result.is_exception_marker())
            {
                auto exception = context.thread()->pending_exception_object();
                FAIL() << string_as_wchar_t(exception.extract()
                                                ->get_shape()
                                                ->get_class()
                                                ->get_name())
                       << L": "
                       << string_as_wchar_t(
                              test::native_exception_message(exception));
            }
            EXPECT_EQ(Value::True(), result);
        }
        test::VmTestContext context;
        ThreadState::ActivationScope activation{context.thread()};
    };
}  // namespace

TEST_F(ExpandedCallSyntax, MixedRepeatedSourcesBindNormally)
{
    run(LR"(
def f(a, b, *rest, c=5, **kw):
    assert a == 1 and b == 2
    assert rest == (3, 4) and c == 6
    assert len(kw) == 2 and kw['x'] == 7 and kw['y'] == 8
f(1, *[2, 3], 4, *(), c=6, **{'x': 7}, y=8)
True
)");
}

TEST_F(ExpandedCallSyntax,
       PureCollectorsReuseExactTupleAndFreshKeywordTransport)
{
    run(LR"(
def args_only(*args, **kw): return args, kw
values = (1, 2)
source = {'x': 3}
first = args_only(*values, **source)
assert first[0] is values
assert first[1] is not source and first[1]['x'] == 3
second = args_only(*values)
third = args_only(*values)
assert second[0] is values and len(second[1]) == 0
assert second[1] is not third[1]
assert args_only(**{})[0] == ()
True
)");
}

TEST_F(ExpandedCallSyntax, GenericMappingAndStringSubclassKeysArePreserved)
{
    run(LR"(
class S(str):
    def __hash__(self): return str.__hash__(self)
key = S('extra')
events = []
class Mapping:
    def keys(self):
        events.append('keys')
        return ['a', key]
    def __getitem__(self, name):
        events.append(name)
        return 1 if name == 'a' else 2
def f(a, **rest): return a, rest
result = f(**Mapping())
assert result[0] == 1 and result[1][key] == 2
assert next(iter(result[1].keys())) is key
assert events == ['keys', 'a', key]
True
)");
}

TEST_F(ExpandedCallSyntax, LoneStarIsEvaluatedEarlyButIteratedAfterKeywords)
{
    run(LR"(
events = []
class I:
    def __iter__(self):
        events.append('iterate')
        return iter((1,))
def source():
    events.append('source')
    return I()
def keyword():
    events.append('keyword')
    return 2
def f(*args, **kw): return args, kw
result = f(*source(), k=keyword())
assert result[0] == (1,) and result[1]['k'] == 2
assert events == ['source', 'keyword', 'iterate']
events.clear()
result = f(k=keyword(), *source())
assert events == ['source', 'keyword', 'iterate']
True
)");
}

TEST_F(ExpandedCallSyntax, MixedStarsAreIteratedBeforeKeywordEvaluation)
{
    run(LR"(
events = []
class I:
    def __init__(self, name): self.name = name
    def __iter__(self):
        events.append(self.name)
        return iter((1,))
def keyword():
    events.append('keyword')
    return 2
def f(*args, **kw): return args
assert f(0, *I('first'), k=keyword()) == (0, 1)
assert events == ['first', 'keyword']
events.clear()
assert f(*I('first'), k=keyword(), *I('second')) == (1, 1)
assert events == ['first', 'second', 'keyword']
True
)");
}

TEST_F(ExpandedCallSyntax,
       ExplicitKeywordRunIsEvaluatedBeforeDuplicateInsertion)
{
    run(LR"(
events = []
def value(name):
    events.append(name)
    return 1
def later():
    events.append('later')
    return {}
def f(**kw): pass
try:
    f(**{'a': 0}, a=value('a'), b=value('b'), **later())
except TypeError:
    pass
else:
    assert False
assert events == ['a', 'b']
True
)");
}

TEST_F(ExpandedCallSyntax, MappingDuplicateStopsBeforeLaterKeywordSources)
{
    run(LR"(
events = []
def later():
    events.append('later')
    return 1
def f(**kw): pass
try:
    f(**{'a': 0}, **{'a': 1}, b=later())
except TypeError:
    pass
else:
    assert False
assert events == []
True
)");
}

TEST_F(ExpandedCallSyntax,
       NonStringValidationIsDeferredUntilAfterKeywordConstruction)
{
    run(LR"(
events = []
def later():
    events.append('later')
    return 1
def failure(): raise ValueError('later mapping error')
def f(**kw): pass
try:
    f(**{42: 0}, b=later())
except TypeError:
    pass
else:
    assert False
assert events == ['later']
try:
    f(**{42: 0}, **failure())
except ValueError:
    pass
else:
    assert False
True
)");
}

TEST_F(ExpandedCallSyntax, IterationFailureTimingDependsOnLoneOrMixedStar)
{
    run(LR"(
events = []
class Broken:
    def __iter__(self): raise ValueError('broken iterator')
def keyword():
    events.append('keyword')
    return 1
def f(*args, **kw): pass
try:
    f(*Broken(), k=keyword())
except ValueError:
    pass
else:
    assert False
assert events == ['keyword']
events.clear()
try:
    f(0, *Broken(), k=keyword())
except ValueError:
    pass
else:
    assert False
assert events == []
True
)");
}

TEST_F(ExpandedCallSyntax, ConstructorsDefaultsAndPositionalOnlyParameters)
{
    run(LR"(
class C:
    def __init__(self, a, /, *, b=7): self.value = a + b
assert C(*[3], **{}).value == 10
assert C(*[3], **{'b': 4}).value == 7
class N:
    def __new__(cls, a, *, b=7): return a + b
assert N(*[3], **{} ) == 10
def f(a, /, **rest): return a, rest
result = f(*[3], **{'a': 4})
assert result[0] == 3 and result[1]['a'] == 4
True
)");
}

TEST_F(ExpandedCallSyntax, ListSubclassUsesItsIteratorAndEmptySourcesWork)
{
    run(LR"(
class L(list):
    def __iter__(self): return iter((9,))
def f(*args, **kw): return args
assert f(*L()) == (9,)
assert f(*(), *[], **{}, **{}) == ()
assert f(1, 2, **{}) == (1, 2)
True
)");
}

TEST_F(ExpandedCallSyntax, StarredReadPrecedesKeywordWalrusAssignment)
{
    run(LR"(
def f(*args, **kw): raise AssertionError('callee must not run')
def unbound():
    return f(k=(x := 1), *(x,))
try:
    unbound()
except NameError as error:
    # Clover currently reports unbound locals as NameError.
    assert 'x' in error.args[0]
else:
    assert False
True
)");
}

TEST_F(ExpandedCallSyntax, StarredWalrusAssignmentPrecedesKeywordRead)
{
    run(LR"(
def f(*args, **kw): return args, kw
def assigned():
    result = f(k=x, *((x := 2),))
    assert x == 2
    return result
result = assigned()
assert result[0] == (2,) and result[1]['k'] == 2
True
)");
}

TEST_F(ExpandedCallSyntax, MethodsBindReceiverPositionalsKeywordsAndDefaults)
{
    run(LR"(
class C:
    def m(self, a, b=2, *args, c=3, **kw):
        assert self is obj
        return a, b, args, c, kw
obj = C()
result = obj.m(1, *[2, 4], c=5, **{'extra': 6})
assert result[0] == 1 and result[1] == 2 and result[2] == (4,)
assert result[3] == 5 and result[4]['extra'] == 6
assert obj.m(*[1])[1] == 2
True
)");
}

TEST_F(ExpandedCallSyntax, MethodsReuseTheTupleSuffixAfterSelf)
{
    run(LR"(
class C:
    def m(self, *args, **kw): return args, kw
obj = C()
values = (1, 2)
source = {'x': 3}
result = obj.m(*values, **source)
assert result[0] is values
assert result[1] is not source and result[1]['x'] == 3
True
)");
}

TEST_F(ExpandedCallSyntax,
       MethodPureCollectorsIncludeSelfAndHandleUnboundTargets)
{
    run(LR"(
def collect(*args, **kw): return args, kw
class Bound: m = collect
class Unbound: pass
bound = Bound()
unbound = Unbound()
unbound.m = collect
values = (1, 2)
def call(obj): return obj.m(*values)
result = call(bound)
assert result[0][0] is bound and result[0][1:] == values
assert call(unbound)[0] is values
assert call(bound)[0][0] is bound
True
)");
}

TEST_F(ExpandedCallSyntax, InstanceAndModuleAttributesDoNotAcquireSelf)
{
    run(LR"(
def plain(a, *, b=7): return a + b
class C: pass
obj = C()
obj.m = plain
assert obj.m(*[3], **{'b': 4}) == 7
import math
assert math.sqrt(*(9,)) == 3
True
)");
}

TEST_F(ExpandedCallSyntax, InheritedMethodCacheObservesReplacementAndDeletion)
{
    run(LR"(
class Base:
    def m(self, a): return a + 1
class C(Base): pass
obj = C()
def call(): return obj.m(*(3,))
assert call() == 4
def replacement(self, a): return a + 10
C.m = replacement
assert call() == 13
del C.m
assert call() == 4
True
)");
}

TEST_F(ExpandedCallSyntax, MethodSelfConflictsAndPositionalOnlyNames)
{
    run(LR"(
class C:
    def m(self, **rest): return rest
obj = C()
try:
    obj.m(*(), self=1)
except TypeError:
    pass
else:
    assert False
class P:
    def m(self, /, **rest): return rest
assert P().m(*(), self=2)['self'] == 2
True
)");
}

TEST_F(ExpandedCallSyntax, ExpandedMethodsPreserveAcceptedLateLookupTiming)
{
    run(LR"(
events = []
class C:
    def m(self): return 'old'
obj = C()
def replacement(self): return 'new'
def arguments():
    events.append('arguments')
    C.m = replacement
    return ()
# Accepted mismatch: lookup follows expansion, as for ordinary method calls.
assert obj.m(*arguments()) == 'new'
try:
    obj.missing(*arguments())
except AttributeError:
    pass
else:
    assert False
assert events == ['arguments', 'arguments']
True
)");
}

TEST_F(ExpandedCallSyntax,
       MethodBindingCallbacksRetainTheSelectedFunctionAndReceiver)
{
    run(LR"(
class C:
    def __init__(self): self.offset = 10
    def m(self, a): return a + self.offset
class Helper:
    def m(self, a): return a + 1
helper = Helper()
obj = C()
def replacement(self, a): return a + 100
class S(str):
    def __hash__(self): return 123
    def __eq__(self, other):
        assert helper.m(*(8,)) == 9
        C.m = replacement
        return str.__eq__(self, other)
assert obj.m(**{S('a'): 3}) == 13
assert obj.m(*(3,)) == 103
True
)");
}

TEST_F(ExpandedCallSyntax, MethodExceptionsUnwindWithoutEnteringTheCalleeEarly)
{
    run(LR"(
entered = False
error = ValueError('comparison failed')
class C:
    def m(self, a):
        global entered
        entered = True
        raise KeyError('callee failed')
class S(str):
    def __hash__(self): return 123
    def __eq__(self, other): raise error
obj = C()
try:
    obj.m(**{S('a'): 3})
except ValueError as caught:
    assert caught is error
else:
    assert False
assert not entered
try:
    obj.m(*(3,))
except KeyError:
    pass
else:
    assert False
assert entered
True
)");
}

TEST_F(ExpandedCallSyntax, BuiltinMethodsAndNoneReceiverSupportExpansion)
{
    run(LR"(
values = []
values.append(*[3])
values.extend(*[[4, 5]])
assert values == [3, 4, 5]
assert {'a': 7}.get(*['a']) == 7
assert None.__repr__(*()) == 'None'
True
)");
}
