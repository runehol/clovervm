#ifndef CL_STRING_H
#define CL_STRING_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned_typed_value.h"
#include "value.h"
#include <assert.h>
#include <cstring>
#include <stdint.h>
#include <string>
#include <wchar.h>

namespace cl
{
    typedef wchar_t cl_wchar;

    class ClassObject;
    class VirtualMachine;

    struct String : public Object
    {
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::String;

        String(const cl_wchar *_data, TValue<SMI> _count)
            : Object(native_layout_id)
        {
            size_t n_chars = _count.extract();
            memcpy(&this->data[0], _data, n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
            count = _count;
        }

        String(const cl_wchar *_data) : Object(native_layout_id)
        {
            size_t n_chars = wcslen(_data);
            memcpy(&this->data[0], _data, n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
            count = TValue<SMI>(Value::from_smi(n_chars));
        }

        String(const std::wstring &str) : Object(native_layout_id)
        {
            size_t n_chars = str.size();
            memcpy(&this->data[0], str.data(), n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
            count = TValue<SMI>(Value::from_smi(n_chars));
        }

        MemberTValue<SMI> count;
        cl_wchar data[1];

        static size_t size_for(const std::wstring &str)
        {
            return sizeof(String) + str.size() * sizeof(cl_wchar);
        }
        static size_t size_for(const cl_wchar *str)
        {
            return sizeof(String) + wcslen(str) * sizeof(cl_wchar);
        }
        static size_t size_for(size_t n_chars)
        {
            return sizeof(String) + n_chars * sizeof(cl_wchar);
        }

        static DynamicLayoutSpec layout_spec_for(TValue<SMI> count)
        {
            return DynamicLayoutSpec{
                round_up_to_16byte_units(size_for(size_t(count.extract()))), 1};
        }

        static DynamicLayoutSpec layout_spec_for(const cl_wchar *str)
        {
            return DynamicLayoutSpec{round_up_to_16byte_units(size_for(str)),
                                     1};
        }

        static DynamicLayoutSpec layout_spec_for(const std::wstring &str)
        {
            return DynamicLayoutSpec{round_up_to_16byte_units(size_for(str)),
                                     1};
        }

        CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(String, count);
    };

    static inline bool operator==(const String &a, const std::wstring &b)
    {
        return a.data == b;
    }

    static inline bool operator==(const std::wstring &a, const String &b)
    {
        return a == b.data;
    }

    uint64_t string_hash(TValue<String> s);
    bool string_eq_slow_path(TValue<String> a, TValue<String> b);

    const cl_wchar *string_as_wchar_t(TValue<String> s);
    BuiltinClassDefinition make_str_class(VirtualMachine *vm);

    static inline bool string_eq(TValue<String> a, TValue<String> b)
    {
        {
            // pointer equality -> true
            if(a.as_value().as.integer == b.as_value().as.integer)
            {
                return true;
            }

            // if both are interned, that's good enough
            if((a.as_value().as.integer & b.as_value().as.integer) &
               value_interned_ptr_tag)
            {
                return false;
            }

            // otherwise compare the strings byte per byte
            return string_eq_slow_path(a, b);
        }
    }

    static_assert(std::is_trivially_destructible_v<String>);
}  // namespace cl

#endif  // CL_STRING_H
