# Trusted Handler Declarations

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented; handler metadata, VM registration, typed resolver installation, float unary and binary type-adapted handler families, and opaque JIT calls for eligible unary, binary, and ternary cached handlers use this design |
| Scope | Authoritative declaration, registration, and resolution of trusted native handlers |
| Owning layers | Builtin implementations own handler bodies and declarations; the VM owns the registry; code objects retain resolver function pointers; the JIT consumes registered metadata |
| Validated against | `ad1c5054` (2026-08-07) |
| Supersedes | N/A |
| Related documents | [JIT Compiler and IR](jit-compiler-and-ir.md), [AArch64 JIT Calling Convention](aarch64-jit-calling-convention.md), and [Function Specialization](function-specialization.md) |

## Objective

Every trusted handler needs one authoritative declaration containing its typed
function target, effects, and semantic identity. That declaration must drive
both VM registration and resolver results. Resolver execution must not lock or
query the registry, and a code object must continue to retain an ordinary
resolver function pointer rather than per-resolver runtime state.

The declaration mechanism must establish these invariants:

- handler arity follows from the C++ function type;
- effects are declared beside the handler body or semantic operator;
- known semantics identify the semantic operation independently of the guarded
  operand domains recorded by the inline cache;
- a resolver can return only handlers in its declared sequence;
- installing a resolver registers its complete sequence before publishing the
  resolver function pointer;
- the same native target may be shared by several resolvers;
- conflicting declarations for one target are fatal;
- JIT compilation may query metadata without adding work to runtime
  resolution.

## Effects

Trusted-handler effects describe the body that will actually execute. They are
independent facts. In particular:

```text
Allocate does not imply Safepoint.
Safepoint does not imply Allocate.
```

The initial vocabulary needs to distinguish at least allocation, safepoints,
raising, and calls into Python. A safepoint permits reclamation; reclamation does
not need a separate capability bit. No safepoint implication should be encoded
merely as a consequence of declaring `Allocate`.

Effects describe execution under the trusted-entry preconditions established
by the resolver result and reproduced as call-site guards. They do not describe
what an unchecked C++ call with arbitrary `Value` arguments might do. This
distinction matters for handlers whose implementation retains a defensive
general fallback: the fallback is outside the trusted invocation contract when
the resolver requires guards that make it unreachable.

Effects remain attached to each concrete native target. One resolver may select
handlers with materially different effects, and two adaptations of one semantic
operation may differ if an adapter itself adds behavior. Reusing effects from a
semantic operator is valid only when every participating adapter adds no effects
of its own.

The first direct JIT call accepts only handlers proven not to raise, safepoint,
or call Python. That eligibility is derived from registered effects rather than
stored as an independent allow-list bit. Precise optimization effects may extend
the metadata later without changing handler identity or resolver installation.

The initial registry metadata is therefore a call-boundary capability summary,
not a complete memory-effect model. Trusted calls retain the conservative JIT
effect envelope: handlers may read or mutate Python-visible state unless later
metadata proves otherwise.

## Semantic Identity

`TrustedHandlerSemantics` identifies the operation performed by a trusted
target:

```cpp
enum class TrustedHandlerSemantics
{
    Generic,

    Add,
    Sub,
    RSub,
    Mul,
    TrueDiv,
    RTrueDiv,
    FloorDiv,
    RFloorDiv,
    Mod,
    RMod,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Neg,
    Pos,
};
```

`Generic` declares no recognized semantics. It is the default for handlers that
remain opaque native calls.

Every other enumerator names one type-independent semantic operation. Normal and
reflected operations are distinct only when their mapping from canonical
arguments differs: `Add` can cover equivalent normal and reflected addition,
while subtraction requires both `Sub` and `RSub`. The operand domains are not
duplicated into this enum. Inline-cache shape keys distinguish integer addition,
float/float addition, and mixed float/SMI-or-Boolean addition.

The inline cache remains the source of guards proving that a cached target is
applicable and of operand-domain facts needed to choose conversions. Semantic
identity tells the JIT which operation the already-selected target performs. A
recognized lowering combines the operation with the guarded shape keys and
accepts only combinations for which it has an exact expansion.

Semantic identity is registered with the concrete handler rather than derived
from the bytecode opcode. The opcode names the protocol operation that initiated
dispatch; normal/reflected search may select a target with different canonical
operand behavior. Ordinary function specializations may also have no operator
opcode from which to recover identity.

Semantic identity neither implies effects nor replaces them. Two targets may
share effects while having different semantics, and a known semantic lowering
must preserve the registered effect and exception contract.

## Handler Definition

A handler definition is a class template over a typed function pointer, its
effects, and its semantic identity:

