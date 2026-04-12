#ifndef CL_STRING_H
#define CL_STRING_H

#include "object.h"
#include "typed_value.h"
#include "value.h"
#include <assert.h>
#include <cstring>
#include <stdint.h>
#include <string>
#include <wchar.h>

namespace cl
{
    typedef wchar_t cl_wchar;

    extern struct Klass cl_string_klass;

    struct String : public Object
    {

        String(const cl_wchar *_data, TValue<SMI> _count)
            : Object(&cl_string_klass, 1,
                     sizeof(Value) + (_count.get() + 1) * sizeof(cl_wchar))
        {
            size_t n_chars = _count.get();
            memcpy(&this->data[0], _data, n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
            count = _count;
        }

        String(const cl_wchar *_data)
            : Object(&cl_string_klass, 1,
                     sizeof(Value) + (wcslen(_data) + 1) * sizeof(cl_wchar))
        {
            size_t n_chars = wcslen(_data);
            memcpy(&this->data[0], _data, n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
            count = TValue<SMI>(Value::from_smi(n_chars));
        }

        String(const std::wstring &str)
            : Object(&cl_string_klass, 1,
                     sizeof(Value) + (str.size() + 1) * sizeof(cl_wchar))
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
    bool string_eq(TValue<String> a, TValue<String> b);
    const cl_wchar *string_as_wchar_t(TValue<String> s);

    static_assert(std::is_trivially_destructible_v<String>);
}  // namespace cl

#endif  // CL_STRING_H
