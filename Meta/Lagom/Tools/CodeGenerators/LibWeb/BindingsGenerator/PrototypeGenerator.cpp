/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PrototypeGenerator.h"
#include "CodeGeneratorUtils.h"
#include "ConstructorGenerator.h"
#include "DictionaryGenerator.h"
#include "EnumGenerator.h"
#include "FunctionGenerator.h"
#include "NamespaceGenerator.h"
#include "TypeConversion.h"

namespace IDL {

void generate_prototype_or_global_mixin_declarations(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    for (auto const& overload_set : interface.overload_sets) {
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

    if (interface.has_stringifier) {
        auto stringifier_generator = generator.fork();
        stringifier_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(to_string);
        )~~~");
    }

    if (interface.pair_iterator_types.has_value()) {
        auto iterator_generator = generator.fork();
        iterator_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(entries);
    JS_DECLARE_NATIVE_FUNCTION(for_each);
    JS_DECLARE_NATIVE_FUNCTION(keys);
    JS_DECLARE_NATIVE_FUNCTION(values);
        )~~~");
    }

    if (interface.async_value_iterator_type.has_value()) {
        auto iterator_generator = generator.fork();
        iterator_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(values);
        )~~~");
    }

    if (interface.set_entry_type.has_value()) {
        auto setlike_generator = generator.fork();

        setlike_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(get_size);
    JS_DECLARE_NATIVE_FUNCTION(entries);
    JS_DECLARE_NATIVE_FUNCTION(values);
    JS_DECLARE_NATIVE_FUNCTION(for_each);
    JS_DECLARE_NATIVE_FUNCTION(has);
)~~~");
        if (!interface.overload_sets.contains("add"sv) && !interface.is_set_readonly) {
            setlike_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(add);
)~~~");
        }
        if (!interface.overload_sets.contains("delete"sv) && !interface.is_set_readonly) {
            setlike_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(delete_);
)~~~");
        }
        if (!interface.overload_sets.contains("clear"sv) && !interface.is_set_readonly) {
            setlike_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(clear);
)~~~");
        }
    }

    if (interface.map_key_type.has_value()) {
        auto maplike_generator = generator.fork();

        maplike_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(get_size);
    JS_DECLARE_NATIVE_FUNCTION(entries);
    JS_DECLARE_NATIVE_FUNCTION(keys);
    JS_DECLARE_NATIVE_FUNCTION(values);
    JS_DECLARE_NATIVE_FUNCTION(for_each);
    JS_DECLARE_NATIVE_FUNCTION(get);
    JS_DECLARE_NATIVE_FUNCTION(has);
)~~~");

        if (!interface.overload_sets.contains("set"sv) && !interface.is_map_readonly)
            maplike_generator.appendln("    JS_DECLARE_NATIVE_FUNCTION(set);");

        if (!interface.overload_sets.contains("delete"sv) && !interface.is_map_readonly)
            maplike_generator.appendln("    JS_DECLARE_NATIVE_FUNCTION(delete_);");

        if (!interface.overload_sets.contains("clear"sv) && !interface.is_map_readonly)
            maplike_generator.appendln("    JS_DECLARE_NATIVE_FUNCTION(clear);");
    }

    for (auto& attribute : interface.attributes) {
        if (attribute.extended_attributes.contains("FIXME"))
            continue;
        auto attribute_generator = generator.fork();
        attribute_generator.set("attribute.getter_callback", attribute.getter_callback_name);
        attribute_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(@attribute.getter_callback@);
)~~~");

        if (!attribute.readonly || attribute.extended_attributes.contains("Replaceable"sv) || attribute.extended_attributes.contains("PutForwards"sv) || attribute.extended_attributes.contains("LegacyLenientSetter")) {
            attribute_generator.set("attribute.setter_callback", attribute.setter_callback_name);
            attribute_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(@attribute.setter_callback@);
)~~~");
        }
    }

    generator.append(R"~~~(

};

)~~~");

    generate_enumerations(interface.enumerations, builder);
}

// https://webidl.spec.whatwg.org/#create-an-inheritance-stack
Vector<Interface const&> create_an_inheritance_stack(IDL::Interface const& start_interface)
{
    // 1. Let stack be a new stack.
    Vector<Interface const&> inheritance_chain;

    // 2. Push I onto stack.
    inheritance_chain.append(start_interface);

    // 3. While I inherits from an interface,
    auto const* current_interface = &start_interface;
    while (current_interface && !current_interface->parent_name.is_empty()) {
        // 1. Let I be that interface.
        auto imported_interface_iterator = start_interface.imported_modules.find_if([&current_interface](IDL::Interface const& imported_interface) {
            return imported_interface.name == current_interface->parent_name;
        });

        // Inherited interfaces must have their IDL files imported.
        VERIFY(imported_interface_iterator != start_interface.imported_modules.end());

        // 2. Push I onto stack.
        inheritance_chain.append(*imported_interface_iterator);

        current_interface = &*imported_interface_iterator;
    }

    // 4. Return stack.
    return inheritance_chain;
}

// https://webidl.spec.whatwg.org/#collect-attribute-values-of-an-inheritance-stack
void collect_attribute_values_of_an_inheritance_stack(SourceGenerator& function_generator, Vector<Interface const&> const& inheritance_chain)
{
    // 1. Let I be the result of popping from stack.
    // 3. If stack is not empty, then invoke collect attribute values of an inheritance stack given object, stack, and map.
    for (auto const& interface_in_chain : inheritance_chain.in_reverse()) {
        // 2. Invoke collect attribute values given object, I, and map.
        // https://webidl.spec.whatwg.org/#collect-attribute-values
        // 1. If a toJSON operation with a [Default] extended attribute is declared on I, then for each exposed regular attribute attr that is an interface member of I, in order:
        auto to_json_iterator = interface_in_chain.functions.find_if([](IDL::Function const& function) {
            return function.name == "toJSON" && function.extended_attributes.contains("Default");
        });

        if (to_json_iterator == interface_in_chain.functions.end())
            continue;

        // FIXME: Check if the attributes are exposed.

        // NOTE: Add more specified exposed global interface groups when needed.
        StringBuilder window_exposed_only_members_builder;
        SourceGenerator window_exposed_only_members_generator { window_exposed_only_members_builder, function_generator.clone_mapping() };
        auto generator_for_member = [&](auto const& name, auto& extended_attributes) -> SourceGenerator {
            if (auto maybe_exposed = extended_attributes.get("Exposed"); maybe_exposed.has_value()) {
                auto exposed_to = MUST(IDL::parse_exposure_set(name, *maybe_exposed));
                if (exposed_to == IDL::ExposedTo::Window) {
                    return window_exposed_only_members_generator.fork();
                }
            }
            return function_generator.fork();
        };

        // 1. Let id be the identifier of attr.
        // 2. Let value be the result of running the getter steps of attr with object as this.

        // 3. If value is a JSON type, then set map[id] to value.
        // Since we are flatly generating the attributes, the consequent is replaced with these steps from "default toJSON steps":
        // 5. For each key → value of map,
        //    1. Let k be key converted to an ECMAScript value.
        //    2. Let v be value converted to an ECMAScript value.
        //    3. Perform ! CreateDataProperty(result, k, v).

        // NOTE: Functions, constructors and static functions cannot be JSON types, so they're not checked here.

        for (auto& attribute : interface_in_chain.attributes) {
            if (attribute.extended_attributes.contains("FIXME"))
                continue;
            if (!attribute.type->is_json(interface_in_chain))
                continue;

            auto attribute_generator = generator_for_member(attribute.name, attribute.extended_attributes);
            auto return_value_name = ByteString::formatted("{}_retval", attribute.name.to_snakecase());

            attribute_generator.set("attribute.name", attribute.name);
            attribute_generator.set("attribute.return_value_name", return_value_name);

            if (attribute.extended_attributes.contains("ImplementedAs")) {
                auto implemented_as = attribute.extended_attributes.get("ImplementedAs").value();
                attribute_generator.set("attribute.cpp_name", implemented_as);
            } else {
                attribute_generator.set("attribute.cpp_name", make_input_acceptable_cpp(attribute.name.to_snakecase()));
            }

            if (attribute.extended_attributes.contains("Reflect")) {
                auto attribute_name = attribute.extended_attributes.get("Reflect").value();
                if (attribute_name.is_empty())
                    attribute_name = attribute.name;

                attribute_generator.set("attribute.reflect_name", attribute_name);
            } else {
                attribute_generator.set("attribute.reflect_name", attribute.name.to_snakecase());
            }

            if (attribute.extended_attributes.contains("Reflect")) {
                if (attribute.type->name() != "boolean") {
                    attribute_generator.append(R"~~~(
    auto @attribute.return_value_name@ = impl->get_attribute_value("@attribute.reflect_name@"_fly_string);
)~~~");
                } else {
                    attribute_generator.append(R"~~~(
    auto @attribute.return_value_name@ = impl->has_attribute("@attribute.reflect_name@"_fly_string);
)~~~");
                }
            } else {
                attribute_generator.append(R"~~~(
    auto @attribute.return_value_name@ = TRY(throw_dom_exception_if_needed(vm, [&] { return impl->@attribute.cpp_name@(); }));
)~~~");
            }

            attribute_generator.append(R"~~~(
    JS::Value @attribute.return_value_name@_wrapped;
)~~~");
            generate_wrap_statement(attribute_generator, return_value_name, attribute.type, interface_in_chain, ByteString::formatted("{}_wrapped =", return_value_name));

            attribute_generator.append(R"~~~(
    MUST(result->create_data_property("@attribute.name@"_utf16_fly_string, @attribute.return_value_name@_wrapped));
)~~~");
        }

        for (auto& constant : interface_in_chain.constants) {
            auto constant_generator = function_generator.fork();
            constant_generator.set("constant.name", constant.name);

            generate_wrap_statement(constant_generator, constant.value, constant.type, interface_in_chain, ByteString::formatted("auto constant_{}_value =", constant.name));

            constant_generator.append(R"~~~(
    MUST(result->create_data_property("@constant.name@"_utf16_fly_string, constant_@constant.name@_value));
)~~~");
        }

        if (!window_exposed_only_members_generator.as_string_view().is_empty()) {
            auto window_only_property_declarations = function_generator.fork();
            window_only_property_declarations.set("defines", window_exposed_only_members_generator.as_string_view());
            window_only_property_declarations.append(R"~~~(
    if (is<HTML::Window>(realm.global_object())) {
@defines@
    }
)~~~");
        }
    }
}

// https://webidl.spec.whatwg.org/#default-tojson-steps
void generate_default_to_json_function(SourceGenerator& generator, ByteString const& class_name, IDL::Interface const& start_interface)
{
    // NOTE: This is done heavily out of order since the spec mixes parse time and run time type information together.

    auto function_generator = generator.fork();
    function_generator.set("class_name", class_name);

    // 4. Let result be OrdinaryObjectCreate(%Object.prototype%).
    function_generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@class_name@::to_json)
{
    WebIDL::log_trace(vm, "@class_name@::to_json");
    auto& realm = *vm.current_realm();
    auto* impl = TRY(impl_from(vm));

    auto result = JS::Object::create(realm, realm.intrinsics().object_prototype());
)~~~");

    // 1. Let map be a new ordered map.
    // NOTE: Instead of making a map, we flatly generate the attributes.

    // 2. Let stack be the result of creating an inheritance stack for interface I.
    auto inheritance_chain = create_an_inheritance_stack(start_interface);

    // 3. Invoke collect attribute values of an inheritance stack given this, stack, and map.
    collect_attribute_values_of_an_inheritance_stack(function_generator, inheritance_chain);

    // NOTE: Step 5 is done as part of collect_attribute_values_of_an_inheritance_stack, due to us flatly generating the attributes.

    // 6. Return result.
    function_generator.append(R"~~~(
    return result;
}
)~~~");
}

void generate_named_properties_object_declarations(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("named_properties_class", ByteString::formatted("{}Properties", interface.name));

    generator.append(R"~~~(
class @named_properties_class@ : public JS::Object {
    JS_OBJECT(@named_properties_class@, JS::Object);
    GC_DECLARE_ALLOCATOR(@named_properties_class@);
public:
    explicit @named_properties_class@(JS::Realm&);
    virtual void initialize(JS::Realm&) override;
    virtual ~@named_properties_class@() override;

    JS::Realm& realm() const { return m_realm; }
private:
    virtual JS::ThrowCompletionOr<Optional<JS::PropertyDescriptor>> internal_get_own_property(JS::PropertyKey const&) const override;
    virtual JS::ThrowCompletionOr<bool> internal_define_own_property(JS::PropertyKey const&, JS::PropertyDescriptor&, Optional<JS::PropertyDescriptor>* precomputed_get_own_property = nullptr) override;
    virtual JS::ThrowCompletionOr<bool> internal_delete(JS::PropertyKey const&) override;
    virtual JS::ThrowCompletionOr<bool> internal_set_prototype_of(JS::Object* prototype) override;
    virtual JS::ThrowCompletionOr<bool> internal_prevent_extensions() override;

    virtual bool eligible_for_own_property_enumeration_fast_path() const override final { return false; }

    virtual void visit_edges(Visitor&) override;

    GC::Ref<JS::Realm> m_realm; // [[Realm]]
};
)~~~");
}