```cpp
template<auto Target, TrustedHandlerEffects Effects,
         TrustedHandlerSemantics Semantics = TrustedHandlerSemantics::Generic>
class TrustedHandlerDefinition
{
public:
    static_assert(is_trusted_handler_function_v<decltype(Target)>);

    static constexpr auto target = Target;
    static constexpr TrustedHandlerArity arity =
        trusted_handler_arity(Target);
    static constexpr TrustedHandlerEffects effects = Effects;
    static constexpr TrustedHandlerSemantics semantics = Semantics;

    static void register_with(VirtualMachine &vm)
    {
        vm.register_trusted_handler(Target, Effects, Semantics);
    }

    static TrustedResolution resolution()
    {
        return TrustedResolution::call_registered(Target);
    }
};
```

`Target` may be a namespace function, static function, or concrete function
template specialization. C++17 already permits function pointers as non-type
template parameters and permits `auto` non-type parameters; C++20 additionally
allows concepts to constrain the accepted signatures.

The accepted signatures are exactly the existing unary, binary, and ternary
trusted-handler ABIs. `trusted_handler_arity()` derives arity from the pointer
type. Callers do not supply arity separately.

The raw `TrustedResolution::call_trusted(function_pointer)` construction path
is retired after migration. Resolver code obtains a successful resolution only
through a declared handler type. This makes membership in the resolver's
sequence visible in its source.

## Resolver Sequence

The common resolver base is templated on its complete ordered handler sequence:

```cpp
template<typename... HandlerDefinitions>
class TrustedHandlerResolverBase
{
protected:
    template<size_t Index>
    using Handler = std::tuple_element_t<
        Index, std::tuple<HandlerDefinitions...>>;

public:
    static void register_handlers(VirtualMachine &vm)
    {
        (HandlerDefinitions::register_with(vm), ...);
    }
};
```

A concrete resolver assigns semantic names to sequence positions and implements
resolution as a static method. The names make selection readable without
repeating targets or metadata:

```cpp
using NormalFloatFloat = Handler<0>;
using NormalFloatSMIOrBool = Handler<1>;
using NormalSMIOrBoolFloat = Handler<2>;
```

Positional aliases are deliberately local to the concrete resolver. Reordering
the declaration sequence requires updating those aliases, but the function
target and effects remain written only once.

## Resolver Installation

Resolver installation is templated on the concrete resolver type:

```cpp
template<typename Resolver>
BuiltinIntrinsicMethod with_trusted_handler_resolver(
    VirtualMachine *vm, BuiltinIntrinsicMethod method)
{
    static_assert(std::is_same_v<decltype(&Resolver::resolve),
                                 TrustedHandlerResolver>);

    Resolver::register_handlers(*vm);
    method.trusted_handler_resolver = &Resolver::resolve;
    return method;
}
```

Registration therefore occurs during builtin installation. Runtime resolution
remains one indirect call through the function pointer already stored on the
code object. It performs no registry lookup, mutex acquisition, context load,
or handler registration.

Direct setup APIs accepting arbitrary `TrustedHandlerResolver` pointers should
be removed after migration. Custom function builders such as the dictionary
builders use the same operation through a code-object overload:

```cpp
template<typename Resolver>
void install_trusted_handler_resolver(VirtualMachine &vm,
                                      CodeObject &code_object)
{
    static_assert(std::is_same_v<decltype(&Resolver::resolve),
                                 TrustedHandlerResolver>);

    Resolver::register_handlers(vm);
    code_object.trusted_handler_resolver = &Resolver::resolve;
}
```

The builtin-method helper delegates to this primitive or performs the same
operation while constructing its method descriptor. Neither path accepts an
arbitrary resolver pointer.

## VM Registry

The VM registry is keyed only by the erased native target. Arity and effects are
metadata:

```cpp
struct TrustedHandlerMetadata
{
    TrustedHandlerArity arity;
    TrustedHandlerEffects effects;
    TrustedHandlerSemantics semantics;
};
```

Typed `register_trusted_handler()` overloads infer arity, erase the target, lock
the registry, and insert its metadata. Registering the same target with
identical metadata is idempotent. Registering it with different arity or
effects or semantics is an invariant failure.

Registry reads and writes take the registry mutex. Reads return metadata by
value rather than returning a map entry after releasing the lock. Resolver
execution does not read the registry. JIT compilation reads it when inspecting
a cached trusted target, where mutex cost is outside the runtime operator path.

Shared mixed-type handlers need no special treatment. If two installed
resolvers declare the same target with identical metadata, both installation
paths converge on the same registry entry.

## General And Trusted Adapters

Some builtin families can generate their checked general handler and their
trusted handlers from one semantic operation. This composition is encouraged
where it removes duplicated semantics, but it is not a requirement of handler
declaration or registration.

The dependency runs from shared semantics to both adapter kinds:

