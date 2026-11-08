#ifndef CL_GC_DIFFERENTIAL_HEAP_FORMAT_H
#define CL_GC_DIFFERENTIAL_HEAP_FORMAT_H

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cl::gc::test::format
{
    struct ObjectId
    {
        std::string value;
        bool operator==(const ObjectId &) const = default;
    };
    struct AllocationId
    {
        uint64_t value;
        bool operator==(const AllocationId &) const = default;
    };
    struct UnitId
    {
        uint64_t value;
        bool operator==(const UnitId &) const = default;
    };

    using Reference = std::optional<AllocationId>;
    using Cell = std::variant<int64_t, Reference>;

    struct Field
    {
        Cell value;
        bool operator==(const Field &) const = default;
    };
    struct Entry
    {
        std::array<Reference, 2> guards;
        Cell payload;
        bool operator==(const Entry &) const = default;
    };
    struct WeakUnit
    {
        UnitId unit_id;
        std::variant<Field, Entry> contents;
        bool operator==(const WeakUnit &) const = default;
    };

    enum class CopyTo
    {
        Ordinary,
        Nonmoving
    };
    struct NormalHeader
    {
        bool marked;
        bool operator==(const NormalHeader &) const = default;
    };
    struct ForwardedHeader
    {
        AllocationId forwarded_to;
        bool operator==(const ForwardedHeader &) const = default;
    };
    using Header = std::variant<NormalHeader, ForwardedHeader>;
    struct YoungOriginal
    {
        CopyTo copy_to;
        Header header;
        bool operator==(const YoungOriginal &) const = default;
    };
    struct OldOriginal
    {
        CopyTo copy_to;
        Header header;
        bool operator==(const OldOriginal &) const = default;
    };
    struct NonmovingOriginal
    {
        bool marked;
        bool operator==(const NonmovingOriginal &) const = default;
    };
    struct OrdinaryDestination
    {
        bool operator==(const OrdinaryDestination &) const = default;
    };
    struct NonmovingDestination
    {
        bool operator==(const NonmovingDestination &) const = default;
    };
    using ObjectState =
        std::variant<YoungOriginal, OldOriginal, NonmovingOriginal,
                     OrdinaryDestination, NonmovingDestination>;
    struct Allocation
    {
        ObjectId object_id;
        AllocationId allocation_id;
        ObjectState state;
        std::vector<Cell> strong;
        std::vector<WeakUnit> weak;
        bool operator==(const Allocation &) const = default;
    };
    struct HeapSnapshot
    {
        std::vector<Cell> roots;
        std::vector<Allocation> objects;
        std::vector<WeakUnit> external_weak;
        std::vector<AllocationId> remembered;
        bool operator==(const HeapSnapshot &) const = default;
    };

    class FormatError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // All operations throw FormatError on invalid input. No VM exception state
    // is involved. parse_heap accepts fresh and completed snapshots and orders
    // allocation records by allocation_id; it preserves every slot/unit order.
    HeapSnapshot parse_heap(std::string_view json);
    void validate_heap(const HeapSnapshot &heap);
    void validate_fresh_heap(const HeapSnapshot &heap);
    // Validates first; emits allocation records in ascending allocation order.
    std::string write_heap(const HeapSnapshot &heap);
}  // namespace cl::gc::test::format

#endif