void generate_named_properties_object_definitions(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("name", interface.name);
    generator.set("parent_name", interface.parent_name);
    generator.set("prototype_base_class", interface.prototype_base_class);
    generator.set("named_properties_class", ByteString::formatted("{}Properties", interface.name));

    // https://webidl.spec.whatwg.org/#create-a-named-properties-object
    generator.append(R"~~~(
#include <LibWeb/WebIDL/AbstractOperations.h>

GC_DEFINE_ALLOCATOR(@named_properties_class@);

@named_properties_class@::@named_properties_class@(JS::Realm& realm)
  : JS::Object(realm, nullptr, MayInterfereWithIndexedPropertyAccess::Yes)
  , m_realm(realm)
{
}

@named_properties_class@::~@named_properties_class@()
{
}

void @named_properties_class@::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    auto& vm = realm.vm();

    // The class string of a named properties object is the concatenation of the interface's identifier and the string "Properties".
    define_direct_property(vm.well_known_symbol_to_string_tag(), JS::PrimitiveString::create(vm, "@named_properties_class@"_string), JS::Attribute::Configurable);
)~~~");

    // 1. Let proto be null
    // 2. If interface is declared to inherit from another interface, then set proto to the interface prototype object in realm for the inherited interface.
    // 3. Otherwise, set proto to realm.[[Intrinsics]].[[%Object.prototype%]].
    // NOTE: Steps 4-9 handled by constructor + other overridden functions
    // 10. Set obj.[[Prototype]] to proto.
    if (interface.prototype_base_class == "ObjectPrototype") {
        generator.append(R"~~~(

    set_prototype(realm.intrinsics().object_prototype());
)~~~");
    } else {
        generator.append(R"~~~(

    set_prototype(&ensure_web_prototype<@prototype_base_class@>(realm, "@parent_name@"_fly_string));
)~~~");
    }

    generator.append(R"~~~(
};

// https://webidl.spec.whatwg.org/#named-properties-object-getownproperty
JS::ThrowCompletionOr<Optional<JS::PropertyDescriptor>> @named_properties_class@::internal_get_own_property(JS::PropertyKey const& property_name) const
{
    auto& realm = this->realm();

    // 1. Let A be the interface for the named properties object O.
    using A = @name@;

    // 2. Let object be O.[[Realm]]'s global object.
    // 3. Assert: object implements A.
    auto& object = as<A>(realm.global_object());

    // 4. If the result of running the named property visibility algorithm with property name P and object object is true, then:
    if (TRY(object.is_named_property_exposed_on_object(property_name))) {
        auto property_name_string = property_name.to_string().to_utf8_but_should_be_ported_to_utf16();

        // 1. Let operation be the operation used to declare the named property getter.
        // 2. Let value be an uninitialized variable.
        // 3. If operation was defined without an identifier, then set value to the result of performing the steps listed in the interface description to determine the value of a named property with P as the name.
        // 4. Otherwise, operation was defined with an identifier. Set value to the result of performing the method steps of operation with « P » as the only argument value.
        auto value = object.named_item_value(property_name_string);

        // 5. Let desc be a newly created Property Descriptor with no fields.
        JS::PropertyDescriptor descriptor;

        // 6. Set desc.[[Value]] to the result of converting value to an ECMAScript value.
        descriptor.value = value;
)~~~");
    if (interface.extended_attributes.contains("LegacyUnenumerableNamedProperties"))
        generator.append(R"~~~(
        // 7. If A implements an interface with the [LegacyUnenumerableNamedProperties] extended attribute, then set desc.[[Enumerable]] to false, otherwise set it to true.
        descriptor.enumerable = true;
)~~~");
    else {
        generator.append(R"~~~(
        // 7. If A implements an interface with the [LegacyUnenumerableNamedProperties] extended attribute, then set desc.[[Enumerable]] to false, otherwise set it to true.
        descriptor.enumerable = false;
)~~~");
    }
    generator.append(R"~~~(
        // 8. Set desc.[[Writable]] to true and desc.[[Configurable]] to true.
        descriptor.writable = true;
        descriptor.configurable = true;

        // 9. Return desc.
        return descriptor;
    }

    // 5. Return OrdinaryGetOwnProperty(O, P).
    return JS::Object::internal_get_own_property(property_name);
}

// https://webidl.spec.whatwg.org/#named-properties-object-defineownproperty
JS::ThrowCompletionOr<bool> @named_properties_class@::internal_define_own_property(JS::PropertyKey const&, JS::PropertyDescriptor&, Optional<JS::PropertyDescriptor>*)
{
    // 1. Return false.
    return false;
}

// https://webidl.spec.whatwg.org/#named-properties-object-delete
JS::ThrowCompletionOr<bool> @named_properties_class@::internal_delete(JS::PropertyKey const&)
{
    // 1. Return false.
    return false;
}

// https://webidl.spec.whatwg.org/#named-properties-object-setprototypeof
JS::ThrowCompletionOr<bool> @named_properties_class@::internal_set_prototype_of(JS::Object* prototype)
{
    // 1. Return ? SetImmutablePrototype(O, V).
    return set_immutable_prototype(prototype);
}

// https://webidl.spec.whatwg.org/#named-properties-object-preventextensions
JS::ThrowCompletionOr<bool> @named_properties_class@::internal_prevent_extensions()
{
    // 1. Return false.
    // Note: this keeps named properties object extensible by making [[PreventExtensions]] fail.
    return false;
}

void @named_properties_class@::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_realm);
}
)~~~");
}

