#ifndef CL_OWNED_TYPED_VALUE_H
#define CL_OWNED_TYPED_VALUE_H

#include "owned.h"
#include "typed_value.h"

namespace cl
{
    template <typename T> struct HandleTraits<TValue<T>>
    {
        using extracted_type = typename ValueTypeTraits<T>::get_type;

        static TValue<T> from_value(Value value) { return TValue<T>(value); }
        static TValue<T> from_value_unchecked(Value value)
        {
            return TValue<T>::unsafe_unchecked(value);
        }
        static Value to_value(TValue<T> value) { return Value(value); }
        static TValue<T> none() { return from_value_unchecked(Value::None()); }
        static constexpr RefcountPolicy refcount_policy =
            ValueTypeTraits<T>::refcount_policy;

        template <typename E = extracted_type,
                  typename = std::enable_if_t<!std::is_void_v<E>>>
        static extracted_type extract(TValue<T> value)
        {
            return value.extract();
        }

        static TValue<T> retain_ref(TValue<T> value)
        {
            if constexpr(refcount_policy == RefcountPolicy::Never)
            {
                return value;
            }
            else if constexpr(refcount_policy == RefcountPolicy::Always)
            {
                if(value.as_value() != Value::None())
                {
                    incref_heap_ptr(value.as_value().as.ptr);
                }
            }
            else
            {
                incref(value.as_value());
            }
            return value;
        }

        static void release_ref(TValue<T> value)
        {
            if constexpr(refcount_policy == RefcountPolicy::Never)
            {
                return;
            }
            else if constexpr(refcount_policy == RefcountPolicy::Always)
            {
                if(value.as_value() != Value::None())
                {
                    decref_heap_ptr(value.as_value().as.ptr);
                }
            }
            else
            {
                decref(value.as_value());
            }
        }
    };

    template <typename T> using OwnedTValue = Owned<TValue<T>>;
    template <typename T> using MemberTValue = Member<TValue<T>>;

}  // namespace cl

#endif  // CL_OWNED_TYPED_VALUE_H
