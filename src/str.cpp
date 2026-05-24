#include "str.h"
#include "class_object.h"
#include "exception_propagation.h"
#include "list.h"
#include "native_function.h"
#include "string_builder.h"
#include "thread_state.h"
#include "tuple.h"
#include "unicode.h"
#include "virtual_machine.h"
#include <cwctype>
#include <iterator>

namespace cl
{
    void String::install_bootstrap_class(ClassObject *new_cls)
    {
        assert(new_cls != nullptr);
        if(!Object::is_class_bootstrapped())
        {
            Object::install_bootstrap_class(new_cls);
        }
        else
        {
            assert(Object::get_shape()->get_class() == new_cls);
        }
        if(Object::get_shape() == nullptr)
        {
            Object::set_shape(new_cls->get_instance_root_shape());
        }
    }

    static Value native_str_str(Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__str__ expects a str receiver");
        }
        return self;
    }

    static Value native_str_repr(Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__repr__ expects a str receiver");
        }

        String *str = self.get_ptr<String>();
        StringBuilder builder;
        builder.append_char(L'\'');
        size_t n_chars = size_t(str->count.extract());
        for(size_t idx = 0; idx < n_chars; ++idx)
        {
            switch(str->data[idx])
            {
                case L'\\':
                    builder.append_c_str(L"\\\\");
                    break;
                case L'\'':
                    builder.append_c_str(L"\\'");
                    break;
                case L'\n':
                    builder.append_c_str(L"\\n");
                    break;
                case L'\r':
                    builder.append_c_str(L"\\r");
                    break;
                case L'\t':
                    builder.append_c_str(L"\\t");
                    break;
                default:
                    builder.append_char(str->data[idx]);
                    break;
            }
        }
        builder.append_char(L'\'');
        return builder.finish();
    }

    static Value native_str_len(Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__len__ expects a str receiver");
        }
        return self.get_ptr<String>()->count.raw_value();
    }

    static Value native_str_add(Value left_value, Value right_value)
    {
        if(!can_convert_to<String>(left_value) ||
           !can_convert_to<String>(right_value))
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"UnimplementedError");
        }

        String *left = left_value.get_ptr<String>();
        String *right = right_value.get_ptr<String>();
        std::wstring result(left->data, size_t(left->count.extract()));
        result.append(right->data, size_t(right->count.extract()));
        return active_thread()->make_object_value<String>(result).raw_value();
    }

    static Value require_str_receiver(Value self, const wchar_t *method_name)
    {
        if(!can_convert_to<String>(self))
        {
            std::wstring message = L"str.";
            message += method_name;
            message += L" expects a str receiver";
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        return Value::None();
    }

    static Value require_string_argument(Value value, const wchar_t *message)
    {
        if(!can_convert_to<String>(value))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message);
        }
        return Value::None();
    }

    static Value require_smi_index(Value value, const wchar_t *message,
                                   int64_t &out)
    {
        if(!value.is_smi())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message);
        }
        out = value.get_smi();
        return Value::None();
    }

    static Value str_char_at(TValue<String> string, int64_t py_idx)
    {
        String *str = string.extract();
        int64_t length = str->count.extract();
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += length;
        }
        if(normalized < 0 || normalized >= length)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"IndexError", L"string index out of range");
        }
        std::wstring result(1, str->data[static_cast<size_t>(normalized)]);
        return active_thread()->make_object_value<String>(result).raw_value();
    }

    Value string_get_item(TValue<String> string, int64_t py_idx)
    {
        return str_char_at(string, py_idx);
    }

    static Value native_str_getitem(Value self, Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"__getitem__"));
        int64_t py_idx;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"string indices must be integers", py_idx));
        return str_char_at(TValue<String>::from_value_assumed(self), py_idx);
    }

    static Value native_str_lower(Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"lower"));
        std::wstring result(
            string_view(TValue<String>::from_value_assumed(self)));
        for(wchar_t &ch: result)
        {
            ch = static_cast<wchar_t>(std::towlower(ch));
        }
        return active_thread()->make_object_value<String>(result).raw_value();
    }

    static Value native_str_upper(Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"upper"));
        std::wstring result(
            string_view(TValue<String>::from_value_assumed(self)));
        for(wchar_t &ch: result)
        {
            ch = static_cast<wchar_t>(std::towupper(ch));
        }
        return active_thread()->make_object_value<String>(result).raw_value();
    }

    static Value native_str_startswith(Value self, Value prefix_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"startswith"));
        CL_PROPAGATE_EXCEPTION(require_string_argument(
            prefix_value, L"startswith first arg must be str"));
        std::wstring_view str =
            string_view(TValue<String>::from_value_assumed(self));
        std::wstring_view prefix =
            string_view(TValue<String>::from_value_assumed(prefix_value));
        return str.size() >= prefix.size() &&
                       str.substr(0, prefix.size()) == prefix
                   ? Value::True()
                   : Value::False();
    }

    static Value native_str_endswith(Value self, Value suffix_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"endswith"));
        CL_PROPAGATE_EXCEPTION(require_string_argument(
            suffix_value, L"endswith first arg must be str"));
        std::wstring_view str =
            string_view(TValue<String>::from_value_assumed(self));
        std::wstring_view suffix =
            string_view(TValue<String>::from_value_assumed(suffix_value));
        return str.size() >= suffix.size() &&
                       str.substr(str.size() - suffix.size()) == suffix
                   ? Value::True()
                   : Value::False();
    }

    static Value native_str_find(Value self, Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"find"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        std::wstring_view str =
            string_view(TValue<String>::from_value_assumed(self));
        std::wstring_view needle =
            string_view(TValue<String>::from_value_assumed(needle_value));
        size_t found = str.find(needle);
        if(found == std::wstring_view::npos)
        {
            return Value::from_smi(-1);
        }
        return Value::from_smi(static_cast<int64_t>(found));
    }

    static Value native_str_index(Value self, Value needle_value)
    {
        Value found = native_str_find(self, needle_value);
        CL_PROPAGATE_EXCEPTION(found);
        if(found.get_smi() == -1)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"substring not found");
        }
        return found;
    }

    static Value native_str_count(Value self, Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"count"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        std::wstring_view str =
            string_view(TValue<String>::from_value_assumed(self));
        std::wstring_view needle =
            string_view(TValue<String>::from_value_assumed(needle_value));
        if(needle.empty())
        {
            return Value::from_smi(static_cast<int64_t>(str.size() + 1));
        }
        int64_t count = 0;
        size_t pos = 0;
        while(pos <= str.size())
        {
            size_t found = str.find(needle, pos);
            if(found == std::wstring_view::npos)
            {
                break;
            }
            ++count;
            pos = found + needle.size();
        }
        return Value::from_smi(count);
    }

    static Value native_str_replace(Value self, Value old_value,
                                    Value new_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"replace"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(old_value, L"replace old must be str"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(new_value, L"replace new must be str"));
        std::wstring_view str =
            string_view(TValue<String>::from_value_assumed(self));
        std::wstring_view old =
            string_view(TValue<String>::from_value_assumed(old_value));
        std::wstring_view replacement =
            string_view(TValue<String>::from_value_assumed(new_value));

        std::wstring result;
        if(old.empty())
        {
            result.append(replacement);
            for(wchar_t ch: str)
            {
                result.push_back(ch);
                result.append(replacement);
            }
            return active_thread()
                ->make_object_value<String>(result)
                .raw_value();
        }

        size_t pos = 0;
        while(pos < str.size())
        {
            size_t found = str.find(old, pos);
            if(found == std::wstring_view::npos)
            {
                result.append(str.substr(pos));
                break;
            }
            result.append(str.substr(pos, found - pos));
            result.append(replacement);
            pos = found + old.size();
        }
        if(str.empty())
        {
            result.append(str);
        }
        return active_thread()->make_object_value<String>(result).raw_value();
    }

    static bool is_strip_space(wchar_t ch) { return std::iswspace(ch) != 0; }

    static Value strip_string(Value self, bool strip_left, bool strip_right)
    {
        std::wstring_view str =
            string_view(TValue<String>::from_value_assumed(self));
        size_t start = 0;
        size_t end = str.size();
        if(strip_left)
        {
            while(start < end && is_strip_space(str[start]))
            {
                ++start;
            }
        }
        if(strip_right)
        {
            while(end > start && is_strip_space(str[end - 1]))
            {
                --end;
            }
        }
        return active_thread()
            ->make_object_value<String>(
                std::wstring(str.substr(start, end - start)))
            .raw_value();
    }

    static Value native_str_strip(Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"strip"));
        return strip_string(self, true, true);
    }

    static Value native_str_lstrip(Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"lstrip"));
        return strip_string(self, true, false);
    }

    static Value native_str_rstrip(Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"rstrip"));
        return strip_string(self, false, true);
    }

    static bool is_string_sequence(Value value)
    {
        return can_convert_to<List>(value) || can_convert_to<Tuple>(value);
    }

    static size_t sequence_size(Value value)
    {
        if(can_convert_to<List>(value))
        {
            return value.get_ptr<List>()->size();
        }
        return value.get_ptr<Tuple>()->size();
    }

    static Value sequence_item(Value value, size_t idx)
    {
        if(can_convert_to<List>(value))
        {
            return value.get_ptr<List>()->item_unchecked(idx);
        }
        return value.get_ptr<Tuple>()->item_unchecked(idx);
    }

    static Value native_str_join(Value self, Value sequence)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"join"));
        if(!is_string_sequence(sequence))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.join expects a list or tuple");
        }

        std::wstring_view separator =
            string_view(TValue<String>::from_value_assumed(self));
        std::wstring result;
        size_t n_items = sequence_size(sequence);
        for(size_t idx = 0; idx < n_items; ++idx)
        {
            Value item = sequence_item(sequence, idx);
            CL_PROPAGATE_EXCEPTION(
                require_string_argument(item, L"sequence item must be str"));
            if(idx != 0)
            {
                result.append(separator);
            }
            result.append(
                string_view(TValue<String>::from_value_assumed(item)));
        }
        return active_thread()->make_object_value<String>(result).raw_value();
    }

    static Value classify_string(Value self, const wchar_t *method_name,
                                 int (*predicate)(std::wint_t))
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, method_name));
        std::wstring_view str =
            string_view(TValue<String>::from_value_assumed(self));
        if(str.empty())
        {
            return Value::False();
        }
        for(wchar_t ch: str)
        {
            if(predicate(ch) == 0)
            {
                return Value::False();
            }
        }
        return Value::True();
    }

    static Value native_str_isalpha(Value self)
    {
        return classify_string(self, L"isalpha", std::iswalpha);
    }

    static Value native_str_isdigit(Value self)
    {
        return classify_string(self, L"isdigit", std::iswdigit);
    }

    static Value native_str_isalnum(Value self)
    {
        return classify_string(self, L"isalnum", std::iswalnum);
    }

    static Value native_str_isspace(Value self)
    {
        return classify_string(self, L"isspace", std::iswspace);
    }

    BuiltinClassDefinition make_str_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::String};
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class<String>(
            vm->get_or_create_interned_string_value(L"str"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }

    void install_str_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_str_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_str_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"__len__", native_str_len,
                                     L"Return len(self)."),
            builtin_intrinsic_method(L"__add__", native_str_add,
                                     L"Return self + value."),
            builtin_intrinsic_method(L"__getitem__", native_str_getitem,
                                     L"Return self[index]."),
            builtin_intrinsic_method(L"lower", native_str_lower,
                                     L"Return a lowercase copy."),
            builtin_intrinsic_method(L"upper", native_str_upper,
                                     L"Return an uppercase copy."),
            builtin_intrinsic_method(
                L"startswith", native_str_startswith,
                L"Return whether self starts with prefix."),
            builtin_intrinsic_method(L"endswith", native_str_endswith,
                                     L"Return whether self ends with suffix."),
            builtin_intrinsic_method(L"find", native_str_find,
                                     L"Return first substring index or -1."),
            builtin_intrinsic_method(L"index", native_str_index,
                                     L"Return first substring index."),
            builtin_intrinsic_method(
                L"count", native_str_count,
                L"Return number of substring occurrences."),
            builtin_intrinsic_method(L"replace", native_str_replace,
                                     L"Return a copy with replacements."),
            builtin_intrinsic_method(L"strip", native_str_strip,
                                     L"Return a stripped copy."),
            builtin_intrinsic_method(L"lstrip", native_str_lstrip,
                                     L"Return a left-stripped copy."),
            builtin_intrinsic_method(L"rstrip", native_str_rstrip,
                                     L"Return a right-stripped copy."),
            builtin_intrinsic_method(L"join", native_str_join,
                                     L"Join list or tuple of strings."),
            builtin_intrinsic_method(
                L"isalpha", native_str_isalpha,
                L"Return whether all chars are alphabetic."),
            builtin_intrinsic_method(L"isdigit", native_str_isdigit,
                                     L"Return whether all chars are digits."),
            builtin_intrinsic_method(
                L"isalnum", native_str_isalnum,
                L"Return whether all chars are alphanumeric."),
            builtin_intrinsic_method(
                L"isspace", native_str_isspace,
                L"Return whether all chars are whitespace."),
        };
        install_builtin_intrinsic_methods(vm, vm->str_class(), methods,
                                          std::size(methods));
    }

    uint64_t string_hash(TValue<String> s)
    {
        String *str = s.extract();
        uint64_t len = str->count.extract();

        cl_wchar *c = &str->data[0];
        uint64_t hash = 5381;
        for(uint64_t i = 0; i < len; ++i)
        {
            hash = hash * 33 + c[i];
        }
        return hash;
    }

    const cl_wchar *string_as_wchar_t(TValue<String> s)
    {
        String *str = s.extract();
        cl_wchar *c = &str->data[0];
        return c;
    }

    std::wstring_view string_view(TValue<String> s)
    {
        String *str = s.extract();
        return std::wstring_view(str->data, size_t(str->count.extract()));
    }

    std::optional<TValue<String>>
    try_make_string_from_utf8(ThreadState *thread, std::string_view bytes)
    {
        std::optional<unicode::Utf8WcharLayout> layout =
            unicode::validate_utf8_for_wchar(bytes);
        if(!layout.has_value())
        {
            return std::nullopt;
        }

        String *string = thread->make_object_raw<String>(TValue<SMI>::from_smi(
            static_cast<int64_t>(layout->code_unit_count)));
        if(!unicode::decode_utf8_into_wchar(bytes, string->data,
                                            layout->code_unit_count))
        {
            return std::nullopt;
        }
        string->data[layout->code_unit_count] = 0;
        return TValue<String>::from_oop(string);
    }

    bool string_eq_slow_path(TValue<String> a, TValue<String> b)
    {

        const String *sa = a.extract();
        const String *sb = b.extract();

        if(sa->count != sb->count)
            return false;

        uint64_t len = sa->count.extract();

        for(uint64_t i = 0; i < len; ++i)
        {
            if(sa->data[i] != sb->data[i])
                return false;
        }
        return true;
    }

}  // namespace cl
