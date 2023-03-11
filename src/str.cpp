#include "str.h"
#include "klass.h"

namespace cl
{
    static Value string_str(Value s)
    {
        return s;
    }

    Klass cl_string_klass(L"string", string_str);

    uint64_t string_hash(Value s)
    {
        assert(s.is_ptr() && s.get_ptr()->klass == &cl_string_klass);
        String *str = reinterpret_cast<String *>(s.get_ptr());
        uint64_t len = str->count.get_smi();

        cl_wchar *c = &str->data[0];
        uint64_t hash = 5381;
        for(uint64_t i = 0; i < len; ++i)
        {
            hash = hash*33 + c[i];
        }
        return hash;

    }

    const cl_wchar *string_as_wchar_t(Value s)
    {
        assert(s.is_ptr() && s.get_ptr()->klass == &cl_string_klass);
        String *str = reinterpret_cast<String *>(s.get_ptr());
        cl_wchar *c = &str->data[0];
        return c;
    }


    bool string_eq(Value a, Value b)
    {
        // value equality -> true
        if(a.as.integer == b.as.integer) return true;

        // one or both are inline or interned pointers values -> false
        if(((a.as.integer | b.as.integer) & value_refcounted_ptr_tag) == 0) return false;


        const Object *oa = a.get_ptr();
        const Object *ob = b.get_ptr();
        if(oa->klass != ob->klass) return false;

        assert(oa->klass == &cl_string_klass);

        // fine, compare the elements then
        const String *sa = reinterpret_cast<const String *>(oa);
        const String *sb = reinterpret_cast<const String *>(ob);

        if(sa->count != sb->count) return false;

        uint64_t len = sa->count.get_smi();

        for(uint64_t i = 0; i < len; ++i)
        {
            if(sa->data[i] != sb->data[i]) return false;
        }
        return true;


    }

}
