# Dictionaries

Insertion ordered dictionaries:

- <https://morepypy.blogspot.com/2015/01/faster-more-memory-efficient-and-more.html>

Two tables:

- insertion order table of `[hash, key, value]`, all in `CLValue` representation. For scopes, this is also a good place to hang invalidation tables
- open probe table of `[position]`, with special values for empty and tombstone. representation TBD

Would be interesting to do the swiss table trick, but since we need the indirection to get insertion order, it's probably not possible:

- <https://abseil.io/about/design/swisstables>

Both tables should be refcounted and have allocator headers. No need for hidden class pointers and such.

Allocator header:

- refcount, 32 bit or 16 bit
- number of `CLValue` cells
- number of non-`CLValue` cells, for string bytes, probe table contents, and so on
- could we store things as `log2` to fit large values into few bytes?
- ideally the entire allocator header fits within 64 bits

Possible layout:

```c
uint32_t refcount;
uint16_t n_value_cells;
uint8_t n_raw_cells;
uint8_t log2_n_raw_cells;
```

`hash()` truncates the value returned from an object’s custom `__hash__()` method to the size of a `Py_ssize_t`. This is typically 8 bytes on 64-bit builds and 4 bytes on 32-bit builds. If an object’s `__hash__()` must interoperate on builds of different bit sizes, be sure to check the width on all supported builds. An easy way to do this is with `python -c "import sys; print(sys.hash_info.width)"`.

- <https://docs.python.org/3.11/reference/datamodel.html#specialnames>
- Descriptor arrays for hidden classes: <https://v8.dev/docs/hidden-classes>

Scopes:

- pointer to parent scope, `CLValue` refcounted
- open probe hash table to insertion order table
- insertion order table

Status note:

- the VM-managed array substrate and scope-specific split storage have now
  landed
- `Scope` no longer uses `std::vector`
- the remaining work here is to build full dictionary payloads and mapping
  behavior on top of that substrate

The insertion order table should contain:

- `CLValue` key
- `CLValue` value
- `invalidate_array`, an array of code objects that depend on the current value of the variable

We never delete a value, simply set the value to `cl_value_not_present`. We also encode the index into the parent table into the upper 32 bits of this value, or `-1` if not known.

This means that we can look up a global by index alone. If we have to recurse into the parent scope and we don't have the index, pick up the key from the table itself.
