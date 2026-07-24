#include "bytecode/code_object.h"
#include "jit/code_cache.h"
#include "jit/jit_code_object.h"
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
        PublishedCode publish_test_code(CodeCache &cache, Value pool_value)
        {
            Result<CodeAllocationProposal, JitCodeError> proposal_result =
                cache.propose(sizeof(uint32_t), 1);
            EXPECT_TRUE(proposal_result);
            CodeAllocationProposal proposal =
                std::move(proposal_result).value();

            Result<CodeAllocation, JitCodeError> allocation_result =
                proposal.commit(sizeof(uint32_t));
            EXPECT_TRUE(allocation_result);
            CodeAllocation allocation = std::move(allocation_result).value();
            *reinterpret_cast<uint32_t *>(allocation.writable_code().data()) =
                0;
            allocation.value_pool_values()[0] = pool_value;

            Result<PublishedCode, JitCodeError> publication =
                cache.publish(std::move(allocation));
            EXPECT_TRUE(publication);
            return std::move(publication).value();
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
            published.code(), published.value_pool_values(),
            published.encoded_code_size());

        EXPECT_EQ(2, string->refcount);
        ASSERT_EQ(1u, jit_code->value_pool_values().size());
        EXPECT_EQ(retained.value(), jit_code->value_pool_values()[0]);

        incref_heap_ptr(jit_code);
        decref_heap_ptr(jit_code);
        context.vm().run_heap_reclamation();

        EXPECT_EQ(1, string->refcount);
        EXPECT_TRUE(published.value_pool_values()[0].is_not_present());
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
            published.code(), published.value_pool_values(),
            published.encoded_code_size());

        EXPECT_FALSE(code_object->has_jit_code());
        EXPECT_EQ(0, jit_code->refcount);

        code_object->publish_jit_code(jit_code);

        EXPECT_TRUE(code_object->has_jit_code());
        EXPECT_EQ(jit_code, code_object->get_jit_code());
        EXPECT_EQ(1, jit_code->refcount);
    }

}  // namespace cl::jit
