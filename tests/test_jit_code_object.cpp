#include "bytecode/code_object.h"
#include "jit/code_cache.h"
#include "jit/jit_code_object.h"
#include "jit/jit_config.h"
#include "jit/machine_address_internal.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/owned.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace cl::jit
{
    namespace
    {
        MachineAddress no_interpreter_tail_entry_thunk()
        {
            return detail::MachineAddressAccess::from_bits(0);
        }

        PublishedCode publish_test_code(CodeCache &cache, Value pool_value)
        {
            Result<CodeAllocationProposal, JitCodeError> proposal_result =
                cache.propose(sizeof(uint32_t), sizeof(Value), alignof(Value));
            EXPECT_TRUE(proposal_result);
            CodeAllocationProposal proposal =
                std::move(proposal_result).value();

            Result<CodeAllocation, JitCodeError> allocation_result =
                proposal.commit(sizeof(uint32_t));
            EXPECT_TRUE(allocation_result);
            CodeAllocation allocation = std::move(allocation_result).value();
            *reinterpret_cast<uint32_t *>(allocation.writable_code().data()) =
                0;
            *reinterpret_cast<Value *>(allocation.constant_pool().data()) =
                pool_value;

            EXPECT_TRUE(cache.publish(allocation));
            return PublishedCode(allocation.code, allocation.constant_pool(),
                                 allocation.constant_pool_address(), 1,
                                 allocation.encoded_code_size());
        }
    }  // namespace

    TEST(JitCodeObject, OwnsAndReleasesPublishedPoolValues)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation_scope(thread);

        String *string = thread->make_internal_raw<String>(L"pooled");
        Owned<Value> retained(Value::from_oop(string));
        ASSERT_EQ(1, string->refcount());

        PublishedCode published =
            publish_test_code(thread->code_cache(), retained.value());
        JitCodeObject *jit_code = thread->make_internal_raw<JitCodeObject>(
            published.code(), no_interpreter_tail_entry_thunk(),
            published.constant_pool(), published.tagged_value_count(),
            published.encoded_code_size(), 4);

        EXPECT_EQ(2, string->refcount());
        EXPECT_TRUE(jit_code->is_non_moving());
        EXPECT_EQ(4u, jit_code->managed_frame_spill_extent());
        ASSERT_EQ(1u, jit_code->tagged_values().size());
        EXPECT_EQ(retained.value(), jit_code->tagged_values()[0]);

        incref_heap_ptr(jit_code);
        decref_heap_ptr(jit_code);
        context.vm().run_heap_reclamation();

        EXPECT_EQ(1, string->refcount());
        EXPECT_TRUE(published.tagged_values()[0].is_not_present());
    }

    TEST(JitCodeObject, ReleasesPoolOwnershipBeforeNativeDestruction)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        Owned<TValue<String>> child(thread->make_object_value<String>(L"pool"));
        PublishedCode published =
            publish_test_code(thread->code_cache(), child.value().raw_value());
        alignas(JitCodeObject) std::byte storage[sizeof(JitCodeObject)];
        JitCodeObject *jit_code = new(storage) JitCodeObject(
            published.code(), no_interpreter_tail_entry_thunk(),
            published.constant_pool(), published.tagged_value_count(),
            published.encoded_code_size(), 0);
        ASSERT_EQ(2, child.extract()->refcount());

        const ReleaseDescriptor &descriptor =
            native_layout_descriptor_for(JitCodeObject::native_layout).release;
        EXPECT_EQ(JitCodeObject::release_managed_references,
                  descriptor.custom_release);
        EXPECT_EQ(JitCodeObject::destroy, descriptor.destroy);
        descriptor.custom_release(jit_code);
        EXPECT_EQ(1, child.extract()->refcount());
        EXPECT_TRUE(published.tagged_values()[0].is_not_present());
        size_t zct_size = thread->zero_count_table_size();

        descriptor.destroy(jit_code);
        EXPECT_EQ(1, child.extract()->refcount());
        EXPECT_TRUE(published.tagged_values()[0].is_not_present());
        EXPECT_EQ(zct_size, thread->zero_count_table_size());
    }

    TEST(JitCodeObject, IsPublishedIntoCodeObjectSeparately)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation_scope(thread);
        CodeObject *code_object = context.compile_file(L"pass");

        PublishedCode published =
            publish_test_code(thread->code_cache(), Value::None());
        JitCodeObject *jit_code = thread->make_internal_raw<JitCodeObject>(
            published.code(), no_interpreter_tail_entry_thunk(),
            published.constant_pool(), published.tagged_value_count(),
            published.encoded_code_size(), 0);

        EXPECT_FALSE(code_object->has_jit_code());
        EXPECT_EQ(0, jit_code->refcount());

        code_object->publish_jit_code(jit_code);

        EXPECT_TRUE(code_object->has_jit_code());
        EXPECT_EQ(jit_code, code_object->get_jit_code());
        EXPECT_EQ(1, jit_code->refcount());
        size_t jit_references = 0;
        visit_strong_reference_slots(code_object, [&](HeapReferenceSlot slot) {
            if(slot.load_pointer() == jit_code)
            {
                ++jit_references;
            }
        });
        EXPECT_EQ(1u, jit_references);
    }

    TEST(JitCodeObject, TieringBudgetTriggersOnlyOnTransitionToZero)
    {
        test::VmTestContext context;
        CodeObject *code_object = context.compile_file(L"pass");

        for(uint32_t remaining = InitialJitTieringBudget; remaining > 1;
            --remaining)
        {
            EXPECT_FALSE(code_object->consume_jit_tiering_budget());
        }
        EXPECT_TRUE(code_object->consume_jit_tiering_budget());
        EXPECT_FALSE(code_object->consume_jit_tiering_budget());
    }

    TEST(JitCodeObject, EmptyTaggedPrefixDoesNotVisitPointerLookingPoolData)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation(thread);
        Owned<TValue<String>> child(thread->make_object_value<String>(L"raw"));
        PublishedCode published =
            publish_test_code(thread->code_cache(), child.raw_value());
        JitCodeObject *jit_code = thread->make_internal_raw<JitCodeObject>(
            published.code(), no_interpreter_tail_entry_thunk(),
            published.constant_pool(), 0, published.encoded_code_size(), 0);
        size_t cells = 0;
        visit_strong_reference_slots(jit_code,
                                     [&](HeapReferenceSlot) { ++cells; });
        EXPECT_EQ(0u, cells);
        EXPECT_EQ(1, child.extract()->refcount());
        incref_heap_ptr(jit_code);
        decref_heap_ptr(jit_code);
        context.vm().run_heap_reclamation();
        EXPECT_EQ(1, child.extract()->refcount());
        EXPECT_EQ(child.raw_value(), published.tagged_values()[0]);
    }

    TEST(JitCodeObject, RetainsAndReleasesOnlyTheTaggedPoolPrefix)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation_scope(thread);

        String *string = thread->make_internal_raw<String>(L"tagged");
        Owned<Value> retained(Value::from_oop(string));
        ASSERT_EQ(1, string->refcount());

        Result<CodeAllocationProposal, JitCodeError> proposal_result =
            thread->code_cache().propose(sizeof(uint32_t), 2 * sizeof(Value),
                                         alignof(Value));
        ASSERT_TRUE(proposal_result);
        CodeAllocationProposal proposal = std::move(proposal_result).value();
        Result<CodeAllocation, JitCodeError> allocation_result =
            proposal.commit(sizeof(uint32_t));
        ASSERT_TRUE(allocation_result);
        CodeAllocation allocation = std::move(allocation_result).value();
        auto *pool =
            reinterpret_cast<Value *>(allocation.constant_pool().data());
        pool[0] = retained.value();
        pool[1] = retained.value();
        ASSERT_TRUE(thread->code_cache().publish(allocation));

        JitCodeObject *jit_code = thread->make_internal_raw<JitCodeObject>(
            allocation.code, no_interpreter_tail_entry_thunk(),
            allocation.constant_pool(), 1, allocation.encoded_code_size(), 0);

        EXPECT_EQ(2, string->refcount());
        Owned<TValue<String>> replacement(
            thread->make_object_value<String>(L"replacement"));
        size_t zct_size = thread->zero_count_table_size();
        size_t cells = 0;
        visit_strong_reference_slots(jit_code, [&](HeapReferenceSlot slot) {
            ++cells;
            EXPECT_EQ(string, slot.load_pointer());
            slot.rewrite_pointer(replacement.extract());
        });
        EXPECT_EQ(1u, cells);
        EXPECT_EQ(replacement.raw_value(), pool[0]);
        // The untagged suffix deliberately contains pointer-looking bits.
        EXPECT_EQ(retained.value(), pool[1]);
        EXPECT_EQ(2, string->refcount());
        EXPECT_EQ(1, replacement.extract()->refcount());
        EXPECT_EQ(zct_size, thread->zero_count_table_size());
        visit_strong_reference_slots(jit_code, [&](HeapReferenceSlot slot) {
            slot.rewrite_pointer(string);
        });
        incref_heap_ptr(jit_code);
        decref_heap_ptr(jit_code);
        context.vm().run_heap_reclamation();

        EXPECT_EQ(1, string->refcount());
        EXPECT_TRUE(pool[0].is_not_present());
        EXPECT_EQ(retained.value(), pool[1]);
    }

}  // namespace cl::jit
