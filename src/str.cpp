#include "str.h"
#include "klass.h"

namespace cl
{
    static Value string_str(Value s) { return s; }

    Klass cl_string_klass(L"string", string_str);

    uint64_t string_hash(TValue<String> s)
    {
        String *str = s.get();
        uint64_t len = str->count.get().get();

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
        String *str = s.get();
        cl_wchar *c = &str->data[0];
        return c;
    }

    bool string_eq(TValue<String> a, TValue<String> b)
    {
        // value equality -> true
        if(a.raw().as.integer == b.raw().as.integer)
            return true;

        if(!a.raw().is_refcounted_ptr() || !b.raw().is_refcounted_ptr())
            return false;

        const String *sa = a.get();
        const String *sb = b.get();

        if(sa->count != sb->count)
            return false;

        uint64_t len = sa->count.get().get();

        for(uint64_t i = 0; i < len; ++i)
        {
            if(sa->data[i] != sb->data[i])
                return false;
        }
        return true;
    }

}  // namespace cl
