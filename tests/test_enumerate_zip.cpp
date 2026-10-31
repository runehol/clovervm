#include "builtin_types/enumerate.h"
#include "builtin_types/tuple_iterator.h"
#include "builtin_types/zip.h"
#include "memory/global_heap.h"
#include "memory/native_layout_descriptor.h"
#include "test_helpers.h"
#include <fstream>
#include <iterator>

using namespace cl;

TEST(EnumerateZip, ManagedCallbacksSurviveEverySafepointReclamation)
{
    // Run the semantic cases with forced reclamation, including reentry,
    // partial zip rows, bigint counter replacement, and exception recovery.
    for(const char *path:
        {"tests/python/builtin_enumerate.py",
         "tests/python/builtin_enumerate_keywords.py",
         "tests/python/builtin_zip.py", "tests/python/builtin_zip_strict.py",
         "tests/python/builtin_zip_callbacks.py",
         "tests/python/builtin_zip_exception_context.py"})
    {
        SCOPED_TRACE(path);
        std::ifstream input(path);
        ASSERT_TRUE(input);
        std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
        std::wstring wide(source.begin(), source.end());
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        auto code = thread->compile(wide.c_str(), StartRule::File);
        ASSERT_FALSE(code.has_exception());
        context.vm().set_fire_every_safepoint_for_testing(true);
        context.vm().request_safepoint();
        Value result = thread->run_clovervm_code_object(code.value());
        ASSERT_FALSE(result.is_exception_marker());
    }
}

TEST(EnumerateZip, CustomIndexRemainsAnExplicitUnsupportedProtocol)
{
    test::FileRunner runner(LR"PY(
class Index:
    def __index__(self):
        raise AssertionError("custom __index__ must not be called in this slice")
try:
    enumerate([], Index())
except TypeError:
    pass
else:
    raise AssertionError("expected unsupported-index TypeError")
)PY");
    EXPECT_FALSE(runner.return_value.is_exception_marker());
}

TEST(EnumerateZip, NativeStateEdgesRetainSourcesAndCounter)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    context.vm().run_heap_reclamation();
    auto &heap = context.vm().get_refcounted_global_heap();
    uint64_t baseline = heap.count_valid_objects_slow();
    {
        Owned<TValue<Tuple>> source(thread->make_object_value<Tuple>(0));
        Owned<TValue<TupleIterator>> iterator(
            thread->make_object_value<TupleIterator>(source.value()));
        Owned<TValue<Tuple>> sources(thread->make_object_value<Tuple>(1));
        sources.extract()->initialize_item_unchecked(0, iterator.raw_value());
        Owned<TValue<EnumerateIterator>> enumerate(
            thread->make_object_value<EnumerateIterator>(iterator.raw_value(),
                                                         Value::from_smi(42)));
        Owned<TValue<ZipIterator>> zip(thread->make_object_value<ZipIterator>(
            sources.value(), TValue<Bool>::True()));
        EXPECT_EQ(sizeof(EnumerateIterator),
                  object_size_in_bytes(enumerate.extract()));
        EXPECT_EQ(sizeof(ZipIterator), object_size_in_bytes(zip.extract()));
        EXPECT_EQ(3, iterator.extract()->refcount());
        EXPECT_EQ(2, source.extract()->refcount());
        EXPECT_EQ(2, sources.extract()->refcount());
        context.vm().run_heap_reclamation();
        EXPECT_EQ(iterator.raw_value(),
                  enumerate.extract()->source.raw_value());
        EXPECT_EQ(sources.raw_value(), zip.extract()->sources.raw_value());
        EXPECT_EQ(Value::from_smi(42),
                  enumerate.extract()->next_index.raw_value());
        EXPECT_TRUE(zip.extract()->strict.value().extract());
    }
    // Reclaiming the native owners must release their traced source edges.
    context.vm().run_heap_reclamation();
    EXPECT_EQ(baseline, heap.count_valid_objects_slow());
}
