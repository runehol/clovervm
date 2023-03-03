#ifndef CL_OBJECT_H
#define CL_OBJECT_H

#include <cstdint>
#include <type_traits>

namespace cl
{
    struct Klass;
/*
  Base class for all language objects, i.e. indirect values
*/
    struct Object
    {
        constexpr Object(const Klass *_klass, uint32_t _n_cells, uint32_t _size_in_cells)
            : klass(_klass),
              refcount(0),
              n_cells(_n_cells),
              size_in_cells(_size_in_cells)
        {}

        constexpr Object(const Klass *_klass, uint32_t _n_cells)
            : klass(_klass),
              refcount(0),
              n_cells(_n_cells),
              size_in_cells(_n_cells)
        {}

        const struct Klass *klass;
        int32_t refcount;
        uint16_t n_cells;
        uint16_t size_in_cells;
    };

    static_assert(std::is_trivially_destructible_v<Object>);



}

#endif //CL_OBJECT_H
