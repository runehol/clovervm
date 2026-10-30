#include "builtin_types/bytearray.h"
#include "builtin_types/bytes.h"
#include "memory/global_heap.h"
#include "memory/heap_constants.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "platform/file_io.h"
#include "runtime/trusted_handler.h"
#include "test_helpers.h"
#include <fstream>
#include <iterator>
#include <unistd.h>

using namespace cl;

TEST(ByteArray, LayoutTracesOwnerBackingAndIterator)
{
    const auto &release = release_descriptor_for(NativeLayoutId::ByteArray);
    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(Object::native_static_release_count() +
                  RawArray<uint8_t>::embedded_value_count,
              release.static_release_count);
    EXPECT_EQ(nullptr, release.custom_release);
    EXPECT_EQ(sizeof(ByteArray),
              object_size_descriptor_for(NativeLayoutId::ByteArray)
                  .static_size_in_bytes);
    EXPECT_EQ(Object::native_static_release_count() + 2,
              release_descriptor_for(NativeLayoutId::ByteArrayIterator)
                  .static_release_count);
    EXPECT_EQ(ReleaseKind::StaticSpan,
              release_descriptor_for(NativeLayoutId::RawArrayBacking).kind);
}

TEST(ByteArray, SemanticsAndCallbacksSurviveEverySafepointReclamation)
{
    for(const char *path: {"tests/python/builtin_bytearray.py",
                           "tests/python/builtin_bytearray_callbacks.py",
                           "tests/python/builtin_bytearray_comparisons.py",
                           "tests/python/builtin_bytearray_errors.py",
                           "tests/python/builtin_bytearray_copies.py",
                           "tests/python/builtin_bytearray_slices.py",
                           "tests/python/builtin_bytearray_limits.py"})
    {
        SCOPED_TRACE(path);
        std::ifstream input(path);
        ASSERT_TRUE(input);
        std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
        std::wstring wide(source.begin(), source.end());
        test::VmTestContext context;
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        EXPECT_FALSE(context.run_file(wide.c_str()).is_exception_marker());
    }
}

TEST(ByteArray, SynchronousWritableBorrowPreservesLengthAndUnwrittenSuffix)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Value result = context.run_file(L"bytearray(5)\n");
    ASSERT_FALSE(result.is_exception_marker());
    Owned<TValue<ByteArray>> owner(
        TValue<ByteArray>::from_value_assumed(result));
    context.vm().run_heap_reclamation();
    int descriptors[2];
    ASSERT_EQ(0, pipe(descriptors));
    ASSERT_EQ(2, write(descriptors[1], "AB", 2));
    FileResult read{};
    // Pending collection is deliberately left pending throughout the native
    // borrow; neither the borrow nor platform read enters a safepoint.
    context.vm().request_safepoint();
    with_bytearray_writable_bytes(
        owner.value(), [&](std::span<uint8_t> bytes) noexcept {
            read =
                platform_file_read(descriptors[0], bytes.data(), bytes.size());
        });
    EXPECT_EQ(0, read.error);
    EXPECT_EQ(2, read.value);
    EXPECT_EQ(5, owner.extract()->size());
    const uint8_t expected[] = {'A', 'B', 0, 0, 0};
    EXPECT_TRUE(std::equal(owner.extract()->view().begin(),
                           owner.extract()->view().end(), std::begin(expected),
                           std::end(expected)));
    context.vm().run_heap_reclamation();
    EXPECT_EQ('A', owner.extract()->view()[0]);
    EXPECT_EQ(0, close(descriptors[0]));
    EXPECT_EQ(0, close(descriptors[1]));
    owner.extract()->items.clear();
    bool entered = false;
    with_bytearray_writable_bytes(
        owner.value(),
        [&](std::span<uint8_t> bytes) noexcept { entered = bytes.empty(); });
    EXPECT_TRUE(entered);
}

