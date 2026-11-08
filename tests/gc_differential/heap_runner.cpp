#include "heap_runner.h"

#include "gc/evacuation.h"
#include "gc/strong_closure.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <type_traits>

namespace cl::gc::test::differential
{
    namespace
    {
        [[noreturn]] void inconsistent(const std::string &message)
        {
            throw RunnerError("native heap: " + message);
        }
        uint64_t logical_identity(const format::ObjectId &id)
        {
            uint64_t result;
            auto text = std::string_view(id.value).substr(4);
            auto parsed =
                std::from_chars(text.data(), text.data() + text.size(), result);
            if(parsed.ec != std::errc{} ||
               parsed.ptr != text.data() + text.size())
                inconsistent("invalid original identity metadata");
            return result;
        }
        EvacuationDestination placement(format::CopyTo copy_to)
        {
            switch(copy_to)
            {
                case format::CopyTo::Ordinary:
                    return EvacuationDestination::Ordinary;
                case format::CopyTo::Nonmoving:
                    return EvacuationDestination::Stable;
            }
            inconsistent("invalid placement metadata");
        }
    }  // namespace

    namespace detail
    {
        void AllocationRegistry::append(TestObject *object,
                                        format::AllocationId original)
        {
            if(object == nullptr || ids_.contains(object))
                inconsistent("null or duplicate allocation address");
            ids_.emplace(object, format::AllocationId{records_.size()});
            records_.push_back({object, original});
        }
        void AllocationRegistry::add_original(TestObject *object)
        {
            append(object, format::AllocationId{records_.size()});
        }
        void AllocationRegistry::add_copy(TestObject *source, TestObject *copy)
        {
            auto source_id = id(source);
            if(records_[source_id.value].original_allocation != source_id)
                inconsistent(
                    "copy allocation did not originate from an original");
            append(copy, source_id);
        }
        format::AllocationId AllocationRegistry::id(TestObject *object) const
        {
            auto found = ids_.find(object);
            if(found == ids_.end())
                inconsistent("reference names an unregistered address");
            return found->second;
        }
        TestObject &AllocationRegistry::object(format::AllocationId id) const
        {
            if(id.value >= records_.size())
                inconsistent("unknown allocation ID");
            return *records_[id.value].address;
        }
        TestObject *RecordingAdapter::copy_object(TestObject *source)
        {
            TestObject *copy = TestHeapAdapter::copy_object(source);
            allocations_.add_copy(source, copy);
            return copy;
        }