void generate_prototype_or_global_mixin_initialization(IDL::Interface const& interface, StringBuilder& builder, GenerateUnforgeables generate_unforgeables)
{
    SourceGenerator generator { builder };

    auto is_global_interface = interface.extended_attributes.contains("Global");
    auto class_name = is_global_interface ? interface.global_mixin_class : interface.prototype_class;
    generator.set("name", interface.name);
    generator.set("namespaced_name", interface.namespaced_name);
    generator.set("class_name", class_name);
    generator.set("fully_qualified_name", interface.fully_qualified_name);
    generator.set("parent_name", interface.parent_name);
    generator.set("prototype_base_class", interface.prototype_base_class);
    generator.set("prototype_name", interface.prototype_class); // Used for Global Mixin

    if (interface.pair_iterator_types.has_value()) {
        generator.set("iterator_name", ByteString::formatted("{}Iterator", interface.name));
    }

    bool define_on_existing_object = is_global_interface || generate_unforgeables == GenerateUnforgeables::Yes;

    if (define_on_existing_object) {
        generator.set("define_direct_accessor", "object.define_direct_accessor");
        generator.set("define_direct_property", "object.define_direct_property");
        generator.set("define_native_accessor", "object.define_native_accessor");
        generator.set("define_native_function", "object.define_native_function");
        generator.set("set_prototype", "object.set_prototype");
    } else {
        generator.set("define_direct_accessor", "define_direct_accessor");
        generator.set("define_direct_property", "define_direct_property");
        generator.set("define_native_accessor", "define_native_accessor");
        generator.set("define_native_function", "define_native_function");
        generator.set("set_prototype", "set_prototype");
    }

    if (generate_unforgeables == GenerateUnforgeables::Yes) {
        generator.append(R"~~~(
void @class_name@::define_unforgeable_attributes(JS::Realm& realm, [[maybe_unused]] JS::Object& object)
{
)~~~");
    } else if (is_global_interface) {
        generator.append(R"~~~(
void @class_name@::initialize(JS::Realm& realm, JS::Object& object)
{
)~~~");
    } else {
        generator.append(R"~~~(
void @class_name@::initialize(JS::Realm& realm)
{
)~~~");
    }

    generator.append(R"~~~(

    [[maybe_unused]] auto& vm = realm.vm();

)~~~");

    // FIXME: Currently almost everything gets default_attributes but it should be configurable per attribute.
    //        See the spec links for details
    if (generate_unforgeables == GenerateUnforgeables::No) {
        generator.append(R"~~~(
    [[maybe_unused]] u8 default_attributes = JS::Attribute::Enumerable | JS::Attribute::Configurable | JS::Attribute::Writable;
)~~~");
    } else {
        generator.append(R"~~~(
    [[maybe_unused]] u8 default_attributes = JS::Attribute::Enumerable;
)~~~");
    }

    if (generate_unforgeables == GenerateUnforgeables::No) {
        if (interface.name == "DOMException"sv) {
            generator.append(R"~~~(

    @set_prototype@(realm.intrinsics().error_prototype());
)~~~");
        }

        else if (interface.prototype_base_class == "ObjectPrototype") {
            generator.append(R"~~~(

    @set_prototype@(realm.intrinsics().object_prototype());

)~~~");
        } else if (is_global_interface) {
            generator.append(R"~~~(
    @set_prototype@(&ensure_web_prototype<@prototype_name@>(realm, "@name@"_fly_string));
)~~~");
        } else {
            generator.append(R"~~~(

    @set_prototype@(&ensure_web_prototype<@prototype_base_class@>(realm, "@parent_name@"_fly_string));

)~~~");
        }
    }

    if (interface.has_unscopable_member) {
        generator.append(R"~~~(
    auto unscopable_object = JS::Object::create(realm, nullptr);
)~~~");
    }

    // NOTE: Add more specified exposed global interface groups when needed.
    StringBuilder window_exposed_only_members_builder;
    SourceGenerator window_exposed_only_members_generator { window_exposed_only_members_builder, generator.clone_mapping() };
    auto generator_for_member = [&](auto const& name, auto& extended_attributes) -> SourceGenerator {
        if (auto maybe_exposed = extended_attributes.get("Exposed"); maybe_exposed.has_value()) {
            auto exposed_to = MUST(IDL::parse_exposure_set(name, *maybe_exposed));
            if (exposed_to == IDL::ExposedTo::Window) {
                return window_exposed_only_members_generator.fork();
            }
        }
        return generator.fork();
    };

    // https://webidl.spec.whatwg.org/#es-attributes
    for (auto& attribute : interface.attributes) {
        bool has_unforgeable_attribute = attribute.extended_attributes.contains("LegacyUnforgeable"sv);
        if ((generate_unforgeables == GenerateUnforgeables::Yes && !has_unforgeable_attribute) || (generate_unforgeables == GenerateUnforgeables::No && has_unforgeable_attribute))
            continue;

        auto attribute_generator = generator_for_member(attribute.name, attribute.extended_attributes);

        // AD-HOC: Do not expose experimental attributes unless instructed to do so.
        if (attribute.extended_attributes.contains("Experimental")) {
            attribute_generator.append(R"~~~(
    if (HTML::UniversalGlobalScopeMixin::expose_experimental_interfaces()) {)~~~");
        }

        if (attribute.extended_attributes.contains("SecureContext")) {
            attribute_generator.append(R"~~~(
    if (HTML::is_secure_context(Bindings::principal_host_defined_environment_settings_object(realm))) {)~~~");
        }

        if (attribute.extended_attributes.contains("FIXME")) {
            attribute_generator.set("attribute.name", attribute.name);
            attribute_generator.append(R"~~~(
    @define_direct_property@("@attribute.name@"_utf16_fly_string, JS::js_undefined(), default_attributes | JS::Attribute::Unimplemented);
            )~~~");
            if (attribute.extended_attributes.contains("SecureContext")) {
                attribute_generator.append(R"~~~(
    })~~~");
            }
            continue;
        }

        attribute_generator.set("attribute.name", attribute.name);
        attribute_generator.set("attribute.getter_callback", attribute.getter_callback_name);
        attribute_generator.set("attribute.setter_callback", attribute.setter_callback_name);

        if (has_unforgeable_attribute) {
            attribute_generator.append(R"~~~(
    auto native_@attribute.getter_callback@ = host_defined_intrinsics(realm).ensure_web_unforgeable_function("@namespaced_name@"_utf16_fly_string, "@attribute.name@"_utf16_fly_string, @attribute.getter_callback@, UnforgeableKey::Type::Getter);
)~~~");
        } else {
            attribute_generator.append(R"~~~(
    auto native_@attribute.getter_callback@ = JS::NativeFunction::create(realm, @attribute.getter_callback@, 0, "@attribute.name@"_utf16_fly_string, &realm, "get"sv);
)~~~");
        }

        if (!attribute.readonly || attribute.extended_attributes.contains("Replaceable"sv) || attribute.extended_attributes.contains("PutForwards"sv) || attribute.extended_attributes.contains("LegacyLenientSetter")) {
            if (has_unforgeable_attribute) {
                attribute_generator.append(R"~~~(
    auto native_@attribute.setter_callback@ = host_defined_intrinsics(realm).ensure_web_unforgeable_function("@namespaced_name@"_utf16_fly_string, "@attribute.name@"_utf16_fly_string, @attribute.setter_callback@, UnforgeableKey::Type::Setter);
)~~~");
            } else {
                attribute_generator.append(R"~~~(
    auto native_@attribute.setter_callback@ = JS::NativeFunction::create(realm, @attribute.setter_callback@, 1, "@attribute.name@"_utf16_fly_string, &realm, "set"sv);
)~~~");
            }
        } else {
            attribute_generator.append(R"~~~(
    GC::Ptr<JS::NativeFunction> native_@attribute.setter_callback@;
)~~~");
        }

        if (attribute.extended_attributes.contains("Unscopable")) {
            attribute_generator.append(R"~~~(
    MUST(unscopable_object->create_data_property("@attribute.name@"_utf16_fly_string, JS::Value(true)));
)~~~");
        }

        attribute_generator.append(R"~~~(
    @define_direct_accessor@("@attribute.name@"_utf16_fly_string, native_@attribute.getter_callback@, native_@attribute.setter_callback@, default_attributes);
)~~~");

        if (attribute.extended_attributes.contains("SecureContext")) {
            attribute_generator.append(R"~~~(
    })~~~");
        }

        if (attribute.extended_attributes.contains("Experimental")) {
            attribute_generator.append(R"~~~(
    })~~~");
        }
    }

    for (auto& function : interface.functions) {
        bool has_unforgeable_attribute = function.extended_attributes.contains("LegacyUnforgeable"sv);
        if ((generate_unforgeables == GenerateUnforgeables::Yes && !has_unforgeable_attribute) || (generate_unforgeables == GenerateUnforgeables::No && has_unforgeable_attribute))
            continue;

        if (function.extended_attributes.contains("FIXME")) {
            auto function_generator = generator_for_member(function.name, function.extended_attributes);
            function_generator.set("function.name", function.name);
            function_generator.append(R"~~~(
        @define_direct_property@("@function.name@"_utf16_fly_string, JS::js_undefined(), default_attributes | JS::Attribute::Unimplemented);
            )~~~");
        }
    }

    // https://webidl.spec.whatwg.org/#es-constants
    if (generate_unforgeables == GenerateUnforgeables::No) {
        for (auto& constant : interface.constants) {
            // FIXME: Do constants need to be added to the unscopable list?

            auto constant_generator = generator.fork();
            constant_generator.set("constant.name", constant.name);

            generate_wrap_statement(constant_generator, constant.value, constant.type, interface, ByteString::formatted("auto constant_{}_value =", constant.name));

            constant_generator.append(R"~~~(
    @define_direct_property@("@constant.name@"_utf16_fly_string, constant_@constant.name@_value, JS::Attribute::Enumerable);
)~~~");
        }
    }

    // https://webidl.spec.whatwg.org/#es-operations
    for (auto const& overload_set : interface.overload_sets) {
        // NOTE: This assumes that every function in the overload set has the same attribute set.
        bool has_unforgeable_attribute = any_of(overload_set.value, [](auto const& function) { return function.extended_attributes.contains("LegacyUnforgeable"); });
        if ((generate_unforgeables == GenerateUnforgeables::Yes && !has_unforgeable_attribute) || (generate_unforgeables == GenerateUnforgeables::No && has_unforgeable_attribute))
            continue;

        auto const& function = overload_set.value.first();
        auto function_generator = generator_for_member(function.name, function.extended_attributes);
        function_generator.set("function.name", overload_set.key);
        function_generator.set("function.name:snakecase", make_input_acceptable_cpp(overload_set.key.to_snakecase()));
        function_generator.set("function.length", ByteString::number(get_shortest_function_length(overload_set.value)));

        if (function.extended_attributes.contains("SecureContext")) {
            function_generator.append(R"~~~(
    if (HTML::is_secure_context(Bindings::principal_host_defined_environment_settings_object(realm))) {)~~~");
        }

        if (any_of(overload_set.value, [](auto const& function) { return function.extended_attributes.contains("Unscopable"); })) {
            VERIFY(all_of(overload_set.value, [](auto const& function) { return function.extended_attributes.contains("Unscopable"); }));
            function_generator.append(R"~~~(
    MUST(unscopable_object->create_data_property("@function.name@"_utf16_fly_string, JS::Value(true)));
)~~~");
        }

        function_generator.append(R"~~~(
    @define_native_function@(realm, "@function.name@"_utf16_fly_string, @function.name:snakecase@, @function.length@, default_attributes);
)~~~");

        if (function.extended_attributes.contains("SecureContext")) {
            function_generator.append(R"~~~(
    })~~~");
        }
    }

    bool should_generate_stringifier = true;
    if (interface.stringifier_attribute.has_value()) {
        bool has_unforgeable_attribute = interface.stringifier_attribute.value().extended_attributes.contains("LegacyUnforgeable"sv);
        if ((generate_unforgeables == GenerateUnforgeables::Yes && !has_unforgeable_attribute) || (generate_unforgeables == GenerateUnforgeables::No && has_unforgeable_attribute))
            should_generate_stringifier = false;
    }
    if (interface.has_stringifier && should_generate_stringifier) {
        // FIXME: Do stringifiers need to be added to the unscopable list?
        auto stringifier_generator = interface.stringifier_extended_attributes.has_value()
            ? generator_for_member("stringifier"sv, *interface.stringifier_extended_attributes)
            : generator.fork();
        stringifier_generator.append(R"~~~(
    @define_native_function@(realm, "toString"_utf16_fly_string, to_string, 0, default_attributes);
)~~~");
    }

    // https://webidl.spec.whatwg.org/#define-the-iteration-methods
    // This applies to this if block and the following if block.
    if (interface.indexed_property_getter.has_value() && generate_unforgeables == GenerateUnforgeables::No) {
        auto iterator_generator = generator.fork();
        iterator_generator.append(R"~~~(
    @define_direct_property@(vm.well_known_symbol_iterator(), realm.intrinsics().array_prototype()->get_without_side_effects(vm.names.values), JS::Attribute::Configurable | JS::Attribute::Writable);
)~~~");

        if (interface.value_iterator_type.has_value()) {
            iterator_generator.append(R"~~~(
    @define_direct_property@(vm.names.entries, realm.intrinsics().array_prototype()->get_without_side_effects(vm.names.entries), default_attributes);
    @define_direct_property@(vm.names.keys, realm.intrinsics().array_prototype()->get_without_side_effects(vm.names.keys), default_attributes);
    @define_direct_property@(vm.names.values, realm.intrinsics().array_prototype()->get_without_side_effects(vm.names.values), default_attributes);
    @define_direct_property@(vm.names.forEach, realm.intrinsics().array_prototype()->get_without_side_effects(vm.names.forEach), default_attributes);
)~~~");
        }
    }

    if (interface.pair_iterator_types.has_value() && generate_unforgeables == GenerateUnforgeables::No) {
        // FIXME: Do pair iterators need to be added to the unscopable list?

        auto iterator_generator = generator.fork();
        iterator_generator.append(R"~~~(
    @define_native_function@(realm, vm.names.entries, entries, 0, default_attributes);
    @define_native_function@(realm, vm.names.forEach, for_each, 1, default_attributes);
    @define_native_function@(realm, vm.names.keys, keys, 0, default_attributes);
    @define_native_function@(realm, vm.names.values, values, 0, default_attributes);

    @define_direct_property@(vm.well_known_symbol_iterator(), get_without_side_effects(vm.names.entries), JS::Attribute::Configurable | JS::Attribute::Writable);
)~~~");
    }

    // https://webidl.spec.whatwg.org/#define-the-asynchronous-iteration-methods
    if (interface.async_value_iterator_type.has_value() && generate_unforgeables == GenerateUnforgeables::No) {
        auto iterator_generator = generator.fork();
        iterator_generator.append(R"~~~(
    @define_native_function@(realm, vm.names.values, values, 0, default_attributes);

    @define_direct_property@(vm.well_known_symbol_async_iterator(), get_without_side_effects(vm.names.values), JS::Attribute::Configurable | JS::Attribute::Writable);
)~~~");
    }

    // https://webidl.spec.whatwg.org/#js-setlike
    if (interface.set_entry_type.has_value() && generate_unforgeables == GenerateUnforgeables::No) {

        auto setlike_generator = generator.fork();

        setlike_generator.append(R"~~~(
    @define_native_accessor@(realm, vm.names.size, get_size, nullptr, JS::Attribute::Enumerable | JS::Attribute::Configurable);
    @define_native_function@(realm, vm.names.entries, entries, 0, default_attributes);
    // NOTE: Keys intentionally returns values for setlike
    @define_native_function@(realm, vm.names.keys, values, 0, default_attributes);
    @define_native_function@(realm, vm.names.values, values, 0, default_attributes);
    @define_direct_property@(vm.well_known_symbol_iterator(), get_without_side_effects(vm.names.values), JS::Attribute::Configurable | JS::Attribute::Writable);
    @define_native_function@(realm, vm.names.forEach, for_each, 1, default_attributes);
    @define_native_function@(realm, vm.names.has, has, 1, default_attributes);
)~~~");

        if (!interface.overload_sets.contains("add"sv) && !interface.is_set_readonly) {
            setlike_generator.append(R"~~~(
    @define_native_function@(realm, vm.names.add, add, 1, default_attributes);
)~~~");
        }
        if (!interface.overload_sets.contains("delete"sv) && !interface.is_set_readonly) {
            setlike_generator.append(R"~~~(
    @define_native_function@(realm, vm.names.delete_, delete_, 1, default_attributes);
)~~~");
        }
        if (!interface.overload_sets.contains("clear"sv) && !interface.is_set_readonly) {
            setlike_generator.append(R"~~~(
    @define_native_function@(realm, vm.names.clear, clear, 0, default_attributes);
)~~~");
        }
    }

    if (interface.map_key_type.has_value() && generate_unforgeables == GenerateUnforgeables::No) {
        auto maplike_generator = generator.fork();

        maplike_generator.append(R"~~~(
    @define_native_accessor@(realm, vm.names.size, get_size, nullptr, JS::Attribute::Enumerable | JS::Attribute::Configurable);
    @define_native_function@(realm, vm.names.entries, entries, 0, default_attributes);
    @define_direct_property@(vm.well_known_symbol_iterator(), get_without_side_effects(vm.names.entries), JS::Attribute::Configurable | JS::Attribute::Writable);
    @define_native_function@(realm, vm.names.keys, keys, 0, default_attributes);
    @define_native_function@(realm, vm.names.values, values, 0, default_attributes);
    @define_native_function@(realm, vm.names.forEach, for_each, 1, default_attributes);
    @define_native_function@(realm, vm.names.get, get, 1, default_attributes);
    @define_native_function@(realm, vm.names.has, has, 1, default_attributes);
)~~~");

        if (!interface.overload_sets.contains("set"sv) && !interface.is_map_readonly)
            maplike_generator.appendln("    @define_native_function@(realm, vm.names.set, set, 2, default_attributes);");

        if (!interface.overload_sets.contains("delete"sv) && !interface.is_map_readonly)
            maplike_generator.appendln("    @define_native_function@(realm, vm.names.delete_, delete_, 1, default_attributes);");

        if (!interface.overload_sets.contains("clear"sv) && !interface.is_map_readonly)
            maplike_generator.appendln("    @define_native_function@(realm, vm.names.clear, clear, 0, default_attributes);");
    }

    if (interface.has_unscopable_member) {
        generator.append(R"~~~(
    @define_direct_property@(vm.well_known_symbol_unscopables(), unscopable_object, JS::Attribute::Configurable);
)~~~");
    }

    if (generate_unforgeables == GenerateUnforgeables::No) {
        generator.append(R"~~~(
    @define_direct_property@(vm.well_known_symbol_to_string_tag(), JS::PrimitiveString::create(vm, "@namespaced_name@"_string), JS::Attribute::Configurable);
)~~~");
    }

    if (!window_exposed_only_members_generator.as_string_view().is_empty()) {
        auto window_only_property_declarations = generator.fork();
        window_only_property_declarations.set("defines", window_exposed_only_members_generator.as_string_view());
        window_only_property_declarations.append(R"~~~(
    if (is<HTML::Window>(realm.global_object())) {
@defines@
    }
)~~~");
    }

    if (!define_on_existing_object) {
        generator.append(R"~~~(
    Base::initialize(realm);
)~~~");
    }

    generator.append(R"~~~(
}
)~~~");
}

// https://webidl.spec.whatwg.org/#dfn-attribute-setter
void generate_attribute_setter(SourceGenerator& attribute_generator, IDL::Attribute const& attribute, IDL::Interface const& interface)
{
    attribute_generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@class_name@::@attribute.setter_callback@)
{
    WebIDL::log_trace(vm, "@class_name@::@attribute.setter_callback@");
    [[maybe_unused]] auto& realm = *vm.current_realm();

    // 1. Let V be undefined.
    auto value = JS::js_undefined();

    // 2. If any arguments were passed, then set V to the value of the first argument passed.
    if (vm.argument_count() > 0)
        value = vm.argument(0);

    // 3. Let id be attribute’s identifier.
    // 4. Let idlObject be null.
    // 5. If attribute is a regular attribute:

    // 1. Let jsValue be the this value, if it is not null or undefined, or realm’s global object otherwise.
    //   (This will subsequently cause a TypeError in a few steps, if the global object does not implement target and [LegacyLenientThis] is not specified.)
    // FIXME: 2. If jsValue is a platform object, then perform a security check, passing jsValue, id, and "setter".
    // 3. Let validThis be true if jsValue implements target, or false otherwise.
    auto maybe_impl = impl_from(vm);

    // 4. If validThis is false and attribute was not specified with the [LegacyLenientThis] extended attribute, then throw a TypeError.
)~~~");
    if (!attribute.extended_attributes.contains("LegacyLenientThis")) {
        attribute_generator.append(R"~~~(
    auto impl = TRY(maybe_impl);
)~~~");
    }

    // For [CEReactions]: https://html.spec.whatwg.org/multipage/custom-elements.html#cereactions

    if (attribute.extended_attributes.contains("CEReactions")) {
        // 1. Push a new element queue onto this object's relevant agent's custom element reactions stack.
        attribute_generator.append(R"~~~(
    auto& reactions_stack = HTML::relevant_similar_origin_window_agent(*impl).custom_element_reactions_stack;
    reactions_stack.element_queue_stack.append({});
)~~~");
    }

    // 5. If attribute is declared with the [Replaceable] extended attribute, then:
    if (attribute.extended_attributes.contains("Replaceable"sv)) {
        attribute_generator.append(R"~~~(
    // 1. Perform ? CreateDataPropertyOrThrow(jsValue, id, V).
    TRY(impl->create_data_property_or_throw("@attribute.name@"_utf16_fly_string, value));

    // 2. Return undefined.
    return JS::js_undefined();
}
)~~~");
        return;
    }

    // 6. If validThis is false, then return undefined.
    // NB: This is only possible if LegacyLenientThis is defined.
    if (attribute.extended_attributes.contains("LegacyLenientThis")) {
        attribute_generator.append(R"~~~(
    if (maybe_impl.is_error())
        return JS::js_undefined();

    auto impl = maybe_impl.release_value();
)~~~");
    }

    // 7. If attribute is declared with a [LegacyLenientSetter] extended attribute, then return undefined.
    if (auto legacy_lenient_setter_identifier = attribute.extended_attributes.get("LegacyLenientSetter"sv); legacy_lenient_setter_identifier.has_value()) {
        attribute_generator.append(R"~~~(
    (void)impl;
    return JS::js_undefined();
}
)~~~");
        return;
    }

    // 8. If attribute is declared with a [PutForwards] extended attribute, then:
    if (auto put_forwards_identifier = attribute.extended_attributes.get("PutForwards"sv); put_forwards_identifier.has_value()) {
        attribute_generator.set("put_forwards_identifier"sv, *put_forwards_identifier);
        VERIFY(!put_forwards_identifier->is_empty() && !is_ascii_digit(put_forwards_identifier->byte_at(0))); // Ensure `PropertyKey`s are not Numbers.

        attribute_generator.append(R"~~~(
    // 1. Let Q be ? Get(jsValue, id).
    auto receiver_value = TRY(impl->get("@attribute.name@"_utf16_fly_string));

    // 2. If Q is not an Object, then throw a TypeError.
    if (!receiver_value.is_object())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObject, receiver_value);
    auto& receiver = receiver_value.as_object();

    // 3. Let forwardId be the identifier argument of the [PutForwards] extended attribute.
    auto forward_id = "@put_forwards_identifier@"_utf16_fly_string;

    // 4. Perform ? Set(Q, forwardId, V, false).
    TRY(receiver.set(JS::PropertyKey { forward_id, JS::PropertyKey::StringMayBeNumber::No }, value, JS::Object::ShouldThrowExceptions::No));

    // 5. Return undefined.
    return JS::js_undefined();
}
)~~~");
        return;
    }

    generate_to_cpp(attribute_generator, attribute, "value", "", "cpp_value", interface, attribute.extended_attributes.contains("LegacyNullToEmptyString"));
    if (attribute.extended_attributes.contains("Reflect")) {
        if (attribute.type->name() == "boolean") {
            attribute_generator.append(R"~~~(
    if (!cpp_value)
        impl->remove_attribute("@attribute.reflect_name@"_fly_string);
    else
        impl->set_attribute_value("@attribute.reflect_name@"_fly_string, String {});
)~~~");
        } else if (attribute.type->name() == "unsigned long") {
            // The setter steps are:
            // FIXME: 1. If the reflected IDL attribute is limited to only positive numbers and the given value is 0, then throw an "IndexSizeError" DOMException.
            // 2. Let minimum be 0.
            // FIXME: 3. If the reflected IDL attribute is limited to only positive numbers or limited to only positive numbers with fallback, then set minimum to 1.
            // 4. Let newValue be minimum.
            // FIXME: 5. If the reflected IDL attribute has a default value, then set newValue to defaultValue.
            // 6. If the given value is in the range minimum to 2147483647, inclusive, then set newValue to it.
            // 7. Run this's set the content attribute with newValue converted to the shortest possible string representing the number as a valid non-negative integer.
            attribute_generator.append(R"~~~(
    u32 minimum = 0;
    u32 new_value = minimum;
    if (cpp_value >= minimum && cpp_value <= 2147483647)
        new_value = cpp_value;
    impl->set_attribute_value("@attribute.reflect_name@"_fly_string, String::number(new_value));
)~~~");
        } else if (attribute.type->is_integer() && !attribute.type->is_nullable()) {
            attribute_generator.append(R"~~~(
    impl->set_attribute_value("@attribute.reflect_name@"_fly_string, String::number(cpp_value));
)~~~");
        }
        // If a reflected IDL attribute has the type T?, where T is either Element or an interface that inherits
        // from Element, then with attr being the reflected content attribute name:
        // FIXME: Handle "an interface that inherits from Element".
        else if (attribute.type->is_nullable() && attribute.type->name() == "Element") {
            // The setter steps are:
            // 1. If the given value is null, then:
            //     1. Set this's explicitly set attr-element to null.
            //     2. Run this's delete the content attribute.
            //     3. Return.
            attribute_generator.append(R"~~~(
    auto content_attribute = "@attribute.reflect_name@"_fly_string;

    if (!cpp_value) {
        impl->set_@attribute.cpp_name@({});
        impl->remove_attribute(content_attribute);
        return JS::js_undefined();
    }
)~~~");
            // 2. Run this's set the content attribute with the empty string.
            attribute_generator.append(R"~~~(
    impl->set_attribute_value(content_attribute, String {});
)~~~");
            // 3. Set this's explicitly set attr-element to a weak reference to the given value.
            attribute_generator.append(R"~~~(
    impl->set_@attribute.cpp_name@(*cpp_value);
)~~~");
        }
        // If a reflected IDL attribute has the type FrozenArray<T>?, where T is either Element or an interface
        // that inherits from Element, then with attr being the reflected content attribute name:
        // FIXME: Handle "an interface that inherits from Element".
        else if (is_nullable_frozen_array_of_single_type(attribute.type, "Element"sv)) {
            // 1. If the given value is null:
            //     1. Set this's explicitly set attr-elements to null.
            //     2. Run this's delete the content attribute.
            //     3. Return.
            attribute_generator.append(R"~~~(
    auto content_attribute = "@attribute.reflect_name@"_fly_string;

    if (!cpp_value.has_value()) {
        impl->set_@attribute.cpp_name@({});
        impl->remove_attribute(content_attribute);
        return JS::js_undefined();
    }
)~~~");

            // 2. Run this's set the content attribute with the empty string.
            attribute_generator.append(R"~~~(
    impl->set_attribute_value(content_attribute, String {});
)~~~");

            // 3. Let elements be an empty list.
            // 4. For each element in the given value:
            //     1. Append a weak reference to element to elements.
            // 5. Set this's explicitly set attr-elements to elements.
            attribute_generator.append(R"~~~(
    Vector<GC::Weak<DOM::Element>> elements;
    elements.ensure_capacity(cpp_value->size());

    for (auto const& element : *cpp_value) {
        elements.unchecked_append(*element);
    }

    impl->set_@attribute.cpp_name@(move(elements));
)~~~");
        } else if (attribute.type->is_nullable()) {
            attribute_generator.append(R"~~~(
    if (!cpp_value.has_value())
        impl->remove_attribute("@attribute.reflect_name@"_fly_string);
    else
        impl->set_attribute_value("@attribute.reflect_name@"_fly_string, cpp_value.value());
)~~~");
        } else {
            attribute_generator.append(R"~~~(
    impl->set_attribute_value("@attribute.reflect_name@"_fly_string, cpp_value);
)~~~");
        }

        if (attribute.extended_attributes.contains("CEReactions")) {
            // 2. Run the originally-specified steps for this construct, catching any exceptions. If the steps return a value, let value be the returned value. If they throw an exception, let exception be the thrown exception.
            // 3. Let queue be the result of popping from this object's relevant agent's custom element reactions stack.
            // 4. Invoke custom element reactions in queue.
            // 5. If an exception exception was thrown by the original steps, rethrow exception.
            // 6. If a value value was returned from the original steps, return value.
            attribute_generator.append(R"~~~(
    auto queue = reactions_stack.element_queue_stack.take_last();
    Bindings::invoke_custom_element_reactions(queue);
)~~~");
        }
    } else {
        if (!attribute.extended_attributes.contains("CEReactions")) {
            attribute_generator.append(R"~~~(
    TRY(throw_dom_exception_if_needed(vm, [&] { return impl->set_@attribute.cpp_name@(cpp_value); }));
)~~~");
        } else {
            // 2. Run the originally-specified steps for this construct, catching any exceptions. If the steps return a value, let value be the returned value. If they throw an exception, let exception be the thrown exception.
            // 3. Let queue be the result of popping from this object's relevant agent's custom element reactions stack.
            // 4. Invoke custom element reactions in queue.
            // 5. If an exception exception was thrown by the original steps, rethrow exception.
            // 6. If a value value was returned from the original steps, return value.
            attribute_generator.append(R"~~~(
    auto maybe_exception = throw_dom_exception_if_needed(vm, [&] { return impl->set_@attribute.cpp_name@(cpp_value); });

    auto queue = reactions_stack.element_queue_stack.take_last();
    Bindings::invoke_custom_element_reactions(queue);

    if (maybe_exception.is_error())
        return maybe_exception.release_error();
)~~~");
        }
    }
    attribute_generator.append(R"~~~(
    return JS::js_undefined();
}
)~~~");
}

