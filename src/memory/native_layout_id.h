#ifndef CL_NATIVE_LAYOUT_ID_H
#define CL_NATIVE_LAYOUT_ID_H

#include <cstdint>

namespace cl
{
    enum class NativeLayoutId : uint8_t
    {
        Invalid = 0,

        BigInt,
        String,
        Bytes,
        ByteArray,
        List,
        Tuple,
        Dict,
        SlotDict,
        Slice,
        IOBase,
        RawIOBase,
        FileIO,
        Float,
        Function,
        EnumerateIterator,
        ZipIterator,
        RangeIterator,
        TupleIterator,
        StringIterator,
        ListIterator,
        DictKeysView,
        DictValuesView,
        DictItemsView,
        DictKeyIterator,
        DictValueIterator,
        DictItemIterator,
        ModuleObject,
        ModuleLoaderObject,
        ModuleSpecObject,
        CodeObject,
        JitCodeObject,
        ClassObject,
        Exception,
        StopIteration,
        OSError,
        Instance,
        Scope,
        Shape,
        ValidityCell,
        OverflowSlots,
        RawArrayBacking,
        ValueArrayBacking,
        HeapPtrArrayBacking,
        HandleChunk,
        TestOnly,

        Count,
    };

}  // namespace cl

#endif  // CL_NATIVE_LAYOUT_ID_H
