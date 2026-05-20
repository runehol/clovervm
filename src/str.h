#ifndef CL_STRING_H
#define CL_STRING_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned2.h"
#include "owned_typed_value.h"
#include "value.h"
#include "value_state.h"
#include <assert.h>
#include <cstring>
#include <stdint.h>
#include <string>
#include <wchar.h>

namespace cl
{
    typedef wchar_t cl_wchar;

    class ClassObject;
    class Shape;
    class VirtualMachine;

    class String : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::String;

        String(ClassObject *cls, const cl_wchar *_data, TValue2<SMI> _count)
            : Object(cls, native_layout), count(_count)
        {
            size_t n_chars = count.extract();
            memcpy(&this->data[0], _data, n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
        }

        String(const cl_wchar *_data, TValue2<SMI> _count)
            : Object(BootstrapObjectTag{}, native_layout), count(_count)
        {
            size_t n_chars = count.extract();
            memcpy(&this->data[0], _data, n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
        }

        String(ClassObject *cls, const cl_wchar *_data)
            : Object(cls, native_layout),
              count(TValue2<SMI>::from_smi(wcslen(_data)))
        {
            size_t n_chars = count.extract();
            memcpy(&this->data[0], _data, n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
        }

        String(const cl_wchar *_data)
            : Object(BootstrapObjectTag{}, native_layout),
              count(TValue2<SMI>::from_smi(wcslen(_data)))
        {
            size_t n_chars = count.extract();
            memcpy(&this->data[0], _data, n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
        }

        String(ClassObject *cls, const std::wstring &str)
            : Object(cls, native_layout),
              count(TValue2<SMI>::from_smi(str.size()))
        {
            size_t n_chars = count.extract();
            memcpy(&this->data[0], str.data(), n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
        }

        String(const std::wstring &str)
            : Object(BootstrapObjectTag{}, native_layout),
              count(TValue2<SMI>::from_smi(str.size()))
        {
            size_t n_chars = count.extract();
            memcpy(&this->data[0], str.data(), n_chars * sizeof(cl_wchar));
            this->data[n_chars] = 0;  // zero terminate for good measure
        }

        void install_bootstrap_class(ClassObject *new_cls);

        Member2<TValue2<SMI>> count;
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
        static size_t size_for(TValue2<SMI> count)
        {
            return size_for(size_t(count.extract()));
        }
        static size_t size_for(ClassObject *, TValue2<SMI> count)
        {
            return size_for(count);
        }
        static size_t size_for(ClassObject *, const cl_wchar *str)
        {
            return size_for(str);
        }
        static size_t size_for(ClassObject *, const std::wstring &str)
        {
            return size_for(str);
        }
        static size_t object_size_in_bytes(const String *str)
        {
            return size_for(size_t(str->count.extract()));
        }

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(String, Object, 1);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(String, String::object_size_in_bytes);
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
    void install_str_class_methods(VirtualMachine *vm);

    static inline bool string_eq(TValue<String> a, TValue<String> b)
    {
        {
            // pointer equality -> true
            if(a.raw_value().as.integer == b.raw_value().as.integer)
            {
                return true;
            }

            // if both are interned, that's good enough
            if((a.raw_value().as.integer & b.raw_value().as.integer) &
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
