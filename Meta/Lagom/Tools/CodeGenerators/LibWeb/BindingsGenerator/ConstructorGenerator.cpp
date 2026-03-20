/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ConstructorGenerator.h"
#include "CodeGeneratorUtils.h"
#include "DictionaryGenerator.h"
#include "EnumGenerator.h"
#include "FunctionGenerator.h"
#include "NamespaceGenerator.h"
#include "PrototypeGenerator.h"
#include "TypeConversion.h"

namespace IDL {

ByteString generate_constructor_for_idl_type(Type const& type)
{
    auto append_type_list = [](auto& builder, auto const& type_list) {
        bool first = true;
        for (auto const& child_type : type_list) {
            if (first) {
                first = false;
            } else {
                builder.append(", "sv);
            }

            builder.append(generate_constructor_for_idl_type(child_type));
        }
    };

    switch (type.kind()) {
    case Type::Kind::Plain:
        return ByteString::formatted("make_ref_counted<IDL::Type>(\"{}\", {})", type.name(), type.is_nullable());
    case Type::Kind::Parameterized: {
        auto const& parameterized_type = type.as_parameterized();
        StringBuilder builder;
        builder.appendff("make_ref_counted<IDL::ParameterizedType>(\"{}\", {}, Vector<NonnullRefPtr<IDL::Type const>> {{", type.name(), type.is_nullable());
        append_type_list(builder, parameterized_type.parameters());
        builder.append("})"sv);
        return builder.to_byte_string();
    }
    case Type::Kind::Union: {
        auto const& union_type = type.as_union();
        StringBuilder builder;
        builder.appendff("make_ref_counted<IDL::UnionType>(\"{}\", {}, Vector<NonnullRefPtr<IDL::Type const>> {{", type.name(), type.is_nullable());
        append_type_list(builder, union_type.member_types());
        builder.append("})"sv);
        return builder.to_byte_string();
    }
    }

    VERIFY_NOT_REACHED();
}

// https://webidl.spec.whatwg.org/#dfn-distinguishing-argument-index

void generate_html_constructor(SourceGenerator& generator, IDL::Constructor const& constructor, IDL::Interface const& interface)
{
    auto constructor_generator = generator.fork();
    // NOTE: A HTMLConstrcuctor must not have any parameters.
    constructor_generator.set("constructor.length", "0");

    // https://html.spec.whatwg.org/multipage/dom.html#html-element-constructors
    // NOTE: The active function object in this context is always going to be the current constructor that has just been called.

    // The [HTMLConstructor] extended attribute must take no arguments, and must only appear on constructor operations. It must
    // appear only once on a constructor operation, and the interface must contain only the single, annotated constructor
    // operation, and no others
    if (interface.constructors.size() != 1) {
        dbgln("Interface {}'s constructor annotated with [HTMLConstructor] must be the only constructor", interface.name);
        VERIFY_NOT_REACHED();
    }

    if (!constructor.parameters.is_empty()) {
        dbgln("Interface {}'s constructor marked with [HTMLConstructor] must not have any parameters", interface.name);
        VERIFY_NOT_REACHED();
    }

    constructor_generator.append(R"~~~(
    auto& window = as<HTML::Window>(HTML::current_principal_global_object());

    // 1. Let registry be current global object's custom element registry.
    auto registry = TRY(throw_dom_exception_if_needed(vm, [&] { return window.custom_elements(); }));

    // 2. If NewTarget is equal to the active function object, then throw a TypeError.
    if (&new_target == vm.active_function_object())
        return vm.throw_completion<JS::TypeError>("Cannot directly construct an HTML element, it must be inherited"sv);

    // 3. Let definition be the item in registry's custom element definition set with constructor equal to NewTarget.
    //    If there is no such item, then throw a TypeError.
    auto definition = registry->get_definition_from_new_target(new_target);
    if (!definition)
        return vm.throw_completion<JS::TypeError>("There is no custom element definition assigned to the given constructor"sv);

    // 4. Let isValue be null.
    Optional<String> is_value;

    // 5. If definition's local name is equal to definition's name (i.e., definition is for an autonomous custom element):
    if (definition->local_name() == definition->name()) {
        // 1. If the active function object is not HTMLElement, then throw a TypeError.
)~~~");

    if (interface.name != "HTMLElement") {
        constructor_generator.append(R"~~~(
        return vm.throw_completion<JS::TypeError>("Autonomous custom elements can only inherit from HTMLElement"sv);
)~~~");
    } else {
        constructor_generator.append(R"~~~(
        // Do nothing, as this is the HTMLElement constructor.
)~~~");
    }

    constructor_generator.append(R"~~~(
    }

    // 6. Otherwise (i.e., if definition is for a customized built-in element):
    else {
        // 1. Let valid local names be the list of local names for elements defined in this specification or in other applicable specifications that use the active function object as their element interface.
        auto valid_local_names = MUST(DOM::valid_local_names_for_given_html_element_interface("@name@"sv));

        // 2. If valid local names does not contain definition's local name, then throw a TypeError.
        if (!valid_local_names.contains_slow(definition->local_name()))
            return vm.throw_completion<JS::TypeError>(MUST(String::formatted("Local name '{}' of customized built-in element is not a valid local name for @name@", definition->local_name())));

        // 3. Set isValue to definition's name.
        is_value = definition->name();
    }

    // 7. If definition's construction stack is empty:
    if (definition->construction_stack().is_empty()) {
        // 1. Let element be the result of internally creating a new object implementing the interface to which the active function object corresponds, given the current Realm Record and NewTarget.
        // 2. Set element's node document to the current global object's associated Document.
        // 3. Set element's namespace to the HTML namespace.
        // 4. Set element's namespace prefix to null.
        // 5. Set element's local name to definition's local name.
        auto element = realm.create<@fully_qualified_name@>(window.associated_document(), DOM::QualifiedName { definition->local_name(), {}, Namespace::HTML });

        // https://webidl.spec.whatwg.org/#internally-create-a-new-object-implementing-the-interface
        // Important steps from "internally create a new object implementing the interface"
        {
            // 3.2: Let prototype be ? Get(newTarget, "prototype").
            auto prototype = TRY(new_target.get(vm.names.prototype));

            // 3.3. If Type(prototype) is not an Object, then:
            if (!prototype.is_object()) {
                // 1. Let targetRealm be ? GetFunctionRealm(newTarget).
                auto* target_realm = TRY(JS::get_function_realm(vm, new_target));

                // 2. Set prototype to the interface prototype object for interface in targetRealm.
                VERIFY(target_realm);
                prototype = &Bindings::ensure_web_prototype<@prototype_class@>(*target_realm, "@name@"_fly_string);
            }

            // 9. Set instance.[[Prototype]] to prototype.
            VERIFY(prototype.is_object());
            MUST(element->internal_set_prototype_of(&prototype.as_object()));
        }

        // 6. Set element's custom element state to "custom".
        // 7. Set element's custom element definition to definition.
        // 8. Set element's is value to isValue.
        element->setup_custom_element_from_constructor(*definition, is_value);

        // 9. Return element.
        return *element;
    }

    // 8. Let prototype be ? Get(NewTarget, "prototype").
    auto prototype = TRY(new_target.get(vm.names.prototype));

    // 9. If Type(prototype) is not Object, then:
    if (!prototype.is_object()) {
        // 1. Let realm be ? GetFunctionRealm(NewTarget).
        auto* function_realm = TRY(JS::get_function_realm(vm, new_target));

        // 2. Set prototype to the interface prototype object of realm whose interface is the same as the interface of the active function object.
        VERIFY(function_realm);
        prototype = &Bindings::ensure_web_prototype<@prototype_class@>(*function_realm, "@name@"_fly_string);
    }

    VERIFY(prototype.is_object());

    // 10. Let element be the last entry in definition's construction stack.
    auto& element = definition->construction_stack().last();

    // 11. If element is an already constructed marker, then throw a TypeError.
    if (element.has<HTML::AlreadyConstructedCustomElementMarker>())
        return vm.throw_completion<JS::TypeError>("Custom element has already been constructed"sv);

    // 12. Perform ? element.[[SetPrototypeOf]](prototype).
    auto actual_element = element.get<GC::Ref<DOM::Element>>();
    TRY(actual_element->internal_set_prototype_of(&prototype.as_object()));

    // 13. Replace the last entry in definition's construction stack with an already constructed marker.
    definition->construction_stack().last() = HTML::AlreadyConstructedCustomElementMarker {};

    // 14. Return element.
    return *actual_element;
}
)~~~");
}

