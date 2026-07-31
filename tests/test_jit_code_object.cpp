#include "bytecode/code_object.h"
#include "jit/code_cache.h"
#include "jit/jit_code_object.h"
#include "jit/jit_config.h"
#include "jit/machine_address_internal.h"
#include "object_model/owned.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstdint>
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
        ASSERT_EQ(1, string->refcount);

        PublishedCode published =
            publish_test_code(thread->code_cache(), retained.value());
        JitCodeObject *jit_code = thread->make_internal_raw<JitCodeObject>(
            published.code(), no_interpreter_tail_entry_thunk(),
            published.constant_pool(), published.tagged_value_count(),
            published.encoded_code_size());

        EXPECT_EQ(2, string->refcount);
        ASSERT_EQ(1u, jit_code->tagged_values().size());
        EXPECT_EQ(retained.value(), jit_code->tagged_values()[0]);

        incref_heap_ptr(jit_code);
        decref_heap_ptr(jit_code);
        context.vm().run_heap_reclamation();

        EXPECT_EQ(1, string->refcount);
        EXPECT_TRUE(published.tagged_values()[0].is_not_present());
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
            published.encoded_code_size());

        EXPECT_FALSE(code_object->has_jit_code());
        EXPECT_EQ(0, jit_code->refcount);

        code_object->publish_jit_code(jit_code);

        EXPECT_TRUE(code_object->has_jit_code());
        EXPECT_EQ(jit_code, code_object->get_jit_code());
        EXPECT_EQ(1, jit_code->refcount);
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

    TEST(JitCodeObject, RetainsAndReleasesOnlyTheTaggedPoolPrefix)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope activation_scope(thread);

        String *string = thread->make_internal_raw<String>(L"tagged");
        Owned<Value> retained(Value::from_oop(string));
        ASSERT_EQ(1, string->refcount);

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
            allocation.constant_pool(), 1, allocation.encoded_code_size());

        EXPECT_EQ(2, string->refcount);
        incref_heap_ptr(jit_code);
        decref_heap_ptr(jit_code);
        context.vm().run_heap_reclamation();

        EXPECT_EQ(1, string->refcount);
        EXPECT_TRUE(pool[0].is_not_present());
        EXPECT_EQ(retained.value(), pool[1]);
    }

}  // namespace cl::jit