TEST(ByteArray, ExhaustionReleasesSourceAndItsBackingWhileIteratorStaysAlive)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<ByteArray>> source(thread->make_object_value<ByteArray>());
    // Give a one-item source a dedicated large backing so its release can be
    // observed independently of unrelated objects in ordinary allocation slabs.
    source.extract()->items.resize(2 * LargeAllocationSize);
    source.extract()->items.resize(1);
    source.extract()->items[0] = 7;
    const void *backing_address = source.extract()->view().data();
    Owned<TValue<ByteArrayIterator>> iterator(
        thread->make_object_value<ByteArrayIterator>(source.value()));
    auto next_name =
        context.vm().get_or_create_interned_string_value(L"__next__");
    EXPECT_EQ(2, source.extract()->refcount);
    context.vm().request_safepoint();
    EXPECT_EQ(Value::from_smi(7),
              thread->call_clovervm_method(iterator.raw_value(), next_name));
    // Returning the last item is not yet exhaustion.
    EXPECT_EQ(2, source.extract()->refcount);
    ASSERT_TRUE(thread->call_clovervm_method(iterator.raw_value(), next_name)
                    .is_exception_marker());
    ASSERT_EQ(PendingExceptionKind::Object, thread->pending_exception_kind());
    EXPECT_EQ(
        thread->class_for_native_layout(NativeLayoutId::StopIteration),
        thread->class_of_value(thread->pending_exception_object().raw_value()));
    thread->clear_pending_exception();
    EXPECT_EQ(1, source.extract()->refcount);
    source.extract()->items.push_back(9);
    ASSERT_TRUE(thread->call_clovervm_method(iterator.raw_value(), next_name)
                    .is_exception_marker());
    thread->clear_pending_exception();
    EXPECT_EQ(1, source.extract()->refcount);
    source = thread->make_object_value<ByteArray>();
    ASSERT_EQ(thread->clover_frame_sentinel(), thread->clover_frame_frontier());
    thread->publish_safepoint_scan_record(thread->clover_frame_sentinel(),
                                          Value::not_present());
    context.vm().run_heap_reclamation();
    context.vm().run_heap_reclamation();
    EXPECT_FALSE(context.vm()
                     .get_refcounted_global_heap()
                     .has_slab_for_address_for_testing(backing_address));
    ASSERT_TRUE(thread->call_clovervm_method(iterator.raw_value(), next_name)
                    .is_exception_marker());
    thread->clear_pending_exception();
}

TEST(ByteArray, StoredPayloadExtensionReusesCapacityWithoutHeapAllocation)
{
    // bytes, another bytearray, self, and both empty payload forms.
    for(int kind = 0; kind < 5; ++kind)
    {
        SCOPED_TRACE(kind);
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        Owned<TValue<ByteArray>> target(thread->make_object_value<ByteArray>());
        target.extract()->items.reserve(64);
        target.extract()->items.resize(2);
        target.extract()->items[0] = 'A';
        target.extract()->items[1] = 'B';
        Owned<Value> source(target.raw_value());
        const uint8_t payload[] = {'C', 'D'};
        if(kind == 0 || kind == 3)
            source = thread
                         ->make_object_value<Bytes>(
                             kind == 0 ? std::span<const uint8_t>(payload)
                                       : std::span<const uint8_t>{})
                         .raw_value();
        else if(kind == 1 || kind == 4)
        {
            auto other = thread->make_object_value<ByteArray>();
            source = other.raw_value();
            if(kind == 1)
            {
                other.extract()->items.resize(2);
                other.extract()->items[0] = 'C';
                other.extract()->items[1] = 'D';
            }
        }
        auto extend_name =
            context.vm().get_or_create_interned_string_value(L"extend");
        // Warm the method and entry adapter before measuring the steady path.
        ASSERT_EQ(Value::None(),
                  thread->call_clovervm_method(target.raw_value(), extend_name,
                                               source.value()));
        target.extract()->items.resize(2);
        context.vm().clear_safepoint_request();
        const void *backing = target.extract()->view().data();
        auto &heap = context.vm().get_refcounted_global_heap();
        auto &interned = context.vm().get_interned_global_heap();
        uint64_t objects_before = heap.count_valid_objects_slow();
        uint64_t interned_before = interned.count_valid_objects_slow();
        size_t safepoints = 0;
        context.vm().set_safepoint_callback_for_testing(
            [](void *state, ThreadState *, Value, Value *, CodeObject *,
               uint32_t, const SafepointScanRecord &) {
                ++*static_cast<size_t *>(state);
            },
            &safepoints);
        Value result = thread->call_clovervm_method(
            target.raw_value(), extend_name, source.value());
        context.vm().set_safepoint_callback_for_testing(nullptr, nullptr);
        ASSERT_EQ(Value::None(), result);
        EXPECT_EQ(0u, safepoints);
        EXPECT_EQ(objects_before, heap.count_valid_objects_slow());
        EXPECT_EQ(interned_before, interned.count_valid_objects_slow());
        EXPECT_EQ(backing, target.extract()->view().data());
        EXPECT_EQ(64u, target.extract()->items.capacity());
        EXPECT_EQ(kind >= 3 ? 2u : 4u, target.extract()->size());
        EXPECT_EQ('A', target.extract()->items[0]);
        EXPECT_EQ('B', target.extract()->items[1]);
        if(kind < 3)
        {
            EXPECT_EQ(kind == 2 ? 'A' : 'C', target.extract()->items[2]);
            EXPECT_EQ(kind == 2 ? 'B' : 'D', target.extract()->items[3]);
        }
    }
}