void generate_constructor(SourceGenerator& generator, IDL::Constructor const& constructor, IDL::Interface const& interface, bool is_html_constructor)
{
    auto constructor_generator = generator.fork();
    constructor_generator.set("constructor_class", interface.constructor_class);
    constructor_generator.set("interface_fully_qualified_name", interface.fully_qualified_name);
    constructor_generator.set("overload_suffix", constructor.is_overloaded ? ByteString::number(constructor.overload_index) : ByteString::empty());

    constructor_generator.append(R"~~~(
JS::ThrowCompletionOr<GC::Ref<JS::Object>> @constructor_class@::construct@overload_suffix@([[maybe_unused]] FunctionObject& new_target)
{
    WebIDL::log_trace(vm(), "@constructor_class@::construct@overload_suffix@");
)~~~");

    generator.append(R"~~~(
    auto& vm = this->vm();
    auto& realm = *vm.current_realm();
)~~~");

    if (is_html_constructor) {
        generate_html_constructor(generator, constructor, interface);
    } else {
        generator.append(R"~~~(
    // To internally create a new object implementing the interface @name@:

    // 3.2. Let prototype be ? Get(newTarget, "prototype").
    auto prototype = TRY(new_target.get(vm.names.prototype));

    // 3.3. If Type(prototype) is not Object, then:
    if (!prototype.is_object()) {
        // 1. Let targetRealm be ? GetFunctionRealm(newTarget).
        auto* target_realm = TRY(JS::get_function_realm(vm, new_target));

        // 2. Set prototype to the interface prototype object for interface in targetRealm.
        VERIFY(target_realm);
        prototype = &Bindings::ensure_web_prototype<@prototype_class@>(*target_realm, "@namespaced_name@"_fly_string);
    }

    // 4. Let instance be MakeBasicObject( « [[Prototype]], [[Extensible]], [[Realm]], [[PrimaryInterface]] »).
    // 5. Set instance.[[Realm]] to realm.
    // 6. Set instance.[[PrimaryInterface]] to interface.
)~~~");
        if (!constructor.parameters.is_empty()) {
            generate_argument_count_check(generator, constructor.name, constructor.shortest_length());

            StringBuilder arguments_builder;
            generate_arguments(generator, constructor.parameters, arguments_builder, interface);
            constructor_generator.set(".constructor_arguments", arguments_builder.string_view());

            constructor_generator.append(R"~~~(
    auto impl = TRY(throw_dom_exception_if_needed(vm, [&] { return @fully_qualified_name@::construct_impl(realm, @.constructor_arguments@); }));
)~~~");
        } else {
            constructor_generator.append(R"~~~(
    auto impl = TRY(throw_dom_exception_if_needed(vm, [&] { return @fully_qualified_name@::construct_impl(realm); }));
)~~~");
        }

        constructor_generator.append(R"~~~(
    // 7. Set instance.[[Prototype]] to prototype.
    VERIFY(prototype.is_object());
    impl->set_prototype(&prototype.as_object());

    // FIXME: Steps 8...11. of the "internally create a new object implementing the interface @name@" algorithm
    // (https://webidl.spec.whatwg.org/#js-platform-objects) are currently not handled, or are handled within @fully_qualified_name@::construct_impl().
    //  8. Let interfaces be the inclusive inherited interfaces of interface.
    //  9. For every interface ancestor interface in interfaces:
    //    9.1. Let unforgeables be the value of the [[Unforgeables]] slot of the interface object of ancestor interface in realm.
    //    9.2. Let keys be ! unforgeables.[[OwnPropertyKeys]]().
    //    9.3. For each element key of keys:
    //      9.3.1. Let descriptor be ! unforgeables.[[GetOwnProperty]](key).
    //      9.3.2. Perform ! DefinePropertyOrThrow(instance, key, descriptor).
    //  10. If interface is declared with the [Global] extended attribute, then:
    //    10.1. Define the regular operations of interface on instance, given realm.
    //    10.2. Define the regular attributes of interface on instance, given realm.
    //    10.3. Define the iteration methods of interface on instance given realm.
    //    10.4. Define the asynchronous iteration methods of interface on instance given realm.
    //    10.5. Define the global property references on instance, given realm.
    //    10.6. Set instance.[[SetPrototypeOf]] as defined in § 3.8.1 [[SetPrototypeOf]].
    //  11. Otherwise, if interfaces contains an interface which supports indexed properties, named properties, or both:
    //    11.1. Set instance.[[GetOwnProperty]] as defined in § 3.9.1 [[GetOwnProperty]].
    //    11.2. Set instance.[[Set]] as defined in § 3.9.2 [[Set]].
    //    11.3. Set instance.[[DefineOwnProperty]] as defined in § 3.9.3 [[DefineOwnProperty]].
    //    11.4. Set instance.[[Delete]] as defined in § 3.9.4 [[Delete]].
    //    11.5. Set instance.[[PreventExtensions]] as defined in § 3.9.5 [[PreventExtensions]].
    //    11.6. Set instance.[[OwnPropertyKeys]] as defined in § 3.9.6 [[OwnPropertyKeys]].

    return *impl;
}
)~~~");
    }
}