// https://webidl.spec.whatwg.org/#interface-prototype-object
void generate_prototype_or_global_mixin_definitions(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    auto is_global_interface = interface.extended_attributes.contains("Global");
    auto class_name = is_global_interface ? interface.global_mixin_class : interface.prototype_class;
    generator.set("name", interface.name);
    generator.set("namespaced_name", interface.namespaced_name);
    generator.set("class_name", class_name);
    generator.set("fully_qualified_name", interface.fully_qualified_name);
    generator.set("parent_name", interface.parent_name);
    generator.set("prototype_base_class", interface.prototype_base_class);
    generator.set("prototype_name", interface.prototype_class); // Used for Global Mixin

    if (interface.pair_iterator_types.has_value()) {
        generator.set("iterator_name", ByteString::formatted("{}Iterator", interface.name));
    }

    if (interface.is_callback_interface)
        return;

    if (!interface.attributes.is_empty() || !interface.functions.is_empty() || interface.has_stringifier || interface.set_entry_type.has_value() || interface.map_key_type.has_value()) {
        generator.append(R"~~~(
[[maybe_unused]] static JS::ThrowCompletionOr<@fully_qualified_name@*> impl_from(JS::VM& vm, JS::Value js_value)
{
)~~~");
        if (interface.name.is_one_of("EventTarget", "Window")) {
            generator.append(R"~~~(
    if (auto window_proxy = js_value.as_if<HTML::WindowProxy>())
        return window_proxy->window().ptr();
)~~~");
        }

        generator.append(R"~~~(
    if (auto impl = js_value.as_if<@fully_qualified_name@>())
        return impl.ptr();
    return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "@namespaced_name@");
}

[[maybe_unused]] static JS::ThrowCompletionOr<@fully_qualified_name@*> impl_from(JS::VM& vm)
{
    auto this_value = vm.this_value();
    if (this_value.is_nullish())
        this_value = &vm.current_realm()->global_object();
    return impl_from(vm, this_value);
}

)~~~");
    }

    for (auto& attribute : interface.attributes) {
        if (attribute.extended_attributes.contains("FIXME"))
            continue;

        bool generated_reflected_element_array = false;

        auto attribute_generator = generator.fork();
        attribute_generator.set("attribute.name", attribute.name);
        attribute_generator.set("attribute.getter_callback", attribute.getter_callback_name);
        attribute_generator.set("attribute.setter_callback", attribute.setter_callback_name);

        if (attribute.extended_attributes.contains("ImplementedAs")) {
            auto implemented_as = attribute.extended_attributes.get("ImplementedAs").value();
            attribute_generator.set("attribute.cpp_name", implemented_as);
        } else {
            attribute_generator.set("attribute.cpp_name", make_input_acceptable_cpp(attribute.name.to_snakecase()));
        }

        if (attribute.extended_attributes.contains("Reflect")) {
            // https://html.spec.whatwg.org/multipage/common-dom-interfaces.html#using-reflect-via-idl-extended-attributes:reflected-content-attribute-name
            // For one of these primary reflection extended attributes, its reflected content attribute name is the
            // string value it takes, if one is provided; otherwise it is the IDL attribute name converted to ASCII
            // lowercase.
            auto attribute_name = attribute.extended_attributes.get("Reflect").value();
            if (attribute_name.is_empty())
                attribute_name = attribute.name.to_lowercase();

            attribute_generator.set("attribute.reflect_name", attribute_name);
        } else {
            attribute_generator.set("attribute.reflect_name", attribute.name.to_snakecase());
        }

        // For [CEReactions]: https://html.spec.whatwg.org/multipage/custom-elements.html#cereactions

        attribute_generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@class_name@::@attribute.getter_callback@)
{
    WebIDL::log_trace(vm, "@class_name@::@attribute.getter_callback@");
    [[maybe_unused]] auto& realm = *vm.current_realm();
)~~~");

        // NOTE: Create a wrapper lambda so that if the function steps return an exception, we can return that in a rejected promise.
        if (attribute.type->name() == "Promise"sv) {
            attribute_generator.append(R"~~~(
    auto steps = [&]() -> JS::ThrowCompletionOr<GC::Ptr<WebIDL::Promise>> {
)~~~");
        }

        attribute_generator.append(R"~~~(
    [[maybe_unused]] auto* impl = TRY(impl_from(vm));
)~~~");

        auto cache_result = false;
        if (attribute.extended_attributes.contains("CachedAttribute")) {
            VERIFY(attribute.readonly);
            cache_result = true;
            attribute_generator.append(R"~~~(
    auto cached_@attribute.cpp_name@ = impl->cached_@attribute.cpp_name@();
    if (cached_@attribute.cpp_name@)
        return cached_@attribute.cpp_name@;
)~~~");
        }

        if (attribute.extended_attributes.contains("CEReactions")) {
            // 1. Push a new element queue onto this object's relevant agent's custom element reactions stack.
            attribute_generator.append(R"~~~(
    auto& reactions_stack = HTML::relevant_similar_origin_window_agent(*impl).custom_element_reactions_stack;
    reactions_stack.element_queue_stack.append({});
)~~~");
        }

        // https://html.spec.whatwg.org/multipage/common-dom-interfaces.html#reflecting-content-attributes-in-idl-attributes
        if (attribute.extended_attributes.contains("Reflect")) {
            if (attribute.type->name() == "DOMString") {
                if (!attribute.type->is_nullable()) {
                    // If a reflected IDL attribute has the type DOMString:
                    // * The getter steps are:

                    // 1. Let element be the result of running this's get the element.
                    // NOTE: this is "impl" above

                    // 2. Let contentAttributeValue be the result of running this's get the content attribute.
                    attribute_generator.append(R"~~~(
    auto contentAttributeValue = impl->attribute("@attribute.reflect_name@"_fly_string);
)~~~");

                    // 3. Let attributeDefinition be the attribute definition of element's content attribute whose namespace is null
                    //    and local name is the reflected content attribute name.
                    // NOTE: this is "attribute" above

                    // NOTE: We do steps 5 and 6 here to have a field to assign to
                    // 5. If contentAttributeValue is null, then return the empty string.
                    // 6. Return contentAttributeValue.
                    attribute_generator.append(R"~~~(
    auto retval = contentAttributeValue.value_or(String {});
)~~~");

                    // 4. If attributeDefinition indicates it is an enumerated attribute and the reflected IDL attribute is defined to be limited to only known values:
                    if (attribute.extended_attributes.contains("Enumerated")) {
                        auto valid_enumerations_type = attribute.extended_attributes.get("Enumerated").value();
                        auto valid_enumerations = interface.enumerations.get(valid_enumerations_type).value();

                        auto missing_value_default = valid_enumerations.extended_attributes.get("MissingValueDefault");
                        auto invalid_value_default = valid_enumerations.extended_attributes.get("InvalidValueDefault");

                        attribute_generator.set("missing_enum_default_value", missing_value_default.has_value() ? missing_value_default.value().view() : ""sv);
                        attribute_generator.set("invalid_enum_default_value", invalid_value_default.has_value() ? invalid_value_default.value().view() : ""sv);
                        attribute_generator.set("valid_enum_values", MUST(String::join(", "sv, valid_enumerations.values.values(), "\"{}\"_string"sv)));

                        // 1. If contentAttributeValue does not correspond to any state of attributeDefinition (e.g., it is null and there is no missing value default),
                        //    or that it is in a state of attributeDefinition with no associated keyword value, then return the empty string.
                        //    NOTE: @invalid_enum_default_value@ is set to the empty string if it isn't present.
                        attribute_generator.append(R"~~~(
    auto did_set_to_missing_value = false;
    if (!contentAttributeValue.has_value()) {
        retval = "@missing_enum_default_value@"_string;
        did_set_to_missing_value = true;
    }

    Array valid_values { @valid_enum_values@ };

    auto has_keyword = false;
    for (auto const& value : valid_values) {
        if (value.equals_ignoring_ascii_case(retval)) {
            has_keyword = true;
            retval = value;
            break;
        }
    }

    if (!has_keyword && !did_set_to_missing_value)
        retval = "@invalid_enum_default_value@"_string;
    )~~~");

                        // 2. Return the canonical keyword for the state of attributeDefinition that contentAttributeValue corresponds to.
                        // NOTE: This is known to be a valid keyword at this point, so we can just return "retval"
                    }
                } else {
                    // If a reflected IDL attribute has the type DOMString?:
                    // * The getter steps are:

                    // 1. Let element be the result of running this's get the element.
                    // NOTE: this is "impl" above

                    // 2. Let contentAttributeValue be the result of running this's get the content attribute.

                    attribute_generator.append(R"~~~(
    auto content_attribute_value = impl->attribute("@attribute.reflect_name@"_fly_string);
)~~~");

                    // 3. Let attributeDefinition be the attribute definition of element's content attribute whose namespace is null
                    //    and local name is the reflected content attribute name.
                    // NOTE: this is "attribute" above

                    // 4. If attributeDefinition indicates it is an enumerated attribute:
                    auto is_enumerated = attribute.extended_attributes.contains("Enumerated");
                    if (is_enumerated) {

                        // NOTE: We run step 4 here to have a field to assign to
                        // 4. Return the canonical keyword for the state of attributeDefinition that contentAttributeValue corresponds to.
                        attribute_generator.append(R"~~~(
    auto retval = impl->attribute("@attribute.reflect_name@"_fly_string);
)~~~");

                        // 1. Assert: the reflected IDL attribute is limited to only known values.
                        // NOTE: This is checked by the "Enumerated" extended attribute, so there's nothing additional to assert.

                        // 2. Assert: contentAttributeValue corresponds to a state of attributeDefinition.
                        auto valid_enumerations_type = attribute.extended_attributes.get("Enumerated").value();
                        auto valid_enumerations = interface.enumerations.get(valid_enumerations_type).value();

                        auto missing_value_default = valid_enumerations.extended_attributes.get("MissingValueDefault");
                        auto invalid_value_default = valid_enumerations.extended_attributes.get("InvalidValueDefault");

                        attribute_generator.set("missing_enum_default_value", missing_value_default.has_value() ? missing_value_default.value().view() : ""sv);
                        attribute_generator.set("invalid_enum_default_value", invalid_value_default.has_value() ? invalid_value_default.value().view() : ""sv);
                        attribute_generator.set("valid_enum_values", MUST(String::join(", "sv, valid_enumerations.values.values(), "\"{}\"_string"sv)));

                        attribute_generator.append(R"~~~(
    Array valid_values { @valid_enum_values@ };
    )~~~");
                        if (invalid_value_default.has_value()) {
                            attribute_generator.append(R"~~~(

    if (retval.has_value()) {
        auto found = false;
        for (auto const& value : valid_values) {
            if (value.equals_ignoring_ascii_case(retval.value())) {
                found = true;
                retval = value;
                break;
            }
        }

        if (!found)
            retval = "@invalid_enum_default_value@"_string;
    }
    )~~~");
                        }

                        if (missing_value_default.has_value()) {
                            attribute_generator.append(R"~~~(
    if (!retval.has_value())
        retval = "@missing_enum_default_value@"_string;
    )~~~");
                        }

                        attribute_generator.append(R"~~~(
    VERIFY(!retval.has_value() || valid_values.contains_slow(retval.value()));
)~~~");

                        // FIXME: 3. If contentAttributeValue corresponds to a state of attributeDefinition with no associated keyword value, then return null.
                    } else {
                        // 5. Return contentAttributeValue.
                        attribute_generator.append(R"~~~(
    auto retval = move(content_attribute_value);
)~~~");
                    }
                }
            }
            // If a reflected IDL attribute has the type boolean:
            else if (attribute.type->name() == "boolean") {
                // The getter steps are:
                // 1. Let contentAttributeValue be the result of running this's get the content attribute.
                // 2. If contentAttributeValue is null, then return false
                attribute_generator.append(R"~~~(
    auto retval = impl->has_attribute("@attribute.reflect_name@"_fly_string);
)~~~");
            }
            // If a reflected IDL attribute has the type long:
            else if (attribute.type->name() == "long") {
                // The getter steps are:
                // 1. Let contentAttributeValue be the result of running this's get the content attribute.
                // 2. If contentAttributeValue is not null:
                //    1. Let parsedValue be the result of integer parsing contentAttributeValue if the reflected IDL attribute is not limited to only non-negative numbers;
                //       otherwise the result of non-negative integer parsing contentAttributeValue.
                //    2. If parsedValue is not an error and is within the long range, then return parsedValue.
                attribute_generator.append(R"~~~(
    i32 retval = 0;
    auto content_attribute_value = impl->get_attribute("@attribute.reflect_name@"_fly_string);
    if (content_attribute_value.has_value()) {
        auto maybe_parsed_value = Web::HTML::parse_integer(*content_attribute_value);
        if (maybe_parsed_value.has_value())
            retval = *maybe_parsed_value;
    }
)~~~");
            }
            // If a reflected IDL attribute has the type unsigned long,
            // FIXME: optionally limited to only positive numbers, limited to only positive numbers with fallback, or clamped to the range [clampedMin, clampedMax], and optionally with a default value defaultValue:
            else if (attribute.type->name() == "unsigned long") {
                // The getter steps are:
                // 1. Let contentAttributeValue be the result of running this's get the content attribute.
                // 2. Let minimum be 0.
                // FIXME: 3. If the reflected IDL attribute is limited to only positive numbers or limited to only positive numbers with fallback, then set minimum to 1.
                // FIXME: 4. If the reflected IDL attribute is clamped to the range, then set minimum to clampedMin.
                // 5. Let maximum be 2147483647 if the reflected IDL attribute is not clamped to the range; otherwise clampedMax.
                // 6. If contentAttributeValue is not null:
                //    1. Let parsedValue be the result of non-negative integer parsing contentAttributeValue.
                //       2. If parsedValue is not an error and is in the range minimum to maximum, inclusive, then return parsedValue.
                //       FIXME: 3. If parsedValue is not an error and the reflected IDL attribute is clamped to the range:
                //              FIXME: 1. If parsedValue is less than minimum, then return minimum.
                //              FIXME: 2. Return maximum.
                attribute_generator.append(R"~~~(
    u32 retval = 0;
    auto content_attribute_value = impl->get_attribute("@attribute.reflect_name@"_fly_string);
    u32 minimum = 0;
    u32 maximum = 2147483647;
    if (content_attribute_value.has_value()) {
        auto parsed_value = Web::HTML::parse_non_negative_integer(*content_attribute_value);
        if (parsed_value.has_value()) {
            if (*parsed_value >= minimum && *parsed_value <= maximum) {
                retval = *parsed_value;
            }
        }
    }
)~~~");
            }

            // If a reflected IDL attribute has the type USVString:
            else if (attribute.type->name() == "USVString") {
                // The getter steps are:
                // 1. Let element be the result of running this's get the element.
                // NOTE: this is "impl" above
                // 2. Let contentAttributeValue be the result of running this's get the content attribute.
                attribute_generator.append(R"~~~(
    auto content_attribute_value = impl->attribute("@attribute.reflect_name@"_fly_string);
)~~~");
                // 3. Let attributeDefinition be the attribute definition of element's content attribute whose namespace is null and local name is the reflected content attribute name.
                // NOTE: this is "attribute" above

                // 4. If attributeDefinition indicates it contains a URL:
                if (attribute.extended_attributes.contains("URL")) {
                    // 1. If contentAttributeValue is null, then return the empty string.
                    // 2. Let urlString be the result of encoding-parsing-and-serializing a URL given contentAttributeValue, relative to element's node document.
                    // 3. If urlString is not failure, then return urlString.
                    attribute_generator.append(R"~~~(
    if (!content_attribute_value.has_value())
        return JS::PrimitiveString::create(vm, String {});

    auto url_string = impl->document().encoding_parse_and_serialize_url(*content_attribute_value);
    if (url_string.has_value())
        return JS::PrimitiveString::create(vm, url_string.release_value());
)~~~");
                }

                // 5. Return contentAttributeValue, converted to a scalar value string.
                attribute_generator.append(R"~~~(
    String retval;
    if (content_attribute_value.has_value())
        retval = MUST(Infra::convert_to_scalar_value_string(*content_attribute_value));
)~~~");
            }
            // If a reflected IDL attribute has the type T?, where T is either Element or an interface that inherits
            // from Element, then with attr being the reflected content attribute name:
            // FIXME: Handle "an interface that inherits from Element".
            else if (attribute.type->is_nullable() && attribute.type->name() == "Element") {
                // The getter steps are to return the result of running this's get the attr-associated element.
                attribute_generator.append(R"~~~(
    auto content_attribute = "@attribute.reflect_name@"_fly_string;

    auto retval = impl->get_the_attribute_associated_element(content_attribute, TRY(throw_dom_exception_if_needed(vm, [&] { return impl->@attribute.cpp_name@(); })));
)~~~");
            }
            // If a reflected IDL attribute has the type FrozenArray<T>?, where T is either Element or an interface that
            // inherits from Element, then with attr being the reflected content attribute name:
            // FIXME: Handle "an interface that inherits from Element".
            else if (is_nullable_frozen_array_of_single_type(attribute.type, "Element"sv)) {
                generated_reflected_element_array = true;

                // 1. Let elements be the result of running this's get the attr-associated elements.
                attribute_generator.append(R"~~~(
    auto content_attribute = "@attribute.reflect_name@"_fly_string;

    auto retval = impl->get_the_attribute_associated_elements(content_attribute, TRY(throw_dom_exception_if_needed(vm, [&] { return impl->@attribute.cpp_name@(); })));
)~~~");
            } else {
                attribute_generator.append(R"~~~(
    auto retval = impl->get_attribute_value("@attribute.reflect_name@"_fly_string);
)~~~");
            }

            if (attribute.extended_attributes.contains("CEReactions")) {
                // 2. Run the originally-specified steps for this construct, catching any exceptions. If the steps return a value, let value be the returned value. If they throw an exception, let exception be the thrown exception.
                // 3. Let queue be the result of popping from this object's relevant agent's custom element reactions stack.
                // 4. Invoke custom element reactions in queue.
                // 5. If an exception exception was thrown by the original steps, rethrow exception.
                // 6. If a value value was returned from the original steps, return value.
                attribute_generator.append(R"~~~(
    auto queue = reactions_stack.element_queue_stack.take_last();
    Bindings::invoke_custom_element_reactions(queue);
)~~~");
            }

            if (generated_reflected_element_array) {
                // 2. If the contents of elements is equal to the contents of this's cached attr-associated elements,
                //    then return this's cached attr-associated elements object.
                attribute_generator.append(R"~~~(
    auto cached_@attribute.cpp_name@ = TRY(throw_dom_exception_if_needed(vm, [&] { return impl->cached_@attribute.cpp_name@(); }));
    if (WebIDL::lists_contain_same_elements(cached_@attribute.cpp_name@, retval))
        return cached_@attribute.cpp_name@;

    auto result = TRY([&]() -> JS::ThrowCompletionOr<JS::Value> {
)~~~");
            }

        } else {
            if (!attribute.extended_attributes.contains("CEReactions")) {
                attribute_generator.append(R"~~~(
    auto retval = TRY(throw_dom_exception_if_needed(vm, [&] { return impl->@attribute.cpp_name@(); }));
)~~~");
            } else {
                // 2. Run the originally-specified steps for this construct, catching any exceptions. If the steps return a value, let value be the returned value. If they throw an exception, let exception be the thrown exception.
                // 3. Let queue be the result of popping from this object's relevant agent's custom element reactions stack.
                // 4. Invoke custom element reactions in queue.
                // 5. If an exception exception was thrown by the original steps, rethrow exception.
                // 6. If a value value was returned from the original steps, return value.
                attribute_generator.append(R"~~~(
    auto retval_or_exception = throw_dom_exception_if_needed(vm, [&] { return impl->@attribute.cpp_name@(); });

    auto queue = reactions_stack.element_queue_stack.take_last();
    Bindings::invoke_custom_element_reactions(queue);

    if (retval_or_exception.is_error())
        return retval_or_exception.release_error();

    auto retval = retval_or_exception.release_value();
)~~~");
            }
        }

        if (attribute.type->name() == "Promise"sv) {
            attribute_generator.append(R"~~~(
        return retval;
    };

    auto maybe_retval = steps();

    // And then, if an exception E was thrown:
    // 1. If attribute’s type is a promise type, then return ! Call(%Promise.reject%, %Promise%, «E»).
    // 2. Otherwise, end these steps and allow the exception to propagate.
    if (maybe_retval.is_throw_completion())
        return WebIDL::create_rejected_promise(realm, maybe_retval.error_value())->promise();

    auto retval = maybe_retval.release_value();
)~~~");
        }

        if (cache_result) {
            generate_wrap_statement(generator, "retval", *attribute.type, interface, ByteString::formatted("cached_{} =", attribute_generator.get("attribute.cpp_name")));
            attribute_generator.append(R"~~~(
    impl->set_cached_@attribute.cpp_name@(cached_@attribute.cpp_name@);
    return cached_@attribute.cpp_name@;
)~~~");
        } else {
            generate_return_statement(generator, *attribute.type, interface);
        }

        if (generated_reflected_element_array) {
            // 3. Let elementsAsFrozenArray be elements, converted to a FrozenArray<T>?.
            // 4. Set this's cached attr-associated elements to elements.
            // 5. Set this's cached attr-associated elements object to elementsAsFrozenArray.
            attribute_generator.append(R"~~~(
    }());

    if (result.is_null()) {
        TRY(throw_dom_exception_if_needed(vm, [&] { impl->set_cached_@attribute.cpp_name@({}); }));
    } else {
        auto& array = as<JS::Array>(result.as_object());
        TRY(throw_dom_exception_if_needed(vm, [&] { impl->set_cached_@attribute.cpp_name@(&array); }));
    }

    return result;
)~~~");
        }

        attribute_generator.append(R"~~~(
}
)~~~");

        // https://webidl.spec.whatwg.org/#dfn-attribute-setter
        // 2. If attribute is read only and does not have a [LegacyLenientSetter], [PutForwards] or [Replaceable] extended attribute, return undefined; there is no attribute setter function.
        if (!attribute.readonly || attribute.extended_attributes.contains("LegacyLenientSetter"sv) || attribute.extended_attributes.contains("PutForwards"sv) || attribute.extended_attributes.contains("Replaceable"sv)) {
            generate_attribute_setter(attribute_generator, attribute, interface);
        }
    }

    // Implementation: Functions
    for (auto& function : interface.functions) {
        if (function.extended_attributes.contains("FIXME"))
            continue;
        if (function.extended_attributes.contains("Default")) {
            if (function.name == "toJSON"sv && function.return_type->name() == "object"sv) {
                generate_default_to_json_function(generator, class_name, interface);
                continue;
            }

            dbgln("Unknown default operation: {} {}()", function.return_type->name(), function.name);
            VERIFY_NOT_REACHED();
        }

        generate_function(generator, function, StaticFunction::No, class_name, interface.fully_qualified_name, interface);
    }

    for (auto const& overload_set : interface.overload_sets) {
        if (overload_set.value.size() == 1)
            continue;
        generate_overload_arbiter(generator, overload_set, interface, class_name, IsConstructor::No);
    }

    if (interface.has_stringifier) {
        auto stringifier_generator = generator.fork();
        stringifier_generator.set("class_name", class_name);
        if (interface.stringifier_attribute.has_value())
            stringifier_generator.set("attribute.cpp_getter_name", interface.stringifier_attribute.value().name.to_snakecase());

        stringifier_generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@class_name@::to_string)
{
    WebIDL::log_trace(vm, "@class_name@::to_string");
    [[maybe_unused]] auto& realm = *vm.current_realm();
    auto* impl = TRY(impl_from(vm));

)~~~");
        if (interface.stringifier_attribute.has_value()) {
            stringifier_generator.append(R"~~~(
    auto retval = TRY(throw_dom_exception_if_needed(vm, [&] { return impl->@attribute.cpp_getter_name@(); }));
)~~~");
        } else {
            stringifier_generator.append(R"~~~(
    auto retval = TRY(throw_dom_exception_if_needed(vm, [&] { return impl->to_string(); }));
)~~~");
        }
        stringifier_generator.append(R"~~~(

    return JS::PrimitiveString::create(vm, move(retval));
}
)~~~");
    }

    if (interface.pair_iterator_types.has_value()) {
        auto iterator_generator = generator.fork();
        iterator_generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@class_name@::entries)
{
    WebIDL::log_trace(vm, "@class_name@::entries");
    auto* impl = TRY(impl_from(vm));

    return TRY(throw_dom_exception_if_needed(vm, [&] { return @iterator_name@::create(*impl, Object::PropertyKind::KeyAndValue); }));
}

