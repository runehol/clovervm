#include "heap_format.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace cl::gc::test::format
{
    namespace
    {
        using Json = nlohmann::json;
        enum class UnitStorage
        {
            Original,
            Destination,
            External
        };

        [[noreturn]] void fail(const std::string &path,
                               std::string_view message)
        {
            throw FormatError(path + ": " + std::string(message));
        }
        std::string index_path(const std::string &path, size_t index)
        {
            return path + "[" + std::to_string(index) + "]";
        }
        void fields(const Json &value, const std::string &path,
                    std::initializer_list<std::string_view> names)
        {
            if(!value.is_object())
                fail(path, "expected object");
            for(auto name: names)
                if(!value.contains(std::string(name)))
                    fail(path + "." + std::string(name), "missing field");
            if(value.size() != names.size())
                fail(path, "unknown field");
        }
        const Json &array(const Json &value, const std::string &path)
        {
            if(!value.is_array())
                fail(path, "expected array");
            return value;
        }
        std::string string(const Json &value, const std::string &path)
        {
            if(!value.is_string())
                fail(path, "expected string");
            return value.get<std::string>();
        }
        bool boolean(const Json &value, const std::string &path)
        {
            if(!value.is_boolean())
                fail(path, "expected boolean");
            return value.get<bool>();
        }
        uint64_t natural(const Json &value, const std::string &path)
        {
            if(value.is_number_unsigned())
                return value.get<uint64_t>();
            if(value.is_number_integer() && value.get<int64_t>() >= 0)
                return static_cast<uint64_t>(value.get<int64_t>());
            fail(path, "expected nonnegative integer in uint64 range");
        }
        Reference reference(const Json &value, const std::string &path)
        {
            if(value.is_null())
                return std::nullopt;
            return AllocationId{natural(value, path)};
        }
        Cell cell(const Json &value, const std::string &path)
        {
            if(value.is_object() && value.contains("imm"))
            {
                fields(value, path, {"imm"});
                const auto &imm = value.at("imm");
                if(!imm.is_number_integer() ||
                   (imm.is_number_unsigned() &&
                    imm.get<uint64_t>() >
                        static_cast<uint64_t>(
                            std::numeric_limits<int64_t>::max())))
                    fail(path + ".imm", "expected signed 64-bit integer");
                return imm.get<int64_t>();
            }
            fields(value, path, {"ref"});
            return reference(value.at("ref"), path + ".ref");
        }
        std::vector<Cell> cells(const Json &value, const std::string &path)
        {
            std::vector<Cell> result;
            for(const auto &item: array(value, path))
                result.push_back(cell(item, index_path(path, result.size())));
            return result;
        }
        std::vector<WeakUnit> units(const Json &value, const std::string &path)
        {
            std::vector<WeakUnit> result;
            for(const auto &item: array(value, path))
            {
                auto p = index_path(path, result.size());
                if(!item.is_object() || !item.contains("kind"))
                    fail(p, "missing kind");
                auto kind = string(item.at("kind"), p + ".kind");
                if(kind == "field")
                {
                    fields(item, p, {"unit_id", "kind", "value"});
                    result.push_back(
                        {UnitId{natural(item.at("unit_id"), p + ".unit_id")},
                         Field{cell(item.at("value"), p + ".value")}});
                }
                else if(kind == "entry")
                {
                    fields(item, p, {"unit_id", "kind", "guards", "payload"});
                    const auto &guards =
                        array(item.at("guards"), p + ".guards");
                    if(guards.size() != 2)
                        fail(p + ".guards", "expected exactly two guards");
                    result.push_back(
                        {UnitId{natural(item.at("unit_id"), p + ".unit_id")},
                         Entry{{reference(guards[0], p + ".guards[0]"),
                                reference(guards[1], p + ".guards[1]")},
                               cell(item.at("payload"), p + ".payload")}});
                }
                else
                    fail(p + ".kind", "unknown weak unit kind");
            }
            return result;
        }
        Header header(const Json &value, const std::string &path)
        {
            if(!value.is_object() || !value.contains("kind"))
                fail(path, "missing kind");
            auto kind = string(value.at("kind"), path + ".kind");
            if(kind == "normal")
            {
                fields(value, path, {"kind", "marked"});
                return NormalHeader{
                    boolean(value.at("marked"), path + ".marked")};
            }
            if(kind == "forwarded")
            {
                fields(value, path, {"kind", "forwarded_to"});
                return ForwardedHeader{AllocationId{
                    natural(value.at("forwarded_to"), path + ".forwarded_to")}};
            }
            fail(path + ".kind", "unknown header kind");
        }
        ObjectState state(const Json &value, const std::string &path)
        {
            if(!value.is_object() || !value.contains("kind"))
                fail(path, "missing kind");
            auto kind = string(value.at("kind"), path + ".kind");
            if(kind == "young_original" || kind == "old_original")
            {
                fields(value, path, {"kind", "copy_to", "header"});
                auto placement = string(value.at("copy_to"), path + ".copy_to");
                if(placement != "ordinary" && placement != "nonmoving")
                    fail(path + ".copy_to", "unknown placement");
                auto copy = placement == "ordinary" ? CopyTo::Ordinary
                                                    : CopyTo::Nonmoving;
                auto h = header(value.at("header"), path + ".header");
                if(kind == "young_original")
                    return YoungOriginal{copy, h};
                return OldOriginal{copy, h};
            }
            if(kind == "nonmoving_original")
            {
                fields(value, path, {"kind", "marked"});
                return NonmovingOriginal{
                    boolean(value.at("marked"), path + ".marked")};
            }
            fields(value, path, {"kind"});
            if(kind == "ordinary_destination")
                return OrdinaryDestination{};
            if(kind == "nonmoving_destination")
                return NonmovingDestination{};
            fail(path + ".kind", "unknown object state");
        }
        uint64_t object_number(const ObjectId &id, const std::string &path)
        {
            std::string_view text = id.value;
            if(!text.starts_with("obj-"))
                fail(path, "expected canonical obj-N identity");
            text.remove_prefix(4);
            if(text.empty() || (text.size() > 1 && text.front() == '0') ||
               !std::all_of(text.begin(), text.end(),
                            [](char c) { return c >= '0' && c <= '9'; }))
                fail(path, "expected canonical obj-N identity");
            uint64_t number;
            auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), number);
            if(error != std::errc{} || end != text.data() + text.size())
                fail(path, "object identity out of range");
            return number;
        }
        bool destination(const ObjectState &s)
        {
            return std::holds_alternative<OrdinaryDestination>(s) ||
                   std::holds_alternative<NonmovingDestination>(s);
        }
        const Header *movable_header(const ObjectState &s)
        {
            if(auto *v = std::get_if<YoungOriginal>(&s))
                return &v->header;
            if(auto *v = std::get_if<OldOriginal>(&s))
                return &v->header;
            return nullptr;
        }
        Json reference_json(const Reference &r)
        {
            return r ? Json(r->value) : Json(nullptr);
        }
        Json cell_json(const Cell &c)
        {
            if(auto *imm = std::get_if<int64_t>(&c))
                return Json{{"imm", *imm}};
            return Json{{"ref", reference_json(std::get<Reference>(c))}};
        }
        Json cells_json(const std::vector<Cell> &values)
        {
            auto result = Json::array();
            for(const auto &v: values)
                result.push_back(cell_json(v));
            return result;
        }
        Json units_json(const std::vector<WeakUnit> &values)
        {
            auto result = Json::array();
            for(const auto &u: values)
            {
                Json v{{"unit_id", u.unit_id.value}};
                if(auto *f = std::get_if<Field>(&u.contents))
                {
                    v["kind"] = "field";
                    v["value"] = cell_json(f->value);
                }
                else
                {
                    const auto &e = std::get<Entry>(u.contents);
                    v["kind"] = "entry";
                    v["guards"] = Json::array({reference_json(e.guards[0]),
                                               reference_json(e.guards[1])});
                    v["payload"] = cell_json(e.payload);
                }
                result.push_back(std::move(v));
            }
            return result;
        }
        Json state_json(const ObjectState &s)
        {
            return std::visit(
                [](const auto &v) -> Json {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr(std::is_same_v<T, YoungOriginal> ||
                                 std::is_same_v<T, OldOriginal>)
                    {
                        Json h;
                        if(auto *normal = std::get_if<NormalHeader>(&v.header))
                            h = {{"kind", "normal"},
                                 {"marked", normal->marked}};
                        else
                            h = {{"kind", "forwarded"},
                                 {"forwarded_to",
                                  std::get<ForwardedHeader>(v.header)
                                      .forwarded_to.value}};
                        return {{"kind", std::is_same_v<T, YoungOriginal>
                                             ? "young_original"
                                             : "old_original"},
                                {"copy_to", v.copy_to == CopyTo::Ordinary
                                                ? "ordinary"
                                                : "nonmoving"},
                                {"header", h}};
                    }
                    else if constexpr(std::is_same_v<T, NonmovingOriginal>)
                        return {{"kind", "nonmoving_original"},
                                {"marked", v.marked}};
                    else if constexpr(std::is_same_v<T, OrdinaryDestination>)
                        return {{"kind", "ordinary_destination"}};
                    else
                        return {{"kind", "nonmoving_destination"}};
                },
                s);
        }
    }  // namespace

    HeapSnapshot parse_heap(std::string_view text)
    {
        if(text.starts_with("\xef\xbb\xbf"))
            fail("$", "UTF-8 BOM is not permitted");
        // The JSON lexer treats a raw NUL as end-of-input. It is never valid
        // JSON text, including inside strings; escaped \u0000 remains allowed.
        if(text.find('\0') != std::string_view::npos)
            fail("$", "raw NUL is not permitted");
        // Callbacks see decoded keys before DOM insertion overwrites
        // duplicates. Every fraction/exponent token (and overflowing integer)
        // becomes a float; reject it even if its mathematical value happens to
        // be integral.
        std::vector<std::set<std::string>> keys;
        Json value;
        try
        {
            value = Json::parse(
                text.begin(), text.end(),
                [&](int, Json::parse_event_t event, Json &parsed) {
                    if(event == Json::parse_event_t::object_start)
                        keys.emplace_back();
                    else if(event == Json::parse_event_t::object_end)
                        keys.pop_back();
                    else if(event == Json::parse_event_t::key &&
                            !keys.back()
                                 .insert(parsed.get<std::string>())
                                 .second)
                        fail("$", "duplicate decoded object key");
                    else if(event == Json::parse_event_t::value &&
                            parsed.is_number_float())
                        fail("$", "only integer literals in 64-bit range are "
                                  "permitted");
                    return true;
                });
        }
        catch(const Json::exception &error)
        {
            fail("$", error.what());
        }
        fields(value, "$",
               {"format", "version", "roots", "objects", "external_weak",
                "remembered"});
        if(string(value.at("format"), "$.format") != "clover-gc-heap")
            fail("$.format", "unsupported format");
        if(natural(value.at("version"), "$.version") != 2)
            fail("$.version", "unsupported version");
        HeapSnapshot heap;
        for(const auto &v: array(value.at("remembered"), "$.remembered"))
            heap.remembered.push_back({natural(v, "$.remembered")});
        heap.roots = cells(value.at("roots"), "$.roots");
        for(const auto &v: array(value.at("objects"), "$.objects"))
        {
            auto p = index_path("$.objects", heap.objects.size());
            fields(v, p,
                   {"object_id", "allocation_id", "state", "strong", "weak"});
            heap.objects.push_back(
                {ObjectId{string(v.at("object_id"), p + ".object_id")},
                 AllocationId{
                     natural(v.at("allocation_id"), p + ".allocation_id")},
                 state(v.at("state"), p + ".state"),
                 cells(v.at("strong"), p + ".strong"),
                 units(v.at("weak"), p + ".weak")});
        }
        heap.external_weak =
            units(value.at("external_weak"), "$.external_weak");
        validate_heap(heap);
        std::sort(heap.objects.begin(), heap.objects.end(),
                  [](const Allocation &a, const Allocation &b) {
                      return a.allocation_id.value < b.allocation_id.value;
                  });
        return heap;
    }

    void validate_heap(const HeapSnapshot &heap)
    {
        const size_t count = heap.objects.size();
        size_t originals = 0;
        for(const auto &a: heap.objects)
            if(!destination(a.state))
                ++originals;
        std::vector<const Allocation *> allocations(count, nullptr);
        std::vector<const Allocation *> origins(originals, nullptr),
            copies(originals, nullptr);
        for(size_t i = 0; i < count; ++i)
        {
            const auto &a = heap.objects[i];
            auto p = index_path("$.objects", i);
            if(a.allocation_id.value >= count ||
               allocations[a.allocation_id.value])
                fail(p + ".allocation_id",
                     "allocation IDs must be unique and dense");
            allocations[a.allocation_id.value] = &a;
            uint64_t logical = object_number(a.object_id, p + ".object_id");
            if(logical >= originals)
                fail(p + ".object_id", "object identities must be dense");
            bool copy = destination(a.state);
            if(copy != (a.allocation_id.value >= originals))
                fail(p + ".state", "original IDs must precede destination IDs");
            auto &slot = copy ? copies[logical] : origins[logical];
            if(slot)
                fail(p + ".object_id",
                     "duplicate original or destination identity");
            slot = &a;
            std::visit(
                [&](const auto &s) {
                    using T = std::decay_t<decltype(s)>;
                    if constexpr(std::is_same_v<T, YoungOriginal> ||
                                 std::is_same_v<T, OldOriginal>)
                        if(s.copy_to != CopyTo::Ordinary &&
                           s.copy_to != CopyTo::Nonmoving)
                            fail(p + ".state.copy_to",
                                 "invalid placement enum");
                },
                a.state);
        }
        auto check_reference = [&](const Reference &r, const std::string &p) {
            if(r && r->value >= count)
                fail(p, "reference names no allocation");
        };
        auto check_cell = [&](const Cell &c, const std::string &p) {
            if(auto *r = std::get_if<Reference>(&c))
                check_reference(*r, p + ".ref");
        };
        std::set<uint64_t> remembered;
        for(auto id: heap.remembered)
        {
            if(id.value >= originals ||
               std::holds_alternative<YoungOriginal>(
                   allocations[id.value]->state) ||
               !remembered.insert(id.value).second)
                fail("$.remembered",
                     "remembered owners must be unique old originals");
        }
        std::set<uint64_t> unit_ids;
        auto check_units = [&](const std::vector<WeakUnit> &values,
                               const std::string &p, UnitStorage storage) {
            bool seen_entry = false;
            for(size_t i = 0; i < values.size(); ++i)
            {
                const auto &u = values[i];
                auto q = index_path(p, i);
                if(storage != UnitStorage::Destination &&
                   !unit_ids.insert(u.unit_id.value).second)
                    fail(q + ".unit_id", "duplicate logical weak unit");
                if(auto *f = std::get_if<Field>(&u.contents))
                {
                    if(storage != UnitStorage::External && seen_entry)
                        fail(q, "owner fields must precede entries");
                    check_cell(f->value, q + ".value");
                }
                else
                {
                    seen_entry = true;
                    const auto &e = std::get<Entry>(u.contents);
                    for(size_t j = 0; j < 2; ++j)
                        check_reference(e.guards[j],
                                        index_path(q + ".guards", j));
                    check_cell(e.payload, q + ".payload");
                }
            }
        };
        for(size_t i = 0; i < heap.roots.size(); ++i)
            check_cell(heap.roots[i], index_path("$.roots", i));
        check_units(heap.external_weak, "$.external_weak",
                    UnitStorage::External);
        for(size_t i = 0; i < count; ++i)
        {
            const auto &a = *allocations[i];
            auto p = index_path("allocation", i);
            for(size_t j = 0; j < a.strong.size(); ++j)
            {
                check_cell(a.strong[j], index_path(p + ".strong", j));
                if(auto *ref = std::get_if<Reference>(&a.strong[j]);
                   ref && *ref &&
                   !std::holds_alternative<YoungOriginal>(a.state) &&
                   std::holds_alternative<YoungOriginal>(
                       allocations[(*ref)->value]->state) &&
                   !remembered.contains(a.allocation_id.value))
                    fail(p + ".strong",
                         "unremembered old-to-young strong edge");
            }
            check_units(a.weak, p + ".weak",
                        destination(a.state) ? UnitStorage::Destination
                                             : UnitStorage::Original);
            auto check_weak_target = [&](const Reference &ref) {
                if(ref && !std::holds_alternative<YoungOriginal>(a.state) &&
                   std::holds_alternative<YoungOriginal>(
                       allocations[ref->value]->state) &&
                   !remembered.contains(a.allocation_id.value))
                    fail(p + ".weak", "unremembered old-to-young weak edge");
            };
            for(const auto &unit: a.weak)
            {
                const Cell *cell;
                if(auto *field = std::get_if<Field>(&unit.contents))
                    cell = &field->value;
                else
                {
                    const auto &entry = std::get<Entry>(unit.contents);
                    for(const auto &guard: entry.guards)
                        check_weak_target(guard);
                    cell = &entry.payload;
                }
                if(auto *ref = std::get_if<Reference>(cell))
                    check_weak_target(*ref);
            }
        }
        uint64_t next = 0;
        for(auto id: unit_ids)
            if(id != next++)
                fail("$.weak", "logical unit IDs must be dense");
        for(size_t i = 0; i < originals; ++i)
        {
            if(!origins[i])
                fail("$.objects", "missing original identity");
            const auto &original = *origins[i];
            const auto *h = movable_header(original.state);
            const auto *f = h ? std::get_if<ForwardedHeader>(h) : nullptr;
            const auto *copy = copies[i];
            if(static_cast<bool>(f) != static_cast<bool>(copy))
                fail("$.objects",
                     "destination and original require reciprocal forwarding");
            if(!copy)
                continue;
            if(f->forwarded_to != copy->allocation_id)
                fail("$.objects",
                     "forwarding must name the same object's destination");
            auto placement = std::visit(
                [](const auto &s) {
                    using T = std::decay_t<decltype(s)>;
                    if constexpr(std::is_same_v<T, YoungOriginal> ||
                                 std::is_same_v<T, OldOriginal>)
                        return s.copy_to;
                    else
                        return CopyTo::Ordinary;  // Nonmovable originals were
                                                  // rejected above.
                },
                original.state);
            if((placement == CopyTo::Ordinary) !=
               std::holds_alternative<OrdinaryDestination>(copy->state))
                fail("$.objects", "destination disagrees with copy_to");
            if(original.weak.size() != copy->weak.size())
                fail("$.objects", "destination weak layout size differs");
            for(size_t j = 0; j < original.weak.size(); ++j)
                if(original.weak[j].unit_id != copy->weak[j].unit_id ||
                   original.weak[j].contents.index() !=
                       copy->weak[j].contents.index())
                    fail("$.objects", "destination weak layout differs");
        }
    }

    void validate_fresh_heap(const HeapSnapshot &heap)
    {
        validate_heap(heap);
        for(const auto &a: heap.objects)
        {
            if(destination(a.state))
                fail("$.objects", "fresh heap cannot contain destinations");
            if(auto *h = movable_header(a.state))
            {
                auto *normal = std::get_if<NormalHeader>(h);
                if(!normal || normal->marked)
                    fail("$.objects",
                         "fresh heap requires normal unmarked headers");
            }
            else if(std::get<NonmovingOriginal>(a.state).marked)
                fail("$.objects", "fresh heap requires clear marks");
        }
    }

    std::string write_heap(const HeapSnapshot &heap)
    {
        validate_heap(heap);
        std::vector<const Allocation *> ordered(heap.objects.size());
        for(const auto &a: heap.objects)
            ordered[a.allocation_id.value] = &a;
        auto objects = Json::array();
        for(const auto *a: ordered)
            objects.push_back({{"object_id", a->object_id.value},
                               {"allocation_id", a->allocation_id.value},
                               {"state", state_json(a->state)},
                               {"strong", cells_json(a->strong)},
                               {"weak", units_json(a->weak)}});
        Json value{{"format", "clover-gc-heap"},
                   {"version", 2},
                   {"roots", cells_json(heap.roots)},
                   {"objects", objects},
                   {"external_weak", units_json(heap.external_weak)},
                   {"remembered", Json::array()}};
        for(auto id: heap.remembered)
            value["remembered"].push_back(id.value);
        return value.dump(2) + "\n";
    }
}  // namespace cl::gc::test::format
