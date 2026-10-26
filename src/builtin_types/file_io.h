#ifndef CL_FILE_IO_H
#define CL_FILE_IO_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cl
{
    class VirtualMachine;
    class FileIOObject : public InlineSlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::FileIO;
        static constexpr uint32_t kPredefinedInlineSlotCount = 4;
        static constexpr uint32_t kInlineSlotCount = 4;

        explicit FileIOObject(ClassObject *cls)
            : InlineSlotObject(cls, native_layout), name(Value::not_present()),
              mode(Value::not_present()), closed(TValue<Bool>::from_bool(true)),
              closefd(TValue<Bool>::from_bool(true))
        {
        }

        Member<Value> name;
        Member<Value> mode;
        Member<TValue<Bool>> closed;
        Member<TValue<Bool>> closefd;
        int fd = -1;
        uint8_t access_flags = 0;
        int8_t seekable = -1;

        int detach_fd()
        {
            int previous = fd;
            fd = -1;
            closed = TValue<Bool>::from_bool(true);
            return previous;
        }

        // Intentionally no resource finalizer. Only explicit close, with-exit,
        // and local construction rollback release descriptors.
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(FileIOObject, InlineSlotObject, 4);
        CL_DECLARE_STATIC_OBJECT_SIZE(FileIOObject);
    };
    static_assert(CL_OFFSETOF(FileIOObject, name) == sizeof(InlineSlotObject));
    static_assert(CL_OFFSETOF(FileIOObject, mode) ==
                  sizeof(InlineSlotObject) + sizeof(Value));
    static_assert(CL_OFFSETOF(FileIOObject, closed) ==
                  sizeof(InlineSlotObject) + 2 * sizeof(Value));
    static_assert(CL_OFFSETOF(FileIOObject, closefd) ==
                  sizeof(InlineSlotObject) + 3 * sizeof(Value));
    static_assert(CL_OFFSETOF(FileIOObject, fd) ==
                  sizeof(InlineSlotObject) + 4 * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<FileIOObject>);

    BuiltinClassDefinition make_file_io_class(VirtualMachine *vm);
    BuiltinClassDefinition make_unsupported_operation_class(VirtualMachine *vm);
    void install_file_io_methods(VirtualMachine *vm);
}  // namespace cl

#endif  // CL_FILE_IO_H