TEST(ByteArray, SliceSetterCacheCopiesStoredPayloadWithoutAllocating)
{
    for(bool from_bytes: {false, true})
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        Value function_value =
            context.run_file(L"def assign(target, key, value):\n"
                             L"    target[key] = value\n"
                             L"assign\n");
        ASSERT_TRUE(has_native_layout<Function>(function_value));
        Owned<TValue<Function>> function(
            TValue<Function>::from_value_assumed(function_value));
        Owned<Value> key(context.run_file(L"slice(1, 1)\n"));
        ASSERT_FALSE(key.value().is_exception_marker());
        Owned<TValue<ByteArray>> target(thread->make_object_value<ByteArray>());
        target.extract()->items.reserve(64);
        target.extract()->items.resize(2);
        target.extract()->items[0] = 'A';
        target.extract()->items[1] = 'B';
        Owned<Value> source(
            context.run_file(from_bytes ? L"b'CD'\n" : L"bytearray(b'CD')\n"));
        ASSERT_FALSE(source.value().is_exception_marker());
        ASSERT_EQ(Value::None(), thread->call_clovervm_function(
                                     function.value(), target.raw_value(),
                                     key.value(), source.value()));
        auto *code = function.extract()->code_object.extract();
        ASSERT_EQ(1u, code->inline_caches.operator_caches.size());
        const auto &cache = code->inline_caches.operator_caches[0];
        ASSERT_FALSE(cache.trusted_handler.is_null());
        auto metadata = context.vm().trusted_handler_metadata(
            cache.trusted_handler.target(TrustedHandlerArity::Ternary));
        ASSERT_TRUE(metadata.has_value());
        EXPECT_EQ(TrustedHandlerEffects::Raise |
                      TrustedHandlerEffects::Allocate |
                      TrustedHandlerEffects::Safepoint |
                      TrustedHandlerEffects::CallPython,
                  metadata->effects);
        target.extract()->items.resize(2);
        target.extract()->items[1] = 'B';
        context.vm().clear_safepoint_request();
        const void *backing = target.extract()->items.data();
        auto &heap = context.vm().get_refcounted_global_heap();
        auto &interned = context.vm().get_interned_global_heap();
        auto objects_before = heap.count_valid_objects_slow();
        auto interned_before = interned.count_valid_objects_slow();
        ASSERT_EQ(Value::None(), thread->call_clovervm_function(
                                     function.value(), target.raw_value(),
                                     key.value(), source.value()));
        EXPECT_EQ(objects_before, heap.count_valid_objects_slow());
        EXPECT_EQ(interned_before, interned.count_valid_objects_slow());
        EXPECT_EQ(backing, target.extract()->items.data());
        EXPECT_EQ(64u, target.extract()->items.capacity());
        const uint8_t expected[] = {'A', 'C', 'D', 'B'};
        EXPECT_TRUE(std::equal(target.extract()->view().begin(),
                               target.extract()->view().end(),
                               std::begin(expected), std::end(expected)));
    }
}

TEST(ByteArray, ItemOperatorsCacheSmiAndSliceHandlers)
{
    for(const wchar_t *body: {L"def op(a, k):\n    return a[k]\nop\n",
                              L"def op(a, k):\n    a[k] = 65\nop\n",
                              L"def op(a, k):\n    del a[k]\nop\n",
                              L"def op(a, k):\n    a[k] = b'AB'\nop\n"})
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        Owned<TValue<Function>> function(
            TValue<Function>::from_value_assumed(context.run_file(body)));
        bool set = std::wstring(body).find(L"a[k] =") != std::wstring::npos;
        bool scalar_set =
            std::wstring(body).find(L"= 65") != std::wstring::npos;
        for(const wchar_t *key_source:
            {L"0\n", L"slice(None)\n", L"slice(None, None, -1)\n"})
        {
            if((scalar_set && key_source[0] != L'0') ||
               (set && !scalar_set && key_source[0] == L'0'))
                continue;
            Owned<Value> key(context.run_file(key_source));
            for(int repeat = 0; repeat < 2; ++repeat)
            {
                Owned<Value> target(context.run_file(L"bytearray(b'CD')\n"));
                ASSERT_FALSE(thread
                                 ->call_clovervm_function(function.value(),
                                                          target.value(),
                                                          key.value())
                                 .is_exception_marker());
                const auto &caches = function.extract()
                                         ->code_object.extract()
                                         ->inline_caches.operator_caches;
                ASSERT_EQ(1u, caches.size());
                ASSERT_FALSE(caches[0].trusted_handler.is_null());
                EXPECT_TRUE(
                    caches[0].matches_binary(target.value(), key.value()));
            }
        }
    }
}