```text
semantic operation
    -> general adapter: checks, conversion, errors, NotImplemented
    -> trusted adapters: assume resolver-established guards, then convert
```

A general adapter cannot be reconstructed from a trusted function pointer. The
pointer does not encode accepted operand classes, receiver-error behavior,
`NotImplemented` behavior, reflected ordering, or argument conversion policy.
`TrustedHandlerDefinition` consequently remains the universal declaration of
one concrete trusted target. An optional family-specific layer may expose both
the general adapter and several declared trusted adapters.

Float binary operators are the initial example. List operations may select
several trusted targets from one general operation, while dictionary general
handlers are generated bytecode rather than C++ adapter templates. Neither must
be forced through the float-family abstraction.

## Float Operation And Handler Families

This pattern is a **type-adapted dunder method**. It is one category of dunder
implementation, not a universal representation for all dunder methods. One
free function represents the underlying operation over its natural C++ types.
The native method, trusted handlers, and resolver provide different adaptations
from Python `Value` arguments to those types:

- the native method checks and converts dynamically;
- each trusted handler performs one statically selected conversion;
- the resolver selects a trusted adaptation from the observed argument shapes.

All of those entries execute the same underlying operation. Effects and
registered semantics still belong to each concrete trusted handler because the
adaptation itself can change the contract. The float binary machinery below is
the first concrete type-adapted dunder family; it is not intended to impose the
float conversion rules on other builtin types.

The reusable operation is therefore an ordinary function over converted C++
values. It contains no receiver check, operand-shape policy, effects, or
registered semantics:

```cpp
static Value float_not_equal(ThreadState *, double left, double right)
{
    return left != right ? Value::True() : Value::False();
}
```

The checked general entry also needs its receiver error. Raw string literals
cannot directly serve as pointer-valued template arguments with the required
identity, so a C++20 structural fixed string carries that text:

```cpp
template<size_t Size>
struct FixedWideString
{
    wchar_t value[Size];

    consteval FixedWideString(const wchar_t (&source)[Size])
    {
        for(size_t index = 0; index < Size; ++index)
        {
            value[index] = source[index];
        }
    }

    constexpr const wchar_t *c_str() const { return value; }
};
```

`FloatBinaryOperation` binds the semantic function to that checked-path error
and generates the native entry. It does not own trusted-handler effects or
semantics:

```cpp
using FloatBinaryFunction = Value (*)(ThreadState *, double, double);

template<typename Operation, TrustedHandlerEffects Effects,
         TrustedHandlerSemantics Semantics>
struct UniformFloatBinaryHandlers;

template<FloatBinaryFunction Function, FixedWideString ReceiverError>
struct FloatBinaryOperation
{
    using Self = FloatBinaryOperation<Function, ReceiverError>;

    static constexpr auto function = Function;

    static Value native(ThreadState *thread, Value self, Value other)
    {
        if(!can_convert_to<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", ReceiverError.c_str());
        }

        double right;
        if(!try_get_float_or_smi_or_bool(other, &right))
        {
            return Value::NotImplemented();
        }
        return Function(thread, self.get_ptr<Float>()->value, right);
    }

    template<TrustedHandlerEffects Effects,
             TrustedHandlerSemantics Semantics>
    using Handlers =
        UniformFloatBinaryHandlers<Self, Effects, Semantics>;
};
```

The three trusted operand adaptations share one enum-driven target template:

```cpp
enum class FloatBinaryAdaptation
{
    FloatFloat,
    FloatIntlike,
    IntlikeFloat,
};

template<FloatBinaryFunction Function, FloatBinaryAdaptation Adaptation>
static Value trusted_float_binary_operator(
    ThreadState *thread, Value left, Value right)
{
    if constexpr(Adaptation == FloatBinaryAdaptation::FloatFloat)
    {
        return Function(thread, left.get_ptr<Float>()->value,
                        right.get_ptr<Float>()->value);
    }
    else if constexpr(Adaptation == FloatBinaryAdaptation::FloatIntlike)
    {
        return Function(thread, left.get_ptr<Float>()->value,
                        smi_or_bool_as_double(right));
    }
    else
    {
        return Function(thread, smi_or_bool_as_double(left),
                        right.get_ptr<Float>()->value);
    }
}
```

Each generated adaptation remains a distinct concrete handler definition:

```cpp
template<typename Operation, FloatBinaryAdaptation Adaptation,
         TrustedHandlerEffects Effects,
         TrustedHandlerSemantics Semantics>
struct FloatBinaryHandler
    : TrustedHandlerDefinition<
          trusted_float_binary_operator<Operation::function, Adaptation>,
          Effects, Semantics>
{};

template<typename Operation, TrustedHandlerEffects Effects,
         TrustedHandlerSemantics Semantics>
struct UniformFloatBinaryHandlers
{
    using FloatFloat =
        FloatBinaryHandler<Operation, FloatBinaryAdaptation::FloatFloat,
                           Effects, Semantics>;
    using FloatIntlike =
        FloatBinaryHandler<Operation, FloatBinaryAdaptation::FloatIntlike,
                           Effects, Semantics>;
    using IntlikeFloat =
        FloatBinaryHandler<Operation, FloatBinaryAdaptation::IntlikeFloat,
                           Effects, Semantics>;
};
```

The uniform set is a float-specific convenience asserting that all three
adaptations have been reviewed and share one contract. The effects and
semantics are still instantiated onto each concrete handler and registered per
native target. Families such as list indexing must instead declare separate
handlers when their concrete operand adaptations have different effects.

The binary resolver consumes one normal and one reflected handler set. Its base
registers all six concrete targets, while shape selection returns the matching
named handler's resolution:

```cpp
template<typename NormalHandlers, typename ReflectedHandlers>
class FloatBinaryResolver final
    : public TrustedHandlerResolverBase<
          typename NormalHandlers::FloatFloat,
          typename NormalHandlers::FloatIntlike,
          typename NormalHandlers::IntlikeFloat,
          typename ReflectedHandlers::FloatFloat,
          typename ReflectedHandlers::FloatIntlike,
          typename ReflectedHandlers::IntlikeFloat>
{
    // Handler<0..2> are normal; Handler<3..5> are reflected.
public:
    static TrustedResolution resolve(
        VirtualMachine *, ShapeKey, ShapeKey,
        TrustedHandlerOperandOrder, TrustedHandlerArity);
};
```

A complete declaration and installation is compact:

```cpp
using FloatNe = FloatBinaryOperation<
    float_not_equal,
    L"float.__ne__ expects a float receiver">;
using FloatNeHandlers = FloatNe::Handlers<
    TrustedHandlerEffects::None,
    TrustedHandlerSemantics::NotEqual>;

with_trusted_handler_resolver<
    FloatBinaryResolver<FloatNeHandlers, FloatNeHandlers>>(
        vm,
        builtin_intrinsic_method(
            L"__ne__", FloatNe::native,
            L"Return self != value."));
```

Unary operations use the same structure with `FloatUnaryOperation`, one trusted
adaptation, and one concrete handler. The operation descriptor generates the
checked native entry; effects and semantics remain arguments to its nested
handler alias.

## Current JIT Consumer

The bytecode-to-Core frontend consumes warmed unary, binary, and ternary
operator-cache entries without rerunning their resolver. It obtains the cached
concrete target, queries registry metadata by that target, and requires the
registered arity to match the bytecode operation. The current native-call
boundary accepts only handlers that do not declare `Safepoint`, `Raise`, or
`CallPython`; `Allocate` alone is permitted because allocation and safepointing
are independent effects.

Eligible lowering emits one pre-operation Snapshot, then the cached shape and
lookup-validity guards in cache-match order. Unary calls guard operand zero;
binary and ternary calls guard operands zero and one because the third ternary
argument is runtime call state rather than a cache key. Inline shape keys become
exact tag guards, object keys become exact shape guards, and non-null lookup
validity cells become `ValidityCellGuard`s.

`TrustedHandlerCall` receives the guarded values in the interpreter cache's
canonical operand order. Reflected selection and operand adaptation are already
encoded by the concrete target, so the JIT does not swap operands or repeat
resolution. Immediate binary bytecodes remain on the interpreter path until
their embedded constant can be represented as an ordinary call argument.
Unregistered or ineligible targets likewise exit before the operation and
replay the original bytecode.

## Recognized JIT Expansion

The current JIT consumer emits an opaque `TrustedHandlerCall` after checking
registered effects. Known semantics additionally permit a later Core pass to
replace that call with the exact guarded operation sequence. For example,
`Add` plus float/SMI-or-Boolean inline-cache shapes expands to a float
unbox, inline-integer conversion, floating-point addition, and result boxing.

This enables a deliberate optimization chain:

```text
registered known semantics
    -> explicit guards, conversions, operation, and boxing
    -> tagged-value facts eliminate redundant guards
    -> unbox(box(value)) simplifies to value
    -> boxes used only by snapshots become side-exit recovery actions
    -> dead mainline boxes are eliminated
```

Snapshots may already capture program values of non-tagged representations, so
recovery can box those values on the side-exit path. Boxing remains on the main
path where a normal return, opaque call, store, or other tagged consumer requires
it.

Semantic expansion consumes both sources of information without conflating
them: registered semantics selects the operation, while IC-derived guards select
and prove the operand adaptation. `Generic` handlers, unsupported shape
combinations, and known handlers whose expansion is not yet implemented remain
ordinary native calls.