        NativeHeap::NativeHeap(const format::HeapSnapshot &input,
                               Collection collection)
            : input_(input), collection_(collection),
              adapter_(heap_, allocations_, collection)
        {
            format::validate_fresh_heap(input_);
            if(collection != Collection::Full &&
               collection != Collection::Young)
                inconsistent("unknown collection");
            if(collection == Collection::Young)
            {
                if(!input_.external_weak.empty())
                    inconsistent("young collection currently requires empty "
                                 "weak storage");
                for(const auto &a: input_.objects)
                    if(!a.weak.empty())
                        inconsistent("young collection currently requires "
                                     "empty weak storage");
            }
            std::sort(input_.objects.begin(), input_.objects.end(),
                      [](const auto &a, const auto &b) {
                          return a.allocation_id.value < b.allocation_id.value;
                      });
            for(const auto &a: input_.objects)
            {
                TestObject *object =
                    std::holds_alternative<format::NonmovingOriginal>(a.state)
                        ? heap_.make_non_moving_object()
                        : heap_.make_object();
                object->logical_identity = logical_identity(a.object_id);
                std::visit(
                    [&](const auto &state) {
                        using T = std::decay_t<decltype(state)>;
                        if constexpr(std::is_same_v<T, format::YoungOriginal> ||
                                     std::is_same_v<T, format::OldOriginal>)
                        {
                            object->generation =
                                std::is_same_v<T, format::YoungOriginal>
                                    ? TestGeneration::Young
                                    : TestGeneration::Old;
                            object->preferred_destination =
                                placement(state.copy_to);
                        }
                    },
                    a.state);
                allocations_.add_original(object);
            }
            for(const auto &a: input_.objects)
            {
                auto &object = allocations_.object(a.allocation_id);
                for(const auto &cell: a.strong)
                    object.strong_cells.push_back(make_cell(cell));
                for(const auto &unit: a.weak)
                {
                    if(auto *field = std::get_if<format::Field>(&unit.contents))
                        object.weak_cells.push_back(make_cell(field->value));
                    else
                        object.weak_entries.push_back(
                            make_entry(std::get<format::Entry>(unit.contents)));
                }
            }
            for(const auto &cell: input_.roots)
                roots_.push_back(make_cell(cell));
            for(const auto &unit: input_.external_weak)
            {
                if(auto *field = std::get_if<format::Field>(&unit.contents))
                    external_weak_.emplace_back(make_cell(field->value));
                else
                    external_weak_.emplace_back(
                        make_entry(std::get<format::Entry>(unit.contents)));
            }
        }
        TestCell NativeHeap::make_cell(const format::Cell &cell) const
        {
            if(auto *imm = std::get_if<int64_t>(&cell))
                return TestCell::immediate(*imm);
            const auto &reference = std::get<format::Reference>(cell);
            return TestCell::reference(
                reference ? &allocations_.object(*reference) : nullptr);
        }
        TestWeakEntry NativeHeap::make_entry(const format::Entry &entry) const
        {
            auto pointer = [&](const format::Reference &ref) {
                return ref ? &allocations_.object(*ref) : nullptr;
            };
            return {{pointer(entry.guards[0]), pointer(entry.guards[1])},
                    make_cell(entry.payload)};
        }
        format::Cell NativeHeap::capture_cell(const TestCell &cell) const
        {
            if(auto *imm = std::get_if<int64_t>(&cell.contents))
                return *imm;
            auto *pointer = std::get<TestObject *>(cell.contents);
            return pointer ? format::Reference{allocations_.id(pointer)}
                           : format::Reference{};
        }
        format::Entry
        NativeHeap::capture_entry(const TestWeakEntry &entry) const
        {
            auto reference = [&](TestObject *pointer) -> format::Reference {
                return pointer ? format::Reference{allocations_.id(pointer)}
                               : format::Reference{};
            };
            return {{reference(entry.guards[0]), reference(entry.guards[1])},
                    capture_cell(entry.payload)};
        }

        void NativeHeap::collect(Algorithm algorithm)
        {
            if(collection_ == Collection::Young &&
               algorithm != Algorithm::Evacuate)
                inconsistent("young collection supports evacuation only");
            if(phase_ != Phase::Fresh)
                inconsistent("collection context is single-use");
            try
            {
                format::validate_fresh_heap(capture());
            }
            catch(const format::FormatError &e)
            {
                inconsistent(e.what());
            }
            phase_ = Phase::Running;
            try
            {
                switch(algorithm)
                {
                    case Algorithm::Mark:
                        {
                            StrongClosure<RecordingAdapter> collection(
                                adapter_);
                            for(auto &root: roots_)
                                collection.add_root(TestSlot(root));
                            collection.drain();
                            break;
                        }
                    case Algorithm::Evacuate:
                        {
                            Evacuation<RecordingAdapter> collection(adapter_);
                            for(auto &unit: external_weak_)
                            {
                                std::visit(
                                    [&](auto &native) {
                                        using T =
                                            std::decay_t<decltype(native)>;
                                        if constexpr(std::is_same_v<T,
                                                                    TestCell>)
                                            collection.add_weak_root(
                                                RecordingAdapter::WeakUnit::
                                                    bind<
                                                        repair_test_weak_field>(
                                                        native));
                                        else
                                            collection.add_weak_root(
                                                RecordingAdapter::WeakUnit::
                                                    bind<
                                                        repair_test_weak_entry>(
                                                        native));
                                    },
                                    unit);
                            }
                            for(auto &root: roots_)
                                collection.add_root(TestSlot(root));
                            if(collection_ == Collection::Young)
                                for(auto owner: input_.remembered_strong)
                                    for(auto &cell:
                                        allocations_.object(owner).strong_cells)
                                        collection.add_root(TestSlot(cell));
                            collection.finish();
                            break;
                        }
                    default:
                        inconsistent("unknown algorithm");
                }
                phase_ = Phase::Finished;
            }
            catch(...)
            {
                phase_ = Phase::Failed;
                throw;
            }
        }

        format::ObjectState
        NativeHeap::capture_state(const TestObject &object,
                                  const AllocationRecord &record,
                                  format::AllocationId id) const
        {
            const auto &original =
                input_.objects[record.original_allocation.value];
            bool marked = adapter_.is_marked(record.address);
            bool is_original = id == record.original_allocation;
            if(collection_ == Collection::Young && is_original && marked)
                inconsistent("young evacuation marked an original");
            if(collection_ == Collection::Young &&
               !std::holds_alternative<format::YoungOriginal>(original.state))
            {
                if(!is_original || object.header.kind != TestHeaderKind::Normal)
                    inconsistent("young collection changed unselected original "
                                 "mark or forwarding");
            }
            if(object.logical_identity != logical_identity(original.object_id))
                inconsistent("logical identity changed");
            if(object.header.kind != TestHeaderKind::Normal &&
               object.header.kind != TestHeaderKind::Forwarded)
                inconsistent("unknown header kind");
            if(object.header.kind == TestHeaderKind::Normal &&
               object.header.target != nullptr)
                inconsistent("normal header has a forwarding target");
            if(!is_original)
            {
                auto *source = &allocations_.object(record.original_allocation);
                TestStorage expected = source->preferred_destination ==
                                               EvacuationDestination::Ordinary
                                           ? TestStorage::OrdinaryDestination
                                           : TestStorage::StableDestination;
                if(object.storage != expected ||
                   object.generation != TestGeneration::Old ||
                   object.header.kind != TestHeaderKind::Normal || !marked)
                    inconsistent("invalid destination storage, generation, "
                                 "header or mark");
                if(object.storage == TestStorage::OrdinaryDestination)
                    return format::OrdinaryDestination{};
                return format::NonmovingDestination{};
            }
            if(std::holds_alternative<format::NonmovingOriginal>(
                   original.state))
            {
                if(object.storage != TestStorage::NonMoving ||
                   object.generation != TestGeneration::Old ||
                   object.header.kind != TestHeaderKind::Normal)
                    inconsistent("invalid nonmoving original state");
                return format::NonmovingOriginal{marked};
            }
            format::Header header;
            if(object.header.kind == TestHeaderKind::Forwarded)
            {
                if(object.header.target == nullptr || marked)
                    inconsistent("invalid forwarded original");
                header = format::ForwardedHeader{
                    allocations_.id(object.header.target)};
            }
            else
                header = format::NormalHeader{marked};
            return std::visit(
                [&](const auto &state) -> format::ObjectState {
                    using T = std::decay_t<decltype(state)>;
                    if constexpr(std::is_same_v<T, format::YoungOriginal> ||
                                 std::is_same_v<T, format::OldOriginal>)
                    {
                        auto generation =
                            std::is_same_v<T, format::YoungOriginal>
                                ? TestGeneration::Young
                                : TestGeneration::Old;
                        if(object.storage != TestStorage::Source ||
                           object.generation != generation ||
                           object.preferred_destination !=
                               placement(state.copy_to))
                            inconsistent("original storage, generation or "
                                         "placement changed");
                        return T{state.copy_to, header};
                    }
                    else
                        inconsistent("invalid original metadata");
                },
                original.state);
        }