void generate_constructors(SourceGenerator& generator, IDL::Interface const& interface)
{
    auto shortest_length = interface.constructors.is_empty() ? 0u : NumericLimits<size_t>::max();
    bool has_html_constructor = false;
    for (auto const& constructor : interface.constructors) {
        shortest_length = min(shortest_length, constructor.shortest_length());

        if (constructor.extended_attributes.contains("HTMLConstructor"sv)) {
            has_html_constructor = true;
            break;
        }
    }

    if (has_html_constructor && interface.constructors.size() != 1) {
        dbgln("Interface {}'s constructor annotated with [HTMLConstructor] must be the only constructor", interface.name);
        VERIFY_NOT_REACHED();
    }

    generator.set("constructor.length", ByteString::number(shortest_length));

    // Implementation: Constructors
    if (interface.constructors.is_empty()) {
        // No constructor
        generator.append(R"~~~(
JS::ThrowCompletionOr<GC::Ref<JS::Object>> @constructor_class@::construct([[maybe_unused]] FunctionObject& new_target)
{
    WebIDL::log_trace(vm(), "@constructor_class@::construct");
)~~~");
        generator.append(R"~~~(
    return vm().throw_completion<JS::TypeError>(JS::ErrorType::NotAConstructor, "@namespaced_name@");
}
)~~~");
    } else {
        for (auto& constructor : interface.constructors) {
            generate_constructor(generator, constructor, interface, has_html_constructor);
        }
    }
    for (auto const& overload_set : interface.constructor_overload_sets) {
        if (overload_set.value.size() == 1)
            continue;
        generate_overload_arbiter(generator, overload_set, interface, interface.constructor_class, IsConstructor::Yes);
    }
}