JS_DEFINE_NATIVE_FUNCTION(@class_name@::for_each)
{
    WebIDL::log_trace(vm, "@class_name@::for_each");
    [[maybe_unused]] auto& realm = *vm.current_realm();
    auto* impl = TRY(impl_from(vm));

    auto callback = vm.argument(0);
    if (!callback.is_function())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAFunction, callback);

    auto this_value = vm.this_value();
    TRY(impl->for_each([&](auto key, auto value) -> JS::ThrowCompletionOr<void> {
)~~~");
        generate_variable_statement(iterator_generator, "wrapped_key", interface.pair_iterator_types->get<0>(), "key", interface);
        generate_variable_statement(iterator_generator, "wrapped_value", interface.pair_iterator_types->get<1>(), "value", interface);
        iterator_generator.append(R"~~~(
        TRY(JS::call(vm, callback.as_function(), vm.argument(1), wrapped_value, wrapped_key, this_value));
        return {};
    }));

    return JS::js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(@class_name@::keys)
{
    WebIDL::log_trace(vm, "@class_name@::keys");
    auto* impl = TRY(impl_from(vm));

    return TRY(throw_dom_exception_if_needed(vm, [&] { return @iterator_name@::create(*impl, Object::PropertyKind::Key);  }));
}

JS_DEFINE_NATIVE_FUNCTION(@class_name@::values)
{
    WebIDL::log_trace(vm, "@class_name@::values");
    auto* impl = TRY(impl_from(vm));

    return TRY(throw_dom_exception_if_needed(vm, [&] { return @iterator_name@::create(*impl, Object::PropertyKind::Value); }));
}
)~~~");
    }

    // https://webidl.spec.whatwg.org/#js-asynchronous-iterable
    if (interface.async_value_iterator_type.has_value()) {
        auto iterator_generator = generator.fork();
        iterator_generator.set("iterator_name"sv, MUST(String::formatted("{}AsyncIterator", interface.name)));
        iterator_generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@class_name@::values)
{
    WebIDL::log_trace(vm, "@class_name@::values");
    auto& realm = *vm.current_realm();
    auto* impl = TRY(impl_from(vm));
)~~~");

        StringBuilder arguments_builder;
        generate_arguments(generator, interface.async_value_iterator_parameters, arguments_builder, interface);

        iterator_generator.append(R"~~~(
    return TRY(throw_dom_exception_if_needed(vm, [&] { return @iterator_name@::create(realm, Object::PropertyKind::Value, *impl)~~~");

        if (!arguments_builder.is_empty()) {
            iterator_generator.set("iterator_arguments"sv, MUST(arguments_builder.to_string()));
            iterator_generator.append(", @iterator_arguments@");
        }

        iterator_generator.append(R"~~~(); }));
}
)~~~");
    }

    if (interface.set_entry_type.has_value()) {
        auto setlike_generator = generator.fork();
        auto const& set_entry_type = *interface.set_entry_type.value();
        setlike_generator.set("value_type", set_entry_type.name());

        if (set_entry_type.is_string()) {
            setlike_generator.set("value_type_check", R"~~~(
    if (!value_arg.is_string()) {
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "String");
    }
)~~~");
        } else {
            setlike_generator.set("value_type_check",
                MUST(String::formatted(R"~~~(
    if (!value_arg.is_object() || !is<{0}>(value_arg.as_object())) {{
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "{0}");
    }}
)~~~",
                    set_entry_type.name())));
        }

        setlike_generator.append(R"~~~(
// https://webidl.spec.whatwg.org/#js-set-size
JS_DEFINE_NATIVE_FUNCTION(@class_name@::get_size)
{
    WebIDL::log_trace(vm, "@class_name@::size");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Set> set = impl->set_entries();

    return set->set_size();
}

// https://webidl.spec.whatwg.org/#js-set-entries
JS_DEFINE_NATIVE_FUNCTION(@class_name@::entries)
{
    WebIDL::log_trace(vm, "@class_name@::entries");
    auto& realm = *vm.current_realm();
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Set> set = impl->set_entries();

    return TRY(throw_dom_exception_if_needed(vm, [&] { return JS::SetIterator::create(realm, *set, Object::PropertyKind::KeyAndValue); }));
}

// https://webidl.spec.whatwg.org/#js-set-values
JS_DEFINE_NATIVE_FUNCTION(@class_name@::values)
{
    WebIDL::log_trace(vm, "@class_name@::values");
    auto& realm = *vm.current_realm();
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Set> set = impl->set_entries();

    return TRY(throw_dom_exception_if_needed(vm, [&] { return JS::SetIterator::create(realm, *set, Object::PropertyKind::Value); }));
}

// https://webidl.spec.whatwg.org/#js-set-forEach
JS_DEFINE_NATIVE_FUNCTION(@class_name@::for_each)
{
    WebIDL::log_trace(vm, "@class_name@::for_each");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Set> set = impl->set_entries();

    auto callback = vm.argument(0);
    if (!callback.is_function())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAFunction, callback);

    for (auto& entry : *set) {
        auto value = entry.key;
        TRY(JS::call(vm, callback.as_function(), vm.argument(1), value, value, impl));
    }

    return JS::js_undefined();
}

