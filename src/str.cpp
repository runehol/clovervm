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

    static Value native_str_str(ThreadState *thread, Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__str__ expects a str receiver");
        }
        return self;
    }

    static Value native_str_repr(ThreadState *thread, Value self)
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

    static Value native_str_len(ThreadState *thread, Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__len__ expects a str receiver");
        }
        return self.get_ptr<String>()->count.raw_value();
    }

    static Value native_str_add(ThreadState *thread, Value left_value,
                                Value right_value)
    {
        if(!can_convert_to<String>(left_value) ||
           !can_convert_to<String>(right_value))
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"UnimplementedError");
        }

        return left_value.get_ptr<String>()
            ->concat(right_value.get_ptr<String>())
            .raw_value();
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

    Value string_get_item(TValue<String> string, int64_t py_idx)
    {
        return string.extract()->char_at(py_idx);
    }

    static Value native_str_getitem(ThreadState *thread, Value self,
                                    Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"__getitem__"));
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"string indices must be integers", py_idx));
        return self.get_ptr<String>()->char_at(py_idx);
    }

    static Value native_str_lower(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"lower"));
        return self.get_ptr<String>()->lower().raw_value();
    }

    static Value native_str_upper(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"upper"));
        return self.get_ptr<String>()->upper().raw_value();
    }

    static Value native_str_startswith(ThreadState *thread, Value self,
                                       Value prefix_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"startswith"));
        CL_PROPAGATE_EXCEPTION(require_string_argument(
            prefix_value, L"startswith first arg must be str"));
        return self.get_ptr<String>()->startswith(
                   prefix_value.get_ptr<String>())
                   ? Value::True()
                   : Value::False();
    }

    static Value native_str_endswith(ThreadState *thread, Value self,
                                     Value suffix_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"endswith"));
        CL_PROPAGATE_EXCEPTION(require_string_argument(
            suffix_value, L"endswith first arg must be str"));
        return self.get_ptr<String>()->endswith(suffix_value.get_ptr<String>())
                   ? Value::True()
                   : Value::False();
    }

    static Value native_str_find(ThreadState *thread, Value self,
                                 Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"find"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        return Value::from_smi(
            self.get_ptr<String>()->find(needle_value.get_ptr<String>()));
    }

    static Value native_str_index(ThreadState *thread, Value self,
                                  Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"index"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        return self.get_ptr<String>()->index(needle_value.get_ptr<String>());
    }

    static Value native_str_count(ThreadState *thread, Value self,
                                  Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"count"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        return Value::from_smi(self.get_ptr<String>()->count_substring(
            needle_value.get_ptr<String>()));
    }

    static Value native_str_replace(ThreadState *thread, Value self,
                                    Value old_value, Value new_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"replace"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(old_value, L"replace old must be str"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(new_value, L"replace new must be str"));
        return self.get_ptr<String>()
            ->replace(old_value.get_ptr<String>(), new_value.get_ptr<String>())
            .raw_value();
    }

    static bool is_strip_space(wchar_t ch) { return std::iswspace(ch) != 0; }

    static Value native_str_strip(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"strip"));
        return self.get_ptr<String>()->strip().raw_value();
    }

    static Value native_str_lstrip(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"lstrip"));
        return self.get_ptr<String>()->lstrip().raw_value();
    }

    static Value native_str_rstrip(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"rstrip"));
        return self.get_ptr<String>()->rstrip().raw_value();
    }

    static bool is_string_sequence(Value value)
    {
        return can_convert_to<List>(value) || can_convert_to<Tuple>(value);
    }

    static Value native_str_join(ThreadState *thread, Value self,
                                 Value sequence)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"join"));
        if(!is_string_sequence(sequence))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.join expects a list or tuple");
        }

        if(can_convert_to<List>(sequence))
        {
            List *list = sequence.get_ptr<List>();
            for(size_t idx = 0; idx < list->size(); ++idx)
            {
                CL_PROPAGATE_EXCEPTION(require_string_argument(
                    list->item_unchecked(idx), L"sequence item must be str"));
            }
            return self.get_ptr<String>()->join_list(list).raw_value();
        }

        Tuple *tuple = sequence.get_ptr<Tuple>();
        for(size_t idx = 0; idx < tuple->size(); ++idx)
        {
            CL_PROPAGATE_EXCEPTION(require_string_argument(
                tuple->item_unchecked(idx), L"sequence item must be str"));
        }
        return self.get_ptr<String>()->join_tuple(tuple).raw_value();
    }

    static Value native_str_isalpha(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isalpha"));
        return self.get_ptr<String>()->isalpha() ? Value::True()
                                                 : Value::False();
    }

    static Value native_str_isdigit(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isdigit"));
        return self.get_ptr<String>()->isdigit() ? Value::True()
                                                 : Value::False();
    }

    static Value native_str_isalnum(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isalnum"));
        return self.get_ptr<String>()->isalnum() ? Value::True()
                                                 : Value::False();
    }

    static Value native_str_isspace(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isspace"));
        return self.get_ptr<String>()->isspace() ? Value::True()
                                                 : Value::False();
    }

    Value String::char_at(int64_t py_idx) const
    {
        int64_t length = count.extract();
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
        std::wstring result(1, data[static_cast<size_t>(normalized)]);
        return active_thread()->make_object_value<String>(result).raw_value();
    }

    TValue<String> String::concat(const String *other) const
    {
        std::wstring result(data, size_t(count.extract()));
        result.append(other->data, size_t(other->count.extract()));
        return active_thread()->make_object_value<String>(result);
    }

    TValue<String> String::lower() const
    {
        std::wstring result(data, size_t(count.extract()));
        for(wchar_t &ch: result)
        {
            ch = static_cast<wchar_t>(std::towlower(ch));
        }
        return active_thread()->make_object_value<String>(result);
    }

    TValue<String> String::upper() const
    {
        std::wstring result(data, size_t(count.extract()));
        for(wchar_t &ch: result)
        {
            ch = static_cast<wchar_t>(std::towupper(ch));
        }
        return active_thread()->make_object_value<String>(result);
    }

    bool String::startswith(const String *prefix) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view prefix_view(prefix->data,
                                      size_t(prefix->count.extract()));
        return str.size() >= prefix_view.size() &&
               str.substr(0, prefix_view.size()) == prefix_view;
    }

    bool String::endswith(const String *suffix) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view suffix_view(suffix->data,
                                      size_t(suffix->count.extract()));
        return str.size() >= suffix_view.size() &&
               str.substr(str.size() - suffix_view.size()) == suffix_view;
    }

    int64_t String::find(const String *needle) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view needle_view(needle->data,
                                      size_t(needle->count.extract()));
        size_t found = str.find(needle_view);
        if(found == std::wstring_view::npos)
        {
            return -1;
        }
        return static_cast<int64_t>(found);
    }

    Value String::index(const String *needle) const
    {
        int64_t found = find(needle);
        if(found == -1)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"substring not found");
        }
        return Value::from_smi(found);
    }

    int64_t String::count_substring(const String *needle) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view needle_view(needle->data,
                                      size_t(needle->count.extract()));
        if(needle_view.empty())
        {
            return static_cast<int64_t>(str.size() + 1);
        }
        int64_t result = 0;
        size_t pos = 0;
        while(pos <= str.size())
        {
            size_t found = str.find(needle_view, pos);
            if(found == std::wstring_view::npos)
            {
                break;
            }
            ++result;
            pos = found + needle_view.size();
        }
        return result;
    }

    TValue<String> String::replace(const String *old,
                                   const String *replacement) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view old_view(old->data, size_t(old->count.extract()));
        std::wstring_view replacement_view(
            replacement->data, size_t(replacement->count.extract()));

        std::wstring result;
        if(old_view.empty())
        {
            result.append(replacement_view);
            for(wchar_t ch: str)
            {
                result.push_back(ch);
                result.append(replacement_view);
            }
            return active_thread()->make_object_value<String>(result);
        }

        size_t pos = 0;
        while(pos < str.size())
        {
            size_t found = str.find(old_view, pos);
            if(found == std::wstring_view::npos)
            {
                result.append(str.substr(pos));
                break;
            }
            result.append(str.substr(pos, found - pos));
            result.append(replacement_view);
            pos = found + old_view.size();
        }
        if(str.empty())
        {
            result.append(str);
        }
        return active_thread()->make_object_value<String>(result);
    }

    static TValue<String> strip_string(const String *str, bool strip_left,
                                       bool strip_right)
    {
        std::wstring_view view(str->data, size_t(str->count.extract()));
        size_t start = 0;
        size_t end = view.size();
        if(strip_left)
        {
            while(start < end && is_strip_space(view[start]))
            {
                ++start;
            }
        }
        if(strip_right)
        {
            while(end > start && is_strip_space(view[end - 1]))
            {
                --end;
            }
        }
        return active_thread()->make_object_value<String>(
            std::wstring(view.substr(start, end - start)));
    }

    TValue<String> String::strip() const
    {
        return strip_string(this, true, true);
    }

    TValue<String> String::lstrip() const
    {
        return strip_string(this, true, false);
    }

    TValue<String> String::rstrip() const
    {
        return strip_string(this, false, true);
    }

    static void append_join_item(std::wstring &result,
                                 std::wstring_view separator, bool need_sep,
                                 Value item)
    {
        if(need_sep)
        {
            result.append(separator);
        }
        result.append(string_view(TValue<String>::from_value_assumed(item)));
    }

    TValue<String> String::join_list(const List *sequence) const
    {
        std::wstring_view separator(data, size_t(count.extract()));
        std::wstring result;
        for(size_t idx = 0; idx < sequence->size(); ++idx)
        {
            append_join_item(result, separator, idx != 0,
                             sequence->item_unchecked(idx));
        }
        return active_thread()->make_object_value<String>(result);
    }

    TValue<String> String::join_tuple(const Tuple *sequence) const
    {
        std::wstring_view separator(data, size_t(count.extract()));
        std::wstring result;
        for(size_t idx = 0; idx < sequence->size(); ++idx)
        {
            append_join_item(result, separator, idx != 0,
                             sequence->item_unchecked(idx));
        }
        return active_thread()->make_object_value<String>(result);
    }

    static bool classify_string(const String *str,
                                int (*predicate)(std::wint_t))
    {
        if(str->count.extract() == 0)
        {
            return false;
        }
        for(size_t idx = 0; idx < size_t(str->count.extract()); ++idx)
        {
            if(predicate(str->data[idx]) == 0)
            {
                return false;
            }
        }
        return true;
    }

    bool String::isalpha() const
    {
        return classify_string(this, std::iswalpha);
    }

    bool String::isdigit() const
    {
        return classify_string(this, std::iswdigit);
    }

    bool String::isalnum() const
    {
        return classify_string(this, std::iswalnum);
    }

    bool String::isspace() const
    {
        return classify_string(this, std::iswspace);
    }

    BuiltinClassDefinition make_str_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::String};
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class<String>(
            vm->get_or_create_interned_string_value(L"str"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
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
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->str_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
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