        format::HeapSnapshot NativeHeap::capture() const
        {
            if(phase_ == Phase::Running || phase_ == Phase::Failed)
                inconsistent("collection has not completed successfully");
            auto records = allocations_.records();
            if(heap_.objects().size() != input_.objects.size() ||
               records.size() < input_.objects.size() ||
               records.size() - input_.objects.size() !=
                   heap_.destination_object_count(
                       EvacuationDestination::Ordinary) +
                       heap_.destination_object_count(
                           EvacuationDestination::Stable))
                inconsistent(
                    "allocation registry disagrees with owned storage");
            if(roots_.size() != input_.roots.size() ||
               external_weak_.size() != input_.external_weak.size())
                inconsistent("root or external unit layout changed");
            format::HeapSnapshot result;
            result.remembered_strong = input_.remembered_strong;
            for(const auto &root: roots_)
                result.roots.push_back(capture_cell(root));
            for(size_t i = 0; i < records.size(); ++i)
            {
                const auto &record = records[i];
                if(allocations_.id(record.address).value != i ||
                   record.original_allocation.value >= input_.objects.size())
                    inconsistent("invalid allocation registry entry");
                if(i < input_.objects.size() &&
                   heap_.objects()[i].get() != record.address)
                    inconsistent("original address association changed");
                const auto &object = *record.address;
                const auto &layout =
                    input_.objects[record.original_allocation.value];
                auto fields = std::count_if(
                    layout.weak.begin(), layout.weak.end(), [](const auto &u) {
                        return std::holds_alternative<format::Field>(
                            u.contents);
                    });
                if(object.strong_cells.size() != layout.strong.size() ||
                   object.weak_cells.size() != static_cast<size_t>(fields) ||
                   object.weak_entries.size() !=
                       layout.weak.size() - static_cast<size_t>(fields))
                    inconsistent("object slot or weak unit layout changed");
                format::Allocation allocation{
                    format::ObjectId{"obj-" +
                                     std::to_string(object.logical_identity)},
                    format::AllocationId{i},
                    capture_state(object, record, format::AllocationId{i}),
                    {},
                    {}};
                for(const auto &cell: object.strong_cells)
                    allocation.strong.push_back(capture_cell(cell));
                size_t field = 0, entry = 0;
                for(const auto &unit: layout.weak)
                {
                    if(std::holds_alternative<format::Field>(unit.contents))
                        allocation.weak.push_back(
                            {unit.unit_id, format::Field{capture_cell(
                                               object.weak_cells[field++])}});
                    else
                        allocation.weak.push_back(
                            {unit.unit_id,
                             capture_entry(object.weak_entries[entry++])});
                }
                result.objects.push_back(std::move(allocation));
            }
            for(size_t i = 0; i < external_weak_.size(); ++i)
            {
                const auto &layout = input_.external_weak[i];
                if(layout.contents.index() != external_weak_[i].index())
                    inconsistent("external weak unit kind changed");
                if(auto *cell = std::get_if<TestCell>(&external_weak_[i]))
                    result.external_weak.push_back(
                        {layout.unit_id, format::Field{capture_cell(*cell)}});
                else
                    result.external_weak.push_back(
                        {layout.unit_id, capture_entry(std::get<TestWeakEntry>(
                                             external_weak_[i]))});
            }
            try
            {
                format::validate_heap(result);
            }
            catch(const format::FormatError &e)
            {
                inconsistent(e.what());
            }
            return result;
        }
    }  // namespace detail

    format::HeapSnapshot run_collection(const format::HeapSnapshot &input,
                                        Algorithm algorithm,
                                        Collection collection)
    {
        detail::NativeHeap heap(input, collection);
        heap.collect(algorithm);
        return heap.capture();
    }
}  // namespace cl::gc::test::differential