// https://webidl.spec.whatwg.org/#js-set-has
JS_DEFINE_NATIVE_FUNCTION(@class_name@::has)
{
    WebIDL::log_trace(vm, "@class_name@::has");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Set> set = impl->set_entries();

    auto value_arg = vm.argument(0);
    @value_type_check@

    // FIXME: If value is -0, set value to +0.
    // What? Which interfaces have a number as their set type?

    return set->set_has(value_arg);
}
)~~~");

        if (!interface.overload_sets.contains("add"sv) && !interface.is_set_readonly) {
            setlike_generator.append(R"~~~(
// https://webidl.spec.whatwg.org/#js-set-add
JS_DEFINE_NATIVE_FUNCTION(@class_name@::add)
{
    WebIDL::log_trace(vm, "@class_name@::add");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Set> set = impl->set_entries();

    auto value_arg = vm.argument(0);
    @value_type_check@

    // FIXME: If value is -0, set value to +0.
    // What? Which interfaces have a number as their set type?

    set->set_add(value_arg);
    impl->on_set_modified_from_js({});

    return impl;
}
)~~~");
        }
        if (!interface.overload_sets.contains("delete"sv) && !interface.is_set_readonly) {
            setlike_generator.append(R"~~~(
// https://webidl.spec.whatwg.org/#js-set-delete
JS_DEFINE_NATIVE_FUNCTION(@class_name@::delete_)
{
    WebIDL::log_trace(vm, "@class_name@::delete_");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Set> set = impl->set_entries();

    auto value_arg = vm.argument(0);
    @value_type_check@

    // FIXME: If value is -0, set value to +0.
    // What? Which interfaces have a number as their set type?

    auto result = set->set_remove(value_arg);
    impl->on_set_modified_from_js({});
    return result;
}
)~~~");
        }
        if (!interface.overload_sets.contains("clear"sv) && !interface.is_set_readonly) {
            setlike_generator.append(R"~~~(
// https://webidl.spec.whatwg.org/#js-set-clear
JS_DEFINE_NATIVE_FUNCTION(@class_name@::clear)
{
    WebIDL::log_trace(vm, "@class_name@::clear");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Set> set = impl->set_entries();

    set->set_clear();
    impl->on_set_modified_from_js({});

    return JS::js_undefined();
}
)~~~");
        }
    }

    if (interface.map_key_type.has_value()) {
        auto maplike_generator = generator.fork();

        if (interface.map_key_type.value()->is_string()) {
            maplike_generator.set("key_arg_converted_to_idl_type", "JS::PrimitiveString::create(vm, TRY(key_arg.to_string(vm)));");
        } else {
            TODO();
        }

        if (interface.map_value_type.value()->is_sequence() && interface.map_value_type.value()->as_parameterized().parameters().at(0)->is_numeric()) {
            // FIXME: We should convert rather than just fail if we have the wrong type.
            maplike_generator.set("value_arg_converted_to_idl_type", R"~~~([&](){
    if (!value_arg.is_object() || !is<JS::Array>(value_arg.as_object())) {
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "Array");
    }

    for (auto const& item : as<JS::Array>(value_arg.as_object())) {
        if (!item.is_numeric()) {
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "Number");
        }
    }

    return value_arg;
}();
)~~~");
        } else {
            TODO();
        }

        maplike_generator.append(R"~~~(
// https://webidl.spec.whatwg.org/#js-map-size
JS_DEFINE_NATIVE_FUNCTION(@class_name@::get_size)
{
    WebIDL::log_trace(vm, "@class_name@::size");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    return map->map_size();
}

// https://webidl.spec.whatwg.org/#js-map-entries
JS_DEFINE_NATIVE_FUNCTION(@class_name@::entries)
{
    WebIDL::log_trace(vm, "@class_name@::entries");
    auto& realm = *vm.current_realm();
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    return TRY(throw_dom_exception_if_needed(vm, [&] { return JS::MapIterator::create(realm, *map, Object::PropertyKind::KeyAndValue); }));
}

// https://webidl.spec.whatwg.org/#js-map-keys
JS_DEFINE_NATIVE_FUNCTION(@class_name@::keys)
{
    WebIDL::log_trace(vm, "@class_name@::keys");
    auto& realm = *vm.current_realm();
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    return TRY(throw_dom_exception_if_needed(vm, [&] { return JS::MapIterator::create(realm, *map, Object::PropertyKind::Key); }));
}

// https://webidl.spec.whatwg.org/#js-map-values
JS_DEFINE_NATIVE_FUNCTION(@class_name@::values)
{
    WebIDL::log_trace(vm, "@class_name@::values");
    auto& realm = *vm.current_realm();
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    return TRY(throw_dom_exception_if_needed(vm, [&] { return JS::MapIterator::create(realm, *map, Object::PropertyKind::Value); }));
}

// https://webidl.spec.whatwg.org/#js-map-forEach
JS_DEFINE_NATIVE_FUNCTION(@class_name@::for_each)
{
    WebIDL::log_trace(vm, "@class_name@::for_each");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    auto callback = vm.argument(0);
    if (!callback.is_function())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAFunction, callback);

    for (auto& entry : *map)
        TRY(JS::call(vm, callback.as_function(), vm.argument(1), entry.key, entry.value, impl));

    return JS::js_undefined();
}

