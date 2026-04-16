# Specialized list storage design

## Goal

Preserve the existing `List` object identity and Python-visible semantics, while allowing specialized internal storage layouts such as:

- `List[Any]` using ordinary boxed `Value` storage
- `List[float]` using unboxed `double` storage

The specialization should be entirely an internal representation choice. De-specialization must preserve the identity of the `List` object itself.

## Core idea

The list object should not embed its element storage inline in a way that fixes the representation forever.

Instead, the `List` object owns a pointer to a separate backing-storage object or buffer, plus a storage-kind tag.

That gives the implementation freedom to swap backing storage representations without changing the identity of the `List` object.

### Sketch

typedef enum {
    LIST_STORAGE_ANY,
    LIST_STORAGE_FLOAT,
} ListStorageKind;

typedef struct {
    size_t length;
    size_t capacity;
    ListStorageKind storage_kind;
    void* storage;
    uint64_t version;
} ListObject;

Possible backing layouts:

typedef struct {
    Value items[];
} ListAnyStorage;

typedef struct {
    double items[];
} ListFloatStorage;

## Main invariant

The `ListObject` is the stable Python-visible object.

The backing storage is replaceable.

## Basic behavior

### List[Any]

- elements stored as boxed `Value`
- can contain arbitrary values

### List[float]

- elements stored as unboxed `double`
- fast indexing and append

## Creation strategy

- Start as List[Any], or
- Specialize when all observed values are floats

## Operations

### Read

- load double
- box if needed

### Write / Append

- if float → store directly
- else → de-specialize

## De-specialization

1. Allocate generic storage
2. Box all elements
3. Replace storage pointer
4. Update kind
5. Free old storage

## Alternatives
Value and double take the same amount of storage, so it might be possible to mutate the existing backing storage. 
However, this depends on boxing allocation not failing while the storage is being mutated, so we never leave the list in a half-valid state.

## Transition diagram
```
                    +------------------+
                    |   Empty / Any    |
                    | kind = ANY       |
                    | len = 0          |
                    +------------------+
                      | append(float)
                      v
                    +------------------+
                    |  Float-special   |
      append(float) | kind = FLOAT     | append(float)
     / set float    | all elems float  | / set float
    +-------------->| backing=double[] |--------------+
    |               +------------------+              |
    |                    | append/set non-float       |
    |                    | extend mixed iterable      |
    |                    | any op requiring generic   |
    |                    v                            |
    |               +------------------+              |
    +---------------|   Generic / Any  |<-------------+
     append(any)    | kind = ANY       |  append(any)
     set any        | backing=Value[]  |  set any
                    +------------------+
```


## Concurrency

Treat as structural mutation:
- protect storage pointer and kind
- ensure safe reclamation

## Iterator/versioning

- version stored on ListObject
- increment on mutation or transition

## Memory management

- float storage: free directly
- generic: decref elements

## Advantages

- preserves identity
- enables unboxed floats
- localized complexity

## Costs

- extra complexity in list impl
- conversion overhead


## Summary

Specialize backing storage, not object identity.
