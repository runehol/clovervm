#include "str.h"
#include "klass.h"

namespace cl
{
    static Value string_str(Value s) { return s; }

    Klass cl_string_klass(L"string", string_str);

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