// https://webidl.spec.whatwg.org/#js-map-get
JS_DEFINE_NATIVE_FUNCTION(@class_name@::get)
{
    WebIDL::log_trace(vm, "@class_name@::get");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    auto key_arg = vm.argument(0);
    auto key = @key_arg_converted_to_idl_type@

    // FIXME: If key is -0, set key to +0.
    // What? Which interfaces have a number as their map key type?

    auto result = map->map_get(key);

    if (!result.has_value())
        return JS::js_undefined();

    return result.release_value();
}

// https://webidl.spec.whatwg.org/#js-map-has
JS_DEFINE_NATIVE_FUNCTION(@class_name@::has)
{
    WebIDL::log_trace(vm, "@class_name@::has");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    auto key_arg = vm.argument(0);
    auto key = @key_arg_converted_to_idl_type@

    // FIXME: If key is -0, set key to +0.
    // What? Which interfaces have a number as their map key type?

    return map->map_has(key);
}
)~~~");

        if (!interface.overload_sets.contains("set"sv) && !interface.is_map_readonly) {
            maplike_generator.append(R"~~~(
// https://webidl.spec.whatwg.org/#js-map-set
JS_DEFINE_NATIVE_FUNCTION(@class_name@::set)
{
    WebIDL::log_trace(vm, "@class_name@::set");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    auto key_arg = vm.argument(0);
    auto key = @key_arg_converted_to_idl_type@

    // FIXME: If value is -0, set value to +0.
    // What? Which interfaces have a number as their set type?

    auto value_arg = vm.argument(1);
    auto value = @value_arg_converted_to_idl_type@

    map->map_set(key, value);
    impl->on_map_modified_from_js({});

    return impl;
}
)~~~");
        }
        if (!interface.overload_sets.contains("delete"sv) && !interface.is_map_readonly) {
            maplike_generator.append(R"~~~(
// https://webidl.spec.whatwg.org/#js-map-delete
JS_DEFINE_NATIVE_FUNCTION(@class_name@::delete_)
{
    WebIDL::log_trace(vm, "@class_name@::delete_");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    auto key_arg = vm.argument(0);
    auto key = @key_arg_converted_to_idl_type@

    // FIXME: If key is -0, set key to +0.
    // What? Which interfaces have a number as their map key type?

    auto result = map->map_remove(key);
    impl->on_map_modified_from_js({});

    return result;
}
)~~~");
        }
        if (!interface.overload_sets.contains("clear"sv) && !interface.is_map_readonly) {
            maplike_generator.append(R"~~~(
// https://webidl.spec.whatwg.org/#js-map-clear
JS_DEFINE_NATIVE_FUNCTION(@class_name@::clear)
{
    WebIDL::log_trace(vm, "@class_name@::clear");
    auto* impl = TRY(impl_from(vm));

    GC::Ref<JS::Map> map = impl->map_entries();

    map->map_clear();
    impl->on_map_modified_from_js({});

    return JS::js_undefined();
}
)~~~");
        }
    }

    generate_dictionaries(generator, interface);
}

void generate_prototype_header(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("prototype_class", interface.prototype_class);

    generator.append(R"~~~(
#pragma once

#include <LibJS/Runtime/Object.h>

namespace Web::Bindings {

class @prototype_class@ : public JS::Object {
    JS_OBJECT(@prototype_class@, JS::Object);
    GC_DECLARE_ALLOCATOR(@prototype_class@);
public:
    static void define_unforgeable_attributes(JS::Realm&, JS::Object&);

    explicit @prototype_class@(JS::Realm&);
    virtual void initialize(JS::Realm&) override;
    virtual ~@prototype_class@() override;
private:
)~~~");

    // Generate an empty prototype object for global interfaces.
    auto is_global_interface = interface.extended_attributes.contains("Global");
    if (is_global_interface) {
        generator.append(R"~~~(
};
)~~~");
        if (interface.supports_named_properties()) {
            generate_named_properties_object_declarations(interface, builder);
        }
    } else {
        generate_prototype_or_global_mixin_declarations(interface, builder);
    }

    generator.append(R"~~~(
} // namespace Web::Bindings
    )~~~");
}

