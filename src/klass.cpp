#include "klass.h"
#include "refcount.h"
#include "thread_state.h"

namespace cl
{
    static Value klass_str(Value v)
    {
        const Klass *k = (const Klass *)v.get_ptr();
        const cl_wchar *klass_name = k->klass_name;

        return ThreadState::get_active()->make_refcounted_sized_value<String>(
            String::size_for(klass_name), klass_name);
    }

    Klass cl_klass_klass(L"class", klass_str);

}  // namespace cl
