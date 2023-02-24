#ifndef CL_STRING_H
#define CL_STRING_H

#include "object.h"
#include "alloc.h"
#include "value.h"
#include <stdint.h>
#include <assert.h>
#include <wchar.h>
#include <cstring>
#include <string>


namespace cl
{
    typedef wchar_t cl_wchar;

    extern struct Klass cl_string_klass;


    struct String : public Object
    {

        String(const cl_wchar *_data, Value _count)
        : Object(&cl_string_klass, 1, sizeof(Value) + (_count.get_smi() + 1) * sizeof(cl_wchar))
        {
            size_t n_chars = _count.get_smi();
            memcpy(&this->data[0], _data, n_chars*sizeof(cl_wchar));
            this->data[n_chars] = 0; // zero terminate for good measure
            count = _count;
        }

        String(const cl_wchar *_data)
        : Object(&cl_string_klass, 1, sizeof(Value) + (wcslen(_data) + 1) * sizeof(cl_wchar))
        {
            size_t n_chars = wcslen(_data);
            memcpy(&this->data[0], _data, n_chars*sizeof(cl_wchar));
            this->data[n_chars] = 0; // zero terminate for good measure
            count = Value::from_smi(n_chars);
        }

        String(const std::wstring &str)
        : Object(&cl_string_klass, 1, sizeof(Value) + (str.size() + 1) * sizeof(cl_wchar))
        {
            size_t n_chars = str.size();
            memcpy(&this->data[0], str.data(), n_chars*sizeof(cl_wchar));
            this->data[n_chars] = 0; // zero terminate for good measure
            count = Value::from_smi(n_chars);
        }


        Value count;
        cl_wchar data[1];


        static size_t size_for(const std::wstring &str)
        {
            return sizeof(String) + str.size()*sizeof(cl_wchar);
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


    static_assert(std::is_trivially_destructible_v<String>);

    Value make_interned_string(const cl_wchar *data);
}




#endif //CL_STRING_H