void generate_constructor_header(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("constructor_class", interface.constructor_class);

    generator.append(R"~~~(
#pragma once

#include <LibJS/Runtime/NativeFunction.h>

namespace Web::Bindings {

class @constructor_class@ : public JS::NativeFunction {
    JS_OBJECT(@constructor_class@, JS::NativeFunction);
    GC_DECLARE_ALLOCATOR(@constructor_class@);
public:
    explicit @constructor_class@(JS::Realm&);
    virtual void initialize(JS::Realm&) override;
    virtual ~@constructor_class@() override;

    virtual JS::ThrowCompletionOr<JS::Value> call() override;
)~~~");

    if (!interface.is_callback_interface) {
        generator.append(R"~~~(
    virtual JS::ThrowCompletionOr<GC::Ref<JS::Object>> construct(JS::FunctionObject& new_target) override;

private:
    virtual bool has_constructor() const override { return true; }
)~~~");
    }

    for (auto& attribute : interface.static_attributes) {
        auto attribute_generator = generator.fork();
        attribute_generator.set("attribute.name:snakecase", attribute.name.to_snakecase());
        attribute_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(@attribute.name:snakecase@_getter);
)~~~");

        if (!attribute.readonly) {
            attribute_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(@attribute.name:snakecase@_setter);
)~~~");
        }
    }

    for (auto const& overload_set : interface.constructor_overload_sets) {
        auto constructor_generator = generator.fork();
        if (overload_set.value.size() > 1) {
            for (auto i = 0u; i < overload_set.value.size(); ++i) {
                constructor_generator.set("overload_suffix", ByteString::number(i));
                constructor_generator.append(R"~~~(
    JS::ThrowCompletionOr<GC::Ref<JS::Object>> construct@overload_suffix@(JS::FunctionObject& new_target);
)~~~");
            }
        }
    }

    for (auto const& overload_set : interface.static_overload_sets) {
        auto function_generator = generator.fork();
        function_generator.set("function.name:snakecase", make_input_acceptable_cpp(overload_set.key.to_snakecase()));
        function_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(@function.name:snakecase@);
)~~~");
        if (overload_set.value.size() > 1) {
            for (auto i = 0u; i < overload_set.value.size(); ++i) {
                function_generator.set("overload_suffix", ByteString::number(i));
                function_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(@function.name:snakecase@@overload_suffix@);
)~~~");
            }
        }
    }

    generator.append(R"~~~(
};

} // namespace Web::Bindings
)~~~");
}

