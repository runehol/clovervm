#include "import_system/module_global.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/class_object.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

using namespace cl;

namespace
{
    std::vector<ClassObject *> builtin_roots(const VirtualMachine &vm)
    {
        std::vector<ClassObject *> roots;
        vm.for_each_builtin_class(
            [&](ClassObject *cls) { roots.push_back(cls); });
        return roots;
    }

    // Test-only tracing from accessible roots, not from allocated heap objects.
    // The visited set handles class -> MRO -> class and other graph cycles.
    std::unordered_set<HeapObject *>
    trace_builtin_roots(const VirtualMachine &vm, StrongSlotVisitor visit_slot)
    {
        std::vector<HeapObject *> worklist;
        vm.for_each_builtin_class(
            [&](ClassObject *cls) { worklist.push_back(cls); });
        std::unordered_set<HeapObject *> visited;
        while(!worklist.empty())
        {
            HeapObject *object = worklist.back();
            worklist.pop_back();
            if(!visited.insert(object).second)
                continue;
            visit_strong_reference_slots(object, [&](HeapReferenceSlot slot) {
                visit_slot(slot);
                if(HeapObject *target = slot.load_pointer())
                    worklist.push_back(target);
            });
        }
        return visited;
    }
}  // namespace

TEST(BuiltinClassRoots, CanonicalRootsSurvivePublicRebindingAndDeletion)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    ThreadState::ActivationScope activation(context.thread());
    auto name = vm.get_or_create_interned_string_value(L"TypeError");
    ClassObject *canonical = vm.builtin_class_by_name(name);
    auto original = builtin_roots(vm);
    EXPECT_EQ(1, std::count(original.begin(), original.end(), canonical));
    for(ClassObject *cls: original)
    {
        EXPECT_TRUE(heap_ptr_is_interned(cls));
        EXPECT_EQ(cls, vm.builtin_class_by_name(cls->get_name()));
    }
    ModuleObject *builtins = vm.global_builtins_module().extract();
    ASSERT_TRUE(builtins->set_own_property(name, Value::from_smi(7)).value());
    EXPECT_EQ(original, builtin_roots(vm));
    EXPECT_EQ(canonical, vm.builtin_class_by_name(name));
    ASSERT_TRUE(delete_module_global(builtins, name));
    EXPECT_EQ(original, builtin_roots(vm));
    EXPECT_EQ(canonical, vm.builtin_class_by_name(name));
}

TEST(BuiltinClassRoots, FollowsOutgoingReferencesAndHandlesCycles)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    auto name = vm.get_or_create_interned_string_value(L"TypeError");
    ClassObject *cls = vm.builtin_class_by_name(name);
    Tuple *mro = native_cast<Tuple>(cls->get_mro_value());
    auto visited = trace_builtin_roots(vm, [](HeapReferenceSlot) {});
    EXPECT_TRUE(visited.contains(cls));
    EXPECT_TRUE(visited.contains(cls->get_shape()));
    EXPECT_TRUE(visited.contains(mro));
    EXPECT_TRUE(visited.contains(vm.object_class()));
    EXPECT_TRUE(visited.contains(name.extract()));
    EXPECT_EQ(Value::from_oop(cls), mro->item_unchecked(0));
}

TEST(BuiltinClassRoots, RewritesChildCellsWithoutMovingClassOrChangingMap)
{
    test::VmTestContext context;
    VirtualMachine &vm = context.vm();
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    auto name = vm.get_or_create_interned_string_value(L"TypeError");
    ClassObject *cls = vm.builtin_class_by_name(name);
    Owned<TValue<Tuple>> original(
        TValue<Tuple>::from_value_assumed(cls->get_mro_value()));
    ASSERT_TRUE(original.raw_value().is_refcounted_ptr());
    Owned<TValue<Tuple>> replacement(
        thread.make_object_value<Tuple>(original.extract()->size()));
    for(size_t index = 0; index < original.extract()->size(); ++index)
        replacement.extract()->initialize_item_unchecked(
            index, original.extract()->item_unchecked(index));
    auto roots = builtin_roots(vm);
    int32_t original_refs = original.extract()->refcount;
    int32_t replacement_refs = replacement.extract()->refcount;
    size_t zct_size = thread.zero_count_table_size();
    size_t rewrites = 0;
    auto visited = trace_builtin_roots(vm, [&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == original.extract())
        {
            slot.rewrite_pointer(replacement.extract());
            ++rewrites;
        }
    });
    EXPECT_GT(rewrites, 0u);
    EXPECT_TRUE(visited.contains(replacement.extract()));
    EXPECT_EQ(replacement.raw_value(), cls->get_mro_value());
    EXPECT_EQ(cls, vm.builtin_class_by_name(name));
    EXPECT_EQ(name, cls->get_name());
    EXPECT_EQ(roots, builtin_roots(vm));
    EXPECT_EQ(original_refs, original.extract()->refcount);
    EXPECT_EQ(replacement_refs, replacement.extract()->refcount);
    EXPECT_EQ(zct_size, thread.zero_count_table_size());

    // Simulated relocation between live tuples: restore ownership before RC
    // cleanup or any managed execution can observe the substituted address.
    trace_builtin_roots(vm, [&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == replacement.extract())
            slot.rewrite_pointer(original.extract());
    });
    EXPECT_EQ(original.raw_value(), cls->get_mro_value());
    EXPECT_EQ(cls, vm.builtin_class_by_name(name));
}
