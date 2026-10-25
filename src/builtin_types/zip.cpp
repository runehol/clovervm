#include "builtin_types/zip.h"
#include "builtin_types/module_object.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

namespace cl
{
    [[nodiscard]] static Value validate_class(ThreadState *thread, Value value)
    {
        ClassObject *cls = try_native_cast<ClassObject>(value);
        if(cls == nullptr ||
           !is_subclass_of(cls, thread->get_machine()->zip_class()) ||
           cls->instance_native_layout_id() != NativeLayoutId::ZipIterator)
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"zip.__new__ expects a zip subclass");
        }
        return value;
    }

    [[nodiscard]] static Value iterator_iter(ThreadState *thread, Value self)
    {
        (void)thread;
        (void)CL_TRY(TValue<ZipIterator>::from_value_or_raise(
            self, L"TypeError", L"zip.__iter__ expects a zip receiver"));
        return self;
    }

    [[nodiscard]] static Value allocate(ThreadState *thread, Value cls,
                                        Value sources, Value strict)
    {
        CL_PROPAGATE_EXCEPTION(validate_class(thread, cls));
        return thread
            ->make_internal_value<ZipIterator>(
                native_cast<ClassObject>(cls),
                TValue<Tuple>::from_value_assumed(sources),
                TValue<Bool>::from_value_assumed(strict))
            .raw_value();
    }

    [[nodiscard]] static Value sources(ThreadState *thread, Value self)
    {
        (void)thread;
        auto iterator = CL_TRY(TValue<ZipIterator>::from_value_or_raise(
            self, L"TypeError", L"zip.__next__ expects a zip receiver"));
        return iterator.extract()->sources.raw_value();
    }

    [[nodiscard]] static Value strict(ThreadState *thread, Value self)
    {
        (void)thread;
        auto iterator = CL_TRY(TValue<ZipIterator>::from_value_or_raise(
            self, L"TypeError", L"zip.__next__ expects a zip receiver"));
        return iterator.extract()->strict.raw_value();
    }

    BuiltinClassDefinition make_zip_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId layouts[] = {
            NativeLayoutId::ZipIterator};
        ClassObject *cls = ClassObject::make_builtin_class<ZipIterator>(
            vm->get_or_create_interned_string_value(L"zip"), 0, nullptr, 0,
            vm->object_class(), SubclassPolicy::Allowed,
            immutable_shape_flags());
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__iter__", iterator_iter,
                                     L"Return self."),
        };
        unwrap_bootstrap_expected(vm,
                                  install_builtin_intrinsic_methods(
                                      vm, cls, methods, std::size(methods)),
                                  "installing zip methods");
        return builtin_class_definition(cls, layouts,
                                        BuiltinsVisibility::Public);
    }

    void install_zip_helpers(VirtualMachine *vm, ModuleObject *module)
    {
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                vm->get_or_create_interned_string_value(
                    L"_clover_zip_validate_class"),
                unwrap_bootstrap_expected(
                    vm, make_intrinsic_function(vm, validate_class),
                    "creating zip helper")
                    .raw_value()),
            "installing zip helper");
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                vm->get_or_create_interned_string_value(
                    L"_clover_zip_allocate"),
                unwrap_bootstrap_expected(vm,
                                          make_intrinsic_function(vm, allocate),
                                          "creating zip helper")
                    .raw_value()),
            "installing zip helper");
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                vm->get_or_create_interned_string_value(L"_clover_zip_sources"),
                unwrap_bootstrap_expected(vm,
                                          make_intrinsic_function(vm, sources),
                                          "creating zip helper")
                    .raw_value()),
            "installing zip helper");
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                vm->get_or_create_interned_string_value(L"_clover_zip_strict"),
                unwrap_bootstrap_expected(vm,
                                          make_intrinsic_function(vm, strict),
                                          "creating zip helper")
                    .raw_value()),
            "installing zip helper");
    }

    void install_zip_python_methods(VirtualMachine *vm, ModuleObject *module)
    {
        const wchar_t *methods[] = {L"__new__", L"__next__"};
        const wchar_t *helpers[] = {L"_clover_zip_new", L"_clover_zip_next"};
        DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly) |
                                descriptor_flag(DescriptorFlag::StableSlot);
        for(size_t idx = 0; idx < std::size(methods); ++idx)
        {
            Value function = module->get_own_property(
                vm->get_or_create_interned_string_value(helpers[idx]));
            if(!has_native_layout<Function>(function))
                fatal("trusted builtin source did not define zip method");
            bool stored = vm->zip_class()->define_own_property(
                vm->get_or_create_interned_string_value(methods[idx]), function,
                flags);
            assert(stored);
            (void)stored;
        }
    }
}  // namespace cl
