#ifndef CJ_STRING_H
#define CJ_STRING_H

#include "cj_object.h"
#include "cj_alloc.h"
#include "cj_value.h"
#include <stdint.h>
#include <assert.h>
#include <wchar.h>

typedef wchar_t cj_wchar;

typedef struct cj_string
{
    cj_object obj;
    cj_value count;
    cj_wchar data[];

} cj_string;

static inline void string_init(cj_string *vec, const cj_wchar *data, cj_value count)
{
    assert(value_is_smi(count));
    object_init(&vec->obj, NULL, 1, 1 + value_get_smi(count)*sizeof(cj_wchar));
    vec->count = count;
}

static inline cj_value string_make(const cj_wchar *data, cj_value count)
{
    cj_string *s = (cj_string *)cj_alloc(sizeof(cj_string)+value_get_smi(count) * sizeof(cj_wchar));
    string_init(s, data, count);
    return value_make_oop(&s->obj);
}





#endif //CJ_STRING_H