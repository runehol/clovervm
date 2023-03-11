#ifndef CL_FUNCTION_H
#define CL_FUNCTION_H

#include <vector>
#include "object.h"
#include "klass.h"
#include "refcount.h"
#include "indirect_dict.h"
#include "value.h"

namespace cl
{

    //may need closures and stuff later. TBD
    class Function : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"Function", nullptr);

        Function(Value _code_object)
            : Object(&klass, 1, sizeof(Scope)/8),
              code_object(incref(_code_object))
        {}

        Value code_object;
    };




};



#endif //CL_FUNCTION_H