void generate_prototype_implementation(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("parent_name", interface.parent_name);
    generator.set("prototype_class", interface.prototype_class);
    generator.set("prototype_base_class", interface.prototype_base_class);

    generator.append(R"~~~(
#include <AK/Function.h>
#include <LibIDL/Types.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/DataView.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/FunctionObject.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Iterator.h>
#include <LibJS/Runtime/PromiseConstructor.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibJS/Runtime/Value.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibURL/Origin.h>
#include <LibWeb/Bindings/@prototype_class@.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/Bindings/PrincipalHostDefined.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/DOM/Element.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/DOM/IDLEventListener.h>
#include <LibWeb/DOM/NodeFilter.h>
#include <LibWeb/DOM/Range.h>
#include <LibWeb/HTML/Numbers.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/SimilarOriginWindowAgent.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/WindowProxy.h>
#include <LibWeb/Infra/Strings.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/Buffers.h>
#include <LibWeb/WebIDL/OverloadResolution.h>
#include <LibWeb/WebIDL/Promise.h>
#include <LibWeb/WebIDL/Tracing.h>
#include <LibWeb/WebIDL/Types.h>

#if __has_include(<LibWeb/Bindings/@prototype_base_class@.h>)
#    include <LibWeb/Bindings/@prototype_base_class@.h>
#endif

)~~~");

    bool has_ce_reactions = false;
    for (auto const& function : interface.functions) {
        if (function.extended_attributes.contains("FIXME"))
            continue;
        if (function.extended_attributes.contains("CEReactions")) {
            has_ce_reactions = true;
            break;
        }
    }

    if (!has_ce_reactions) {
        for (auto const& attribute : interface.attributes) {
            if (attribute.extended_attributes.contains("CEReactions")) {
                has_ce_reactions = true;
                break;
            }
        }
    }

    if (!has_ce_reactions && interface.indexed_property_setter.has_value() && interface.indexed_property_setter->extended_attributes.contains("CEReactions"))
        has_ce_reactions = true;

    if (!has_ce_reactions && interface.named_property_setter.has_value() && interface.named_property_setter->extended_attributes.contains("CEReactions"))
        has_ce_reactions = true;

    if (!has_ce_reactions && interface.named_property_deleter.has_value() && interface.named_property_deleter->extended_attributes.contains("CEReactions"))
        has_ce_reactions = true;

    if (has_ce_reactions) {
        generator.append(R"~~~(
#include <LibWeb/Bindings/MainThreadVM.h>
)~~~");
    }

    emit_includes_for_all_imports(interface, generator, interface.pair_iterator_types.has_value(), interface.async_value_iterator_type.has_value());

    generate_using_namespace_definitions(generator);

    generator.append(R"~~~(
namespace Web::Bindings {

GC_DEFINE_ALLOCATOR(@prototype_class@);

@prototype_class@::@prototype_class@([[maybe_unused]] JS::Realm& realm))~~~");
    if (interface.name == "DOMException") {
        // https://webidl.spec.whatwg.org/#es-DOMException-specialness
        // Object.getPrototypeOf(DOMException.prototype) === Error.prototype
        generator.append(R"~~~(
    : Object(ConstructWithPrototypeTag::Tag, realm.intrinsics().error_prototype())
)~~~");
    } else if (!interface.parent_name.is_empty()) {
        generator.append(R"~~~(
    : Object(realm, nullptr)
)~~~");
    } else {
        generator.append(R"~~~(
    : Object(ConstructWithPrototypeTag::Tag, realm.intrinsics().object_prototype())
)~~~");
    }

    generator.append(R"~~~(
{
}

@prototype_class@::~@prototype_class@()
{
}
)~~~");

    // Generate a mostly empty prototype object for global interfaces.
    auto is_global_interface = interface.extended_attributes.contains("Global");
    if (is_global_interface) {
        generator.append(R"~~~(
void @prototype_class@::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
)~~~");
        if (interface.supports_named_properties()) {
            generator.set("named_properties_class", ByteString::formatted("{}Properties", interface.name));
            generator.set("namespaced_name", interface.namespaced_name);
            generator.append(R"~~~(
    define_direct_property(vm().well_known_symbol_to_string_tag(), JS::PrimitiveString::create(vm(), "@namespaced_name@"_string), JS::Attribute::Configurable);
    set_prototype(&ensure_web_prototype<@prototype_class@>(realm, "@named_properties_class@"_fly_string));
)~~~");
        } else {
            generator.append(R"~~~(
    set_prototype(&ensure_web_prototype<@prototype_base_class@>(realm, "@parent_name@"_fly_string));
)~~~");
        }
        generator.append(R"~~~(
}
)~~~");
        if (interface.supports_named_properties())
            generate_named_properties_object_definitions(interface, builder);
    } else if (interface.is_callback_interface) {
        generator.append(R"~~~(
void @prototype_class@::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    set_prototype(realm.intrinsics().object_prototype());
}
)~~~");
    } else {
        generate_prototype_or_global_mixin_initialization(interface, builder, GenerateUnforgeables::No);
        generate_prototype_or_global_mixin_initialization(interface, builder, GenerateUnforgeables::Yes);
        generate_prototype_or_global_mixin_definitions(interface, builder);
    }

    generator.append(R"~~~(
} // namespace Web::Bindings
)~~~");
}

void generate_iterator_prototype_header(IDL::Interface const& interface, StringBuilder& builder)
{
    VERIFY(interface.pair_iterator_types.has_value());
    SourceGenerator generator { builder };

    generator.set("prototype_class", ByteString::formatted("{}IteratorPrototype", interface.name));

    generator.append(R"~~~(
#pragma once

#include <LibJS/Runtime/Object.h>

namespace Web::Bindings {

class @prototype_class@ : public JS::Object {
    JS_OBJECT(@prototype_class@, JS::Object);
    GC_DECLARE_ALLOCATOR(@prototype_class@);
public:
    explicit @prototype_class@(JS::Realm&);
    virtual void initialize(JS::Realm&) override;
    virtual ~@prototype_class@() override;

private:
    JS_DECLARE_NATIVE_FUNCTION(next);
};

} // namespace Web::Bindings
    )~~~");
}

void generate_iterator_prototype_implementation(IDL::Interface const& interface, StringBuilder& builder)
{
    VERIFY(interface.pair_iterator_types.has_value());
    SourceGenerator generator { builder };

    generator.set("name", ByteString::formatted("{}Iterator", interface.name));
    generator.set("parent_name", interface.parent_name);
    generator.set("prototype_class", ByteString::formatted("{}IteratorPrototype", interface.name));
    generator.set("to_string_tag", ByteString::formatted("{} Iterator", interface.name));
    generator.set("prototype_base_class", interface.prototype_base_class);
    generator.set("fully_qualified_name", ByteString::formatted("{}Iterator", interface.fully_qualified_name));
    generator.set("possible_include_path", ByteString::formatted("{}Iterator", interface.name.replace("::"sv, "/"sv, ReplaceMode::All)));

    generator.append(R"~~~(
#include <AK/Function.h>
#include <AK/TypeCasts.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/FunctionObject.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibWeb/Bindings/@prototype_class@.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebIDL/Tracing.h>
)~~~");

    emit_includes_for_all_imports(interface, generator, true);

    generate_using_namespace_definitions(generator);

    generator.append(R"~~~(
namespace Web::Bindings {

GC_DEFINE_ALLOCATOR(@prototype_class@);

@prototype_class@::@prototype_class@(JS::Realm& realm)
    : Object(ConstructWithPrototypeTag::Tag, realm.intrinsics().iterator_prototype())
{
}

@prototype_class@::~@prototype_class@()
{
}

void @prototype_class@::initialize(JS::Realm& realm)
{
    auto& vm = this->vm();
    Base::initialize(realm);
    define_native_function(realm, vm.names.next, next, 0, JS::Attribute::Writable | JS::Attribute::Enumerable | JS::Attribute::Configurable);
    define_direct_property(vm.well_known_symbol_to_string_tag(), JS::PrimitiveString::create(vm, "@to_string_tag@"_string), JS::Attribute::Configurable);
}

JS::ThrowCompletionOr<@fully_qualified_name@*> impl_from(JS::VM& vm)
{
    auto this_object = TRY(vm.this_value().to_object(vm));
    if (!is<@fully_qualified_name@>(*this_object))
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "@name@");
    return static_cast<@fully_qualified_name@*>(this_object.ptr());
}

JS_DEFINE_NATIVE_FUNCTION(@prototype_class@::next)
{
    WebIDL::log_trace(vm, "@prototype_class@::next");
    auto* impl = TRY(impl_from(vm));
    return TRY(throw_dom_exception_if_needed(vm, [&] { return impl->next(); }));
}

} // namespace Web::Bindings
)~~~");
}

void generate_async_iterator_prototype_header(IDL::Interface const& interface, StringBuilder& builder)
{
    VERIFY(interface.async_value_iterator_type.has_value());
    SourceGenerator generator { builder };

    generator.set("prototype_class", ByteString::formatted("{}AsyncIteratorPrototype", interface.name));

    generator.append(R"~~~(
#pragma once

#include <LibJS/Runtime/Object.h>

namespace Web::Bindings {

class @prototype_class@ : public JS::Object {
    JS_OBJECT(@prototype_class@, JS::Object);
    GC_DECLARE_ALLOCATOR(@prototype_class@);

public:
    explicit @prototype_class@(JS::Realm&);
    virtual void initialize(JS::Realm&) override;
    virtual ~@prototype_class@() override;

private:
    JS_DECLARE_NATIVE_FUNCTION(next);
    )~~~");

    if (interface.extended_attributes.contains("DefinesAsyncIteratorReturn")) {
        generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(return_);
)~~~");
    }

    generator.append(R"~~~(
};

} // namespace Web::Bindings
    )~~~");
}

void generate_async_iterator_prototype_implementation(IDL::Interface const& interface, StringBuilder& builder)
{
    VERIFY(interface.async_value_iterator_type.has_value());
    SourceGenerator generator { builder };

    generator.set("name", ByteString::formatted("{}AsyncIterator", interface.name));
    generator.set("parent_name", interface.parent_name);
    generator.set("prototype_class", ByteString::formatted("{}AsyncIteratorPrototype", interface.name));
    generator.set("to_string_tag", ByteString::formatted("{} AsyncIterator", interface.name));
    generator.set("prototype_base_class", interface.prototype_base_class);
    generator.set("fully_qualified_name", ByteString::formatted("{}AsyncIterator", interface.fully_qualified_name));
    generator.set("possible_include_path", ByteString::formatted("{}AsyncIterator", interface.name.replace("::"sv, "/"sv, ReplaceMode::All)));

    generator.append(R"~~~(
#include <AK/Function.h>
#include <AK/TypeCasts.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/FunctionObject.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/Bindings/@prototype_class@.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebIDL/AsyncIterator.h>
#include <LibWeb/WebIDL/Tracing.h>
)~~~");

    emit_includes_for_all_imports(interface, generator, false, true);

    generate_using_namespace_definitions(generator);

    generator.append(R"~~~(
namespace Web::Bindings {

GC_DEFINE_ALLOCATOR(@prototype_class@);

@prototype_class@::@prototype_class@(JS::Realm& realm)
    : Object(ConstructWithPrototypeTag::Tag, realm.intrinsics().async_iterator_prototype())
{
}

@prototype_class@::~@prototype_class@()
{
}

void @prototype_class@::initialize(JS::Realm& realm)
{
    auto& vm = this->vm();
    Base::initialize(realm);
    define_direct_property(vm.well_known_symbol_to_string_tag(), JS::PrimitiveString::create(vm, "@to_string_tag@"_string), JS::Attribute::Configurable);

    define_native_function(realm, vm.names.next, next, 0, JS::default_attributes);)~~~");

    if (interface.extended_attributes.contains("DefinesAsyncIteratorReturn")) {
        generator.append(R"~~~(
    define_native_function(realm, vm.names.return_, return_, 1, JS::default_attributes);)~~~");
    }

    generator.append(R"~~~(
}

JS_DEFINE_NATIVE_FUNCTION(@prototype_class@::next)
{
    WebIDL::log_trace(vm, "@prototype_class@::next");
    auto& realm = *vm.current_realm();

    return TRY(throw_dom_exception_if_needed(vm, [&] {
        return WebIDL::AsyncIterator::next<@fully_qualified_name@>(realm, "@name@"sv);
    }));
}
)~~~");

    if (interface.extended_attributes.contains("DefinesAsyncIteratorReturn")) {
        generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@prototype_class@::return_)
{
    WebIDL::log_trace(vm, "@prototype_class@::return");
    auto& realm = *vm.current_realm();

    auto value = vm.argument(0);

    return TRY(throw_dom_exception_if_needed(vm, [&] {
        return WebIDL::AsyncIterator::return_<@fully_qualified_name@>(realm, "@name@"sv, value);
    }));
}
)~~~");
    }

    generator.append(R"~~~(
} // namespace Web::Bindings
)~~~");
}

void generate_global_mixin_header(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("class_name", interface.global_mixin_class);

    generator.append(R"~~~(
#pragma once

#include <LibJS/Runtime/Object.h>

namespace Web::Bindings {

class @class_name@ {
public:
    void initialize(JS::Realm&, JS::Object&);
    void define_unforgeable_attributes(JS::Realm&, JS::Object&);
    @class_name@();
    virtual ~@class_name@();

private:
)~~~");

    generate_prototype_or_global_mixin_declarations(interface, builder);

    generator.append(R"~~~(
} // namespace Web::Bindings
    )~~~");
}

void generate_global_mixin_implementation(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("class_name", interface.global_mixin_class);
    generator.set("prototype_name", interface.prototype_class);

    generator.append(R"~~~(
#include <AK/Function.h>
#include <LibIDL/Types.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/DataView.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/FunctionObject.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Iterator.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibJS/Runtime/Value.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibURL/Origin.h>
#include <LibWeb/Bindings/@class_name@.h>
#include <LibWeb/Bindings/@prototype_name@.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/PrincipalHostDefined.h>
#include <LibWeb/DOM/Element.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/DOM/IDLEventListener.h>
#include <LibWeb/DOM/NodeFilter.h>
#include <LibWeb/DOM/Range.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/WindowProxy.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/OverloadResolution.h>
#include <LibWeb/WebIDL/Tracing.h>
#include <LibWeb/WebIDL/Types.h>

)~~~");

    emit_includes_for_all_imports(interface, generator, interface.pair_iterator_types.has_value(), interface.async_value_iterator_type.has_value());

    generate_using_namespace_definitions(generator);

    generator.append(R"~~~(
namespace Web::Bindings {

@class_name@::@class_name@() = default;
@class_name@::~@class_name@() = default;
)~~~");

    generate_prototype_or_global_mixin_initialization(interface, builder, GenerateUnforgeables::No);
    generate_prototype_or_global_mixin_initialization(interface, builder, GenerateUnforgeables::Yes);
    generate_prototype_or_global_mixin_definitions(interface, builder);

    generator.append(R"~~~(
} // namespace Web::Bindings
    )~~~");
}

}
