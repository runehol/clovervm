#include "klass.h"
#include "refcount.h"
#include "thread_state.h"

namespace cl
{
    static Value klass_str(Value v)
    {
        const Klass *k = (const Klass*)v.get_ptr();
        const cl_wchar *klass_name = k->klass_name;

        void *mem = ThreadState::get_active()->allocate_refcounted(String::size_for(klass_name));
        String *str = new(mem)String(klass_name);
        return Value::from_oop(str);
    }

    Klass cl_klass_klass(L"class", klass_str);

}
