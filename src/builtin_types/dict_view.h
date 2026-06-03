#ifndef CL_DICT_VIEW_H
#define CL_DICT_VIEW_H

#include "builtin_types/dict.h"
#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"

namespace cl
{
    class VirtualMachine;

    class DictKeysView : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::DictKeysView;

        DictKeysView(ClassObject *cls, TValue<Dict> _dict)
            : Object(cls, native_layout), dict(_dict)
        {
        }

        Member<TValue<Dict>> dict;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(DictKeysView, Object, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(DictKeysView);
    };

    class DictValuesView : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::DictValuesView;

        DictValuesView(ClassObject *cls, TValue<Dict> _dict)
            : Object(cls, native_layout), dict(_dict)
        {
        }

        Member<TValue<Dict>> dict;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(DictValuesView, Object, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(DictValuesView);
    };

    class DictItemsView : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::DictItemsView;

        DictItemsView(ClassObject *cls, TValue<Dict> _dict)
            : Object(cls, native_layout), dict(_dict)
        {
        }

        Member<TValue<Dict>> dict;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(DictItemsView, Object, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(DictItemsView);
    };

    class DictKeyIterator : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::DictKeyIterator;

        DictKeyIterator(ClassObject *cls, TValue<Dict> _dict)
            : Object(cls, native_layout), dict(_dict),
              index(TValue<SMI>::from_smi(0)),
              expected_size(TValue<SMI>::from_smi(
                  static_cast<int64_t>(_dict.extract()->size())))
        {
        }

        Member<TValue<Dict>> dict;
        Member<TValue<SMI>> index;
        Member<TValue<SMI>> expected_size;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(DictKeyIterator, Object, 3);
        CL_DECLARE_STATIC_OBJECT_SIZE(DictKeyIterator);
    };

    class DictValueIterator : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::DictValueIterator;

        DictValueIterator(ClassObject *cls, TValue<Dict> _dict)
            : Object(cls, native_layout), dict(_dict),
              index(TValue<SMI>::from_smi(0)),
              expected_size(TValue<SMI>::from_smi(
                  static_cast<int64_t>(_dict.extract()->size())))
        {
        }

        Member<TValue<Dict>> dict;
        Member<TValue<SMI>> index;
        Member<TValue<SMI>> expected_size;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(DictValueIterator, Object, 3);
        CL_DECLARE_STATIC_OBJECT_SIZE(DictValueIterator);
    };

    class DictItemIterator : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::DictItemIterator;

        DictItemIterator(ClassObject *cls, TValue<Dict> _dict)
            : Object(cls, native_layout), dict(_dict),
              index(TValue<SMI>::from_smi(0)),
              expected_size(TValue<SMI>::from_smi(
                  static_cast<int64_t>(_dict.extract()->size())))
        {
        }

        Member<TValue<Dict>> dict;
        Member<TValue<SMI>> index;
        Member<TValue<SMI>> expected_size;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(DictItemIterator, Object, 3);
        CL_DECLARE_STATIC_OBJECT_SIZE(DictItemIterator);
    };

    BuiltinClassDefinition make_dict_keys_view_class(VirtualMachine *vm);
    BuiltinClassDefinition make_dict_values_view_class(VirtualMachine *vm);
    BuiltinClassDefinition make_dict_items_view_class(VirtualMachine *vm);
    BuiltinClassDefinition make_dict_key_iterator_class(VirtualMachine *vm);
    BuiltinClassDefinition make_dict_value_iterator_class(VirtualMachine *vm);
    BuiltinClassDefinition make_dict_item_iterator_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_DICT_VIEW_H