TEST(ByteArray, ConstructionAndBasicMutationSurviveCollection)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    Value result = context.run_file(L"assert len(bytearray()) == 0\n"
                                    L"assert len(bytearray(3)) == 3\n"
                                    L"a = bytearray([65, 66, 67])\n"
                                    L"copy = bytearray(a)\n"
                                    L"a.clear()\n"
                                    L"assert len(a) == 0 and len(copy) == 3\n"
                                    L"a.append(68)\n"
                                    L"copy.append(69)\n"
                                    L"copy\n");
    ASSERT_TRUE(has_native_layout<ByteArray>(result));
    const uint8_t expected[] = {'A', 'B', 'C', 'E'};
    auto view = result.get_ptr<ByteArray>()->view();
    EXPECT_TRUE(std::equal(view.begin(), view.end(), std::begin(expected),
                           std::end(expected)));
}

TEST(ByteArray, IterationRepresentationAndImmutableCopiesSurviveCollection)
{
    test::VmTestContext context;
    context.vm().set_fire_every_safepoint_for_testing(true);
    context.vm().request_safepoint();
    EXPECT_FALSE(context
                     .run_file(L"a = bytearray(b'AB')\n"
                               L"assert repr(a) == \"bytearray(b'AB')\"\n"
                               L"assert str(a) == repr(a)\n"
                               L"assert list(a) == [65, 66]\n"
                               L"b = bytes(a)\n"
                               L"assert a == b and b == a and a != b'AC'\n"
                               L"a.clear()\n"
                               L"assert b == b'AB' and a == b''\n")
                     .is_exception_marker());
}

TEST(ByteArray, AllComparisonCachesAndReflectedHandlersPreserveOrdering)
{
    const wchar_t *operators[] = {L"==", L"!=", L"<", L"<=", L">", L">="};
    const bool expected[] = {false, true, true, true, false, false};
    const wchar_t *reflected[] = {L"__eq__", L"__ne__", L"__gt__",
                                  L"__ge__", L"__lt__", L"__le__"};
    for(size_t op = 0; op < std::size(operators); ++op)
    {
        SCOPED_TRACE(op);
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        std::wstring source = L"def compare(left, right):\n    return left ";
        source += operators[op];
        source += L" right\ncompare\n";
        Owned<TValue<Function>> function(TValue<Function>::from_value_assumed(
            context.run_file(source.c_str())));
        for(int kind = 0; kind < 3; ++kind)
        {
            SCOPED_TRACE(kind);
            Owned<Value> left(context.run_file(
                kind == 2 ? L"b'ab'\n" : L"bytearray(b'ab')\n"));
            Owned<Value> right(
                context.run_file(kind == 1 ? L"b'b'\n" : L"bytearray(b'b')\n"));
            for(int repeat = 0; repeat < 2; ++repeat)
            {
                EXPECT_EQ(expected[op] ? Value::True() : Value::False(),
                          thread->call_clovervm_function(
                              function.value(), left.value(), right.value()));
                const auto &caches = function.extract()
                                         ->code_object.extract()
                                         ->inline_caches.operator_caches;
                ASSERT_EQ(1u, caches.size());
                if(kind == 2)
                {
                    // The first bytes method remains the cached entry; its
                    // NotImplemented continuation resolves the reflected
                    // handler.
                    ASSERT_NE(nullptr, caches[0].function);
                    Value method =
                        context.vm().bytearray_class()->get_own_property(
                            context.vm().get_or_create_interned_string_value(
                                reflected[op]));
                    auto resolver = method.get_ptr<Function>()
                                        ->code_object.extract()
                                        ->trusted_handler_resolver;
                    ASSERT_NE(nullptr, resolver);
                    auto resolution = resolver(
                        &context.vm(), ShapeKey::from_value(left.value()),
                        ShapeKey::from_value(right.value()),
                        TrustedHandlerOperandOrder::Reflected,
                        TrustedHandlerArity::Binary);
                    ASSERT_TRUE(resolution.has_trusted_handler());
                    EXPECT_EQ(
                        expected[op] ? Value::True() : Value::False(),
                        resolution.binary(thread, left.value(), right.value()));
                    auto metadata = context.vm().trusted_handler_metadata(
                        erase_trusted_handler_target(resolution.binary));
                    ASSERT_TRUE(metadata.has_value());
                    EXPECT_EQ(TrustedHandlerEffects::None, metadata->effects);
                    continue;
                }
                ASSERT_FALSE(caches[0].trusted_handler.is_null());
                EXPECT_TRUE(
                    caches[0].matches_binary(left.value(), right.value()));
                auto metadata = context.vm().trusted_handler_metadata(
                    caches[0].trusted_handler.target(
                        TrustedHandlerArity::Binary));
                ASSERT_TRUE(metadata.has_value());
                EXPECT_EQ(TrustedHandlerEffects::None, metadata->effects);
            }
        }
    }
}