void generate_constructor_implementation(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("name", interface.name);
    generator.set("namespaced_name", interface.namespaced_name);
    generator.set("prototype_class", interface.prototype_class);
    generator.set("constructor_class", interface.constructor_class);
    generator.set("fully_qualified_name", interface.fully_qualified_name);
    generator.set("parent_name", interface.parent_name);
    generator.set("prototype_base_class", interface.prototype_base_class);
    generator.set("constructor.length", "0");

    generator.append(R"~~~(
#include <LibIDL/Types.h>
#include <LibGC/Heap.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/DataView.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Iterator.h>
#include <LibJS/Runtime/PromiseConstructor.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/Bindings/@constructor_class@.h>
#include <LibWeb/Bindings/@prototype_class@.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/WindowProxy.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/Buffers.h>
#include <LibWeb/WebIDL/CallbackType.h>
#include <LibWeb/WebIDL/OverloadResolution.h>
#include <LibWeb/WebIDL/Tracing.h>
#include <LibWeb/WebIDL/Types.h>

#if __has_include(<LibWeb/Bindings/@prototype_base_class@.h>)
#    include <LibWeb/Bindings/@prototype_base_class@.h>
#endif

)~~~");

    if (interface.constructors.size() == 1) {
        auto& constructor = interface.constructors[0];
        if (constructor.extended_attributes.contains("HTMLConstructor"sv)) {
            generator.append(R"~~~(
#include <LibJS/Runtime/AbstractOperations.h>
#include <LibWeb/DOM/ElementFactory.h>
#include <LibWeb/HTML/CustomElements/CustomElementRegistry.h>
#include <LibWeb/HTML/CustomElements/CustomElementDefinition.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/Namespace.h>
)~~~");
        }
    }

    emit_includes_for_all_imports(interface, generator, interface.pair_iterator_types.has_value(), interface.async_value_iterator_type.has_value());

    generate_using_namespace_definitions(generator);

    generator.append(R"~~~(
namespace Web::Bindings {

GC_DEFINE_ALLOCATOR(@constructor_class@);

@constructor_class@::@constructor_class@(JS::Realm& realm)
    : NativeFunction("@name@"_utf16_fly_string, realm.intrinsics().function_prototype())
{
}

@constructor_class@::~@constructor_class@()
{
}

JS::ThrowCompletionOr<JS::Value> @constructor_class@::call()
{
    return vm().throw_completion<JS::TypeError>(JS::ErrorType::ConstructorWithoutNew, "@namespaced_name@");
}

)~~~");

    if (!interface.is_callback_interface)
        generate_constructors(generator, interface);

    generator.append(R"~~~(

void @constructor_class@::initialize(JS::Realm& realm)
{
    auto& vm = this->vm();
    [[maybe_unused]] u8 default_attributes = JS::Attribute::Enumerable;

    Base::initialize(realm);
)~~~");

    if (!interface.is_callback_interface && interface.prototype_base_class != "ObjectPrototype") {
        generator.append(R"~~~(
    set_prototype(&ensure_web_constructor<@prototype_base_class@>(realm, "@parent_name@"_fly_string));
)~~~");
    }

    generator.append(R"~~~(
    define_direct_property(vm.names.length, JS::Value(@constructor.length@), JS::Attribute::Configurable);
    define_direct_property(vm.names.name, JS::PrimitiveString::create(vm, "@name@"_string), JS::Attribute::Configurable);
)~~~");

    if (!interface.is_callback_interface) {
        generator.append(R"~~~(
    define_direct_property(vm.names.prototype, &ensure_web_prototype<@prototype_class@>(realm, "@namespaced_name@"_fly_string), 0);
)~~~");
    }

    for (auto& constant : interface.constants) {
        auto constant_generator = generator.fork();
        constant_generator.set("constant.name", constant.name);

        generate_wrap_statement(constant_generator, constant.value, constant.type, interface, ByteString::formatted("auto constant_{}_value =", constant.name));

        constant_generator.append(R"~~~(
    define_direct_property("@constant.name@"_utf16_fly_string, constant_@constant.name@_value, JS::Attribute::Enumerable);
)~~~");
    }

    for (auto& attribute : interface.static_attributes) {
        auto attribute_generator = generator.fork();
        attribute_generator.set("attribute.name", attribute.name);
        attribute_generator.set("attribute.getter_callback", attribute.getter_callback_name);
        attribute_generator.set("attribute.getter_callback", attribute.getter_callback_name);

        if (!attribute.readonly)
            attribute_generator.set("attribute.setter_callback", attribute.setter_callback_name);
        else
            attribute_generator.set("attribute.setter_callback", "nullptr");

        attribute_generator.append(R"~~~(
    define_native_accessor(realm, "@attribute.name@"_utf16_fly_string, @attribute.getter_callback@, @attribute.setter_callback@, default_attributes);
)~~~");
    }

    define_the_operations(generator, interface.static_overload_sets);

    generator.append(R"~~~(
}
)~~~");

    // Implementation: Static Attributes
    for (auto& attribute : interface.static_attributes) {
        auto attribute_generator = generator.fork();
        attribute_generator.set("attribute.name", attribute.name);
        attribute_generator.set("attribute.getter_callback", attribute.getter_callback_name);
        attribute_generator.set("attribute.setter_callback", attribute.setter_callback_name);

        if (attribute.extended_attributes.contains("ImplementedAs")) {
            auto implemented_as = attribute.extended_attributes.get("ImplementedAs").value();
            attribute_generator.set("attribute.cpp_name", implemented_as);
        } else {
            attribute_generator.set("attribute.cpp_name", attribute.name.to_snakecase());
        }

        attribute_generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@constructor_class@::@attribute.getter_callback@)
{
    WebIDL::log_trace(vm, "@constructor_class@::@attribute.getter_callback@");
    auto retval = TRY(throw_dom_exception_if_needed(vm, [&] { return @fully_qualified_name@::@attribute.cpp_name@(vm); }));
)~~~");

        generate_return_statement(generator, *attribute.type, interface);

        attribute_generator.append(R"~~~(
}
)~~~");

        // FIXME: Add support for static attribute setters.
    }

    // Implementation: Static Functions
    for (auto& function : interface.static_functions) {
        if (function.extended_attributes.contains("FIXME"))
            continue;
        generate_function(generator, function, StaticFunction::Yes, interface.constructor_class, interface.fully_qualified_name, interface);
    }
    for (auto const& overload_set : interface.static_overload_sets) {
        if (overload_set.value.size() == 1)
            continue;
        generate_overload_arbiter(generator, overload_set, interface, interface.constructor_class, IsConstructor::No);
    }

    generator.append(R"~~~(
} // namespace Web::Bindings
)~~~");
}

}
