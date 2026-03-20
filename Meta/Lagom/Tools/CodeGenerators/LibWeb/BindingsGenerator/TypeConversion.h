/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "CodeGeneratorUtils.h"
#include "Namespaces.h"
#include <AK/Array.h>
#include <AK/GenericLexer.h>
#include <AK/LexicalPath.h>
#include <AK/NumericLimits.h>
#include <AK/Queue.h>
#include <AK/RefPtr.h>
#include <AK/StringBuilder.h>
#include <AK/StringUtils.h>
#include <LibIDL/ExposedTo.h>
#include <LibIDL/Types.h>

namespace IDL {

template<typename ParameterType>
void generate_to_string(SourceGenerator& scoped_generator, ParameterType const& parameter, bool variadic, bool optional, Optional<ByteString> const& optional_default_value)
{
    auto is_utf16_string = parameter.type->name().contains("Utf16"sv);
    auto is_fly_string = parameter.extended_attributes.contains("FlyString"sv);

    if (is_utf16_string) {
        scoped_generator.set("string_type", is_fly_string ? "Utf16FlyString"sv : "Utf16String"sv);
        scoped_generator.set("string_suffix", "_utf16"sv);
    } else {
        scoped_generator.set("string_type", is_fly_string ? "FlyString"sv : "String"sv);
        scoped_generator.set("string_suffix", "_string"sv);
    }

    if (parameter.type->name().is_one_of("USVString"sv, "Utf16USVString"sv))
        scoped_generator.set("to_string", is_utf16_string ? "to_utf16_usv_string"sv : "to_usv_string"sv);
    else if (parameter.type->name() == "ByteString")
        scoped_generator.set("to_string", "to_byte_string"sv);
    else
        scoped_generator.set("to_string", is_utf16_string ? "to_utf16_string"sv : "to_string"sv);

    if (variadic) {
        scoped_generator.append(R"~~~(
    Vector<@string_type@> @cpp_name@;

    if (vm.argument_count() > @js_suffix@) {
        @cpp_name@.ensure_capacity(vm.argument_count() - @js_suffix@);

        for (size_t i = @js_suffix@; i < vm.argument_count(); ++i) {
            auto to_string_result = TRY(WebIDL::@to_string@(vm, vm.argument(i)));
            @cpp_name@.unchecked_append(move(to_string_result));
        }
    }
)~~~");
    } else if (!optional) {
        if (!parameter.type->is_nullable()) {
            scoped_generator.append(R"~~~(
    @string_type@ @cpp_name@;
    if (!@legacy_null_to_empty_string@ || !@js_name@@js_suffix@.is_null()) {
        @cpp_name@ = TRY(WebIDL::@to_string@(vm, @js_name@@js_suffix@));
    }
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    Optional<@string_type@> @cpp_name@;
    if (!@js_name@@js_suffix@.is_nullish())
        @cpp_name@ = TRY(WebIDL::@to_string@(vm, @js_name@@js_suffix@));
)~~~");
        }
    } else {
        bool may_be_null = !optional_default_value.has_value() || parameter.type->is_nullable() || optional_default_value.value() == "null";
        if (may_be_null) {
            scoped_generator.append(R"~~~(
    Optional<@string_type@> @cpp_name@;
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    @string_type@ @cpp_name@;
)~~~");
        }

        if (parameter.type->is_nullable()) {
            scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_undefined()) {
        if (!@js_name@@js_suffix@.is_null())
            @cpp_name@ = TRY(WebIDL::@to_string@(vm, @js_name@@js_suffix@));
    })~~~");
        } else {
            scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_undefined()) {
        if (!@legacy_null_to_empty_string@ || !@js_name@@js_suffix@.is_null())
            @cpp_name@ = TRY(WebIDL::@to_string@(vm, @js_name@@js_suffix@));
    })~~~");
        }

        if (!may_be_null) {
            scoped_generator.append(R"~~~( else {
        @cpp_name@ = @parameter.optional_default_value@@string_suffix@;
    }
)~~~");
        } else {
            scoped_generator.append(R"~~~(
)~~~");
        }
    }
}
void generate_from_integral(SourceGenerator& scoped_generator, IDL::Type const& type, bool const optional_integral_type);

template<typename ParameterType>
void generate_to_integral(SourceGenerator& scoped_generator, ParameterType const& parameter, bool optional, Optional<ByteString> optional_default_value)
{
    struct TypeMap {
        StringView idl_type;
        StringView cpp_type;
    };
    static constexpr auto idl_type_map = to_array<TypeMap>({
        { "boolean"sv, "bool"sv },
        { "byte"sv, "WebIDL::Byte"sv },
        { "octet"sv, "WebIDL::Octet"sv },
        { "short"sv, "WebIDL::Short"sv },
        { "unsigned short"sv, "WebIDL::UnsignedShort"sv },
        { "long"sv, "WebIDL::Long"sv },
        { "long long"sv, "WebIDL::LongLong"sv },
        { "unsigned long"sv, "WebIDL::UnsignedLong"sv },
        { "unsigned long long"sv, "WebIDL::UnsignedLongLong"sv },
    });

    auto it = find_if(idl_type_map.begin(), idl_type_map.end(), [&](auto const& entry) {
        return entry.idl_type == parameter.type->name();
    });

    VERIFY(it != idl_type_map.end());
    scoped_generator.set("cpp_type"sv, it->cpp_type);
    scoped_generator.set("enforce_range", parameter.extended_attributes.contains("EnforceRange") ? "Yes" : "No");
    scoped_generator.set("clamp", parameter.extended_attributes.contains("Clamp") ? "Yes" : "No");

    if (optional_default_value.has_value() && optional_default_value.value() == "null"sv)
        optional_default_value = {};

    if ((!optional && !parameter.type->is_nullable()) || optional_default_value.has_value()) {
        scoped_generator.append(R"~~~(
    @cpp_type@ @cpp_name@;
)~~~");
    } else {
        scoped_generator.append(R"~~~(
    Optional<@cpp_type@> @cpp_name@;
)~~~");
    }

    if (parameter.type->is_nullable()) {
        scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_null() && !@js_name@@js_suffix@.is_undefined())
)~~~");
    } else if (optional) {
        scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_undefined())
)~~~");
    }

    if (it->cpp_type == "bool"sv) {
        scoped_generator.append(R"~~~(
    @cpp_name@ = @js_name@@js_suffix@.to_boolean();
)~~~");
    } else {
        scoped_generator.append(R"~~~(
    @cpp_name@ = TRY(WebIDL::convert_to_int<@cpp_type@>(vm, @js_name@@js_suffix@, WebIDL::EnforceRange::@enforce_range@, WebIDL::Clamp::@clamp@));
)~~~");
    }

    if (optional_default_value.has_value()) {
        scoped_generator.append(R"~~~(
    else
        @cpp_name@ = static_cast<@cpp_type@>(@parameter.optional_default_value@);
)~~~");
    }
}

template<typename ParameterType>
void generate_to_cpp(SourceGenerator& generator, ParameterType& parameter, ByteString const& js_name, ByteString const& js_suffix, ByteString const& cpp_name, IDL::Interface const& interface, bool legacy_null_to_empty_string = false, bool optional = false, Optional<ByteString> optional_default_value = {}, bool variadic = false, size_t recursion_depth = 0)
{
    auto scoped_generator = generator.fork();
    auto acceptable_cpp_name = make_input_acceptable_cpp(cpp_name);
    auto explicit_null = parameter.extended_attributes.contains("ExplicitNull");
    scoped_generator.set("cpp_name", acceptable_cpp_name);
    scoped_generator.set("js_name", js_name);
    scoped_generator.set("js_suffix", js_suffix);
    scoped_generator.set("legacy_null_to_empty_string", legacy_null_to_empty_string ? "true" : "false");

    auto const& type = parameter.type;
    scoped_generator.set("parameter.type.name", type->name());

    if (!libweb_interface_namespaces.span().contains_slow(type->name())) {
        if (is_javascript_builtin(type))
            scoped_generator.set("parameter.type.name.normalized", ByteString::formatted("JS::{}", type->name()));
        else
            scoped_generator.set("parameter.type.name.normalized", type->name());
    } else {
        // e.g. Document.getSelection which returns Selection, which is in the Selection namespace.
        StringBuilder builder;
        builder.append(type->name());
        builder.append("::"sv);
        builder.append(type->name());
        scoped_generator.set("parameter.type.name.normalized", builder.to_byte_string());
    }

    scoped_generator.set("parameter.name", parameter.name);

    if (explicit_null) {
        if (!IDL::is_platform_object(*parameter.type)) {
            dbgln("Parameter marked [ExplicitNull] in interface {} must be a platform object", interface.name);
            VERIFY_NOT_REACHED();
        }

        if (!optional || !parameter.type->is_nullable()) {
            dbgln("Parameter marked [ExplicitNull] in interface {} must be an optional and nullable type", interface.name);
            VERIFY_NOT_REACHED();
        }
    }

    if (optional_default_value.has_value())
        scoped_generator.set("parameter.optional_default_value", *optional_default_value);

    // FIXME: Add support for optional, variadic, nullable and default values to all types
    if (parameter.type->is_string()) {
        generate_to_string(scoped_generator, parameter, variadic, optional, optional_default_value);
    } else if (parameter.type->is_boolean() || parameter.type->is_integer()) {
        generate_to_integral(scoped_generator, parameter, optional, optional_default_value);
    } else if (auto const* callback_interface = callback_interface_for_type(interface, parameter.type)) {
        scoped_generator.set("cpp_type", callback_interface->implemented_name);

        if (parameter.type->is_nullable()) {
            scoped_generator.append(R"~~~(
    @cpp_type@* @cpp_name@ = nullptr;
    if (!@js_name@@js_suffix@.is_nullish()) {
        if (!@js_name@@js_suffix@.is_object())
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObject, @js_name@@js_suffix@);

        auto callback_type = vm.heap().allocate<WebIDL::CallbackType>(@js_name@@js_suffix@.as_object(), HTML::incumbent_realm());
        @cpp_name@ = TRY(throw_dom_exception_if_needed(vm, [&] { return @cpp_type@::create(realm, callback_type); }));
    }
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_object())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObject, @js_name@@js_suffix@);

    auto callback_type = vm.heap().allocate<WebIDL::CallbackType>(@js_name@@js_suffix@.as_object(), HTML::incumbent_realm());
    auto @cpp_name@ = TRY(throw_dom_exception_if_needed(vm, [&] { return @cpp_type@::create(realm, callback_type); }));
)~~~");
        }
    } else if (IDL::is_platform_object(*parameter.type)) {
        if (!parameter.type->is_nullable()) {
            if (!optional) {
                scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_object() || !is<@parameter.type.name.normalized@>(@js_name@@js_suffix@.as_object()))
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "@parameter.type.name@");

    auto& @cpp_name@ = static_cast<@parameter.type.name.normalized@&>(@js_name@@js_suffix@.as_object());
)~~~");
            } else {
                scoped_generator.append(R"~~~(
    GC::Ptr<@parameter.type.name.normalized@> @cpp_name@;
    if (!@js_name@@js_suffix@.is_undefined()) {
        if (!@js_name@@js_suffix@.is_object() || !is<@parameter.type.name.normalized@>(@js_name@@js_suffix@.as_object()))
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "@parameter.type.name@");

        @cpp_name@ = static_cast<@parameter.type.name.normalized@&>(@js_name@@js_suffix@.as_object());
    }
)~~~");
            }
        } else {
            if (explicit_null) {
                scoped_generator.append(R"~~~(
    Optional<GC::Ptr<@parameter.type.name.normalized@>> @cpp_name@;
    if (maybe_@js_name@@js_suffix@.has_value()) {
        auto @js_name@@js_suffix@ = maybe_@js_name@@js_suffix@.release_value();
)~~~");
            } else {
                scoped_generator.append(R"~~~(
    GC::Ptr<@parameter.type.name.normalized@> @cpp_name@;
)~~~");
            }

            scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_nullish()) {
        if (!@js_name@@js_suffix@.is_object() || !is<@parameter.type.name.normalized@>(@js_name@@js_suffix@.as_object()))
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "@parameter.type.name@");

        @cpp_name@ = &static_cast<@parameter.type.name.normalized@&>(@js_name@@js_suffix@.as_object());
    }
)~~~");

            if (explicit_null) {
                scoped_generator.append(R"~~~(
    }
)~~~");
            }
        }
    } else if (parameter.type->is_floating_point()) {
        if (parameter.type->name() == "unrestricted float") {
            scoped_generator.set("parameter.type.name", "float");
        } else if (parameter.type->name() == "unrestricted double") {
            scoped_generator.set("parameter.type.name", "double");
        }

        bool is_wrapped_in_optional_type = false;
        if (!optional) {
            scoped_generator.append(R"~~~(
    @parameter.type.name@ @cpp_name@ = TRY(@js_name@@js_suffix@.to_double(vm));
)~~~");
        } else {
            if (optional_default_value.has_value() && optional_default_value != "null"sv) {
                scoped_generator.append(R"~~~(
    @parameter.type.name@ @cpp_name@;
)~~~");
            } else {
                is_wrapped_in_optional_type = true;
                scoped_generator.append(R"~~~(
    Optional<@parameter.type.name@> @cpp_name@;
)~~~");
            }
            scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_undefined())
        @cpp_name@ = TRY(@js_name@@js_suffix@.to_double(vm));
)~~~");
            if (optional_default_value.has_value() && optional_default_value.value() != "null"sv) {
                scoped_generator.append(R"~~~(
    else
        @cpp_name@ = @parameter.optional_default_value@;
)~~~");
            } else {
                scoped_generator.append(R"~~~(
)~~~");
            }
        }

        if (parameter.type->is_restricted_floating_point()) {
            if (is_wrapped_in_optional_type) {
                scoped_generator.append(R"~~~(
    if (@cpp_name@.has_value() && (isinf(*@cpp_name@) || isnan(*@cpp_name@))) {
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::InvalidRestrictedFloatingPointParameter, "@parameter.name@");
    }
    )~~~");
            } else {
                scoped_generator.append(R"~~~(
    if (isinf(@cpp_name@) || isnan(@cpp_name@)) {
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::InvalidRestrictedFloatingPointParameter, "@parameter.name@");
    }
    )~~~");
            }
        }
    } else if (parameter.type->name() == "Promise") {
        // https://webidl.spec.whatwg.org/#js-promise
        scoped_generator.append(R"~~~(
    // 1. Let promiseCapability be ? NewPromiseCapability(%Promise%).
    auto promise_capability = TRY(JS::new_promise_capability(vm, realm.intrinsics().promise_constructor()));
    // 2. Perform ? Call(promiseCapability.[[Resolve]], undefined, « V »).
    TRY(JS::call(vm, *promise_capability->resolve(), JS::js_undefined(), @js_name@@js_suffix@));
    // 3. Return promiseCapability.
    auto @cpp_name@ = GC::make_root(promise_capability);
)~~~");
    } else if (parameter.type->name() == "object") {
        // https://webidl.spec.whatwg.org/#js-object
        // 1. If V is not an Object, then throw a TypeError.
        // 2. Return the IDL object value that is a reference to the same object as V.
        if (parameter.type->is_nullable()) {
            scoped_generator.append(R"~~~(
    Optional<GC::Root<JS::Object>> @cpp_name@;
    if (!@js_name@@js_suffix@.is_null() && !@js_name@@js_suffix@.is_undefined()) {
        if (!@js_name@@js_suffix@.is_object())
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObject, @js_name@@js_suffix@);
        @cpp_name@ = GC::make_root(@js_name@@js_suffix@.as_object());
    }
)~~~");
        } else if (optional) {
            scoped_generator.append(R"~~~(
    Optional<GC::Root<JS::Object>> @cpp_name@;
    if (!@js_name@@js_suffix@.is_undefined()) {
        if (!@js_name@@js_suffix@.is_object())
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObject, @js_name@@js_suffix@);
        @cpp_name@ = GC::make_root(@js_name@@js_suffix@.as_object());
    }
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_object())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObject, @js_name@@js_suffix@);
    auto @cpp_name@ = GC::make_root(@js_name@@js_suffix@.as_object());
)~~~");
        }
    } else if (is_javascript_builtin(parameter.type) || parameter.type->name() == "BufferSource"sv) {
        if (optional) {
            scoped_generator.append(R"~~~(
    Optional<GC::Root<WebIDL::BufferSource>> @cpp_name@;
    if (!@js_name@@js_suffix@.is_undefined()) {
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    GC::Root<WebIDL::BufferSource> @cpp_name@;
)~~~");
        }
        scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_object() || !(is<JS::TypedArrayBase>(@js_name@@js_suffix@.as_object()) || is<JS::ArrayBuffer>(@js_name@@js_suffix@.as_object()) || is<JS::DataView>(@js_name@@js_suffix@.as_object())))
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "@parameter.type.name@");

    @cpp_name@ = GC::make_root(realm.create<WebIDL::BufferSource>(@js_name@@js_suffix@.as_object()));
)~~~");

        if (optional) {
            scoped_generator.append(R"~~~(
        }
)~~~");
        }

    } else if (parameter.type->name() == "ArrayBufferView") {
        scoped_generator.append(R"~~~(
    GC::Root<WebIDL::ArrayBufferView> @cpp_name@;
)~~~");
        if (parameter.type->is_nullable()) {
            scoped_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_null() && !@js_name@@js_suffix@.is_undefined()) {
)~~~");
        }

        scoped_generator.append(R"~~~(
        if (!@js_name@@js_suffix@.is_object() || !(is<JS::TypedArrayBase>(@js_name@@js_suffix@.as_object()) || is<JS::DataView>(@js_name@@js_suffix@.as_object())))
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "@parameter.type.name@");

        @cpp_name@ = GC::make_root(realm.create<WebIDL::ArrayBufferView>(@js_name@@js_suffix@.as_object()));
)~~~");

        if (parameter.type->is_nullable()) {
            scoped_generator.append(R"~~~(
    }
)~~~");
        }
        if (optional) {
            scoped_generator.append(R"~~~(
        }
)~~~");
        }
    } else if (parameter.type->name() == "any") {
        if (variadic) {
            scoped_generator.append(R"~~~(
    GC::RootVector<JS::Value> @cpp_name@ { vm.heap() };

    if (vm.argument_count() > @js_suffix@) {
        @cpp_name@.ensure_capacity(vm.argument_count() - @js_suffix@);

        for (size_t i = @js_suffix@; i < vm.argument_count(); ++i)
            @cpp_name@.unchecked_append(vm.argument(i));
    }
)~~~");
        } else if (!optional) {
            scoped_generator.append(R"~~~(
    auto @cpp_name@ = @js_name@@js_suffix@;
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    JS::Value @cpp_name@ = JS::js_undefined();
    if (!@js_name@@js_suffix@.is_undefined())
        @cpp_name@ = @js_name@@js_suffix@;
)~~~");
            if (optional_default_value.has_value()) {
                if (optional_default_value == "null") {
                    scoped_generator.append(R"~~~(
    else
        @cpp_name@ = JS::js_null();
)~~~");
                } else if (optional_default_value->to_number<int>().has_value() || optional_default_value->to_number<unsigned>().has_value()) {
                    scoped_generator.append(R"~~~(
    else
        @cpp_name@ = JS::Value(@parameter.optional_default_value@);
)~~~");
                } else {
                    TODO();
                }
            }
        }
    } else if (interface.enumerations.contains(parameter.type->name())) {
        auto enum_generator = scoped_generator.fork();
        auto& enumeration = interface.enumerations.find(parameter.type->name())->value;
        StringView enum_member_name;
        if (optional_default_value.has_value()) {
            VERIFY(optional_default_value->length() >= 2 && (*optional_default_value)[0] == '"' && (*optional_default_value)[optional_default_value->length() - 1] == '"');
            enum_member_name = optional_default_value->substring_view(1, optional_default_value->length() - 2);
        } else {
            enum_member_name = enumeration.first_member;
        }
        auto default_value_cpp_name = enumeration.translated_cpp_names.get(enum_member_name);
        VERIFY(default_value_cpp_name.has_value());
        enum_generator.set("enum.default.cpp_value", *default_value_cpp_name);
        enum_generator.set("js_name.as_string", ByteString::formatted("{}{}_string", enum_generator.get("js_name"sv), enum_generator.get("js_suffix"sv)));
        enum_generator.append(R"~~~(
    @parameter.type.name.normalized@ @cpp_name@ { @parameter.type.name.normalized@::@enum.default.cpp_value@ };
)~~~");

        if (optional) {
            enum_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_undefined()) {
)~~~");
        }

        enum_generator.append(R"~~~(
    auto @js_name.as_string@ = TRY(@js_name@@js_suffix@.to_string(vm));
)~~~");
        auto first = true;
        VERIFY(enumeration.translated_cpp_names.size() >= 1);
        for (auto& it : enumeration.translated_cpp_names) {
            enum_generator.set("enum.alt.name", it.key);
            enum_generator.set("enum.alt.value", it.value);
            enum_generator.set("else", first ? "" : "else ");
            first = false;

            enum_generator.append(R"~~~(
    @else@if (@js_name.as_string@ == "@enum.alt.name@"sv)
        @cpp_name@ = @parameter.type.name.normalized@::@enum.alt.value@;
)~~~");
        }

        // NOTE: Attribute setters return undefined instead of throwing when the string doesn't match an enum value.
        if constexpr (!IsSame<Attribute, RemoveConst<ParameterType>>) {
            enum_generator.append(R"~~~(
    else
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::InvalidEnumerationValue, @js_name.as_string@, "@parameter.type.name@");
)~~~");
        } else {
            enum_generator.append(R"~~~(
    else
        return JS::js_undefined();
)~~~");
        }

        if (optional) {
            enum_generator.append(R"~~~(
    }
)~~~");
        }
    } else if (interface.dictionaries.contains(parameter.type->name())) {
        if (optional_default_value.has_value() && optional_default_value != "{}")
            TODO();
        auto dictionary_generator = scoped_generator.fork();
        dictionary_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_nullish() && !@js_name@@js_suffix@.is_object())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObjectOfType, "@parameter.type.name@");

    @parameter.type.name.normalized@ @cpp_name@ {};
)~~~");
        auto current_dictionary_name = parameter.type->name();
        auto* current_dictionary = &interface.dictionaries.find(current_dictionary_name)->value;
        // FIXME: This (i) is a hack to make sure we don't generate duplicate variable names.
        static auto i = 0;
        while (true) {
            Vector<DictionaryMember> members;
            for (auto& member : current_dictionary->members)
                members.append(member);

            if (interface.partial_dictionaries.contains(current_dictionary_name)) {
                auto& partial_dictionaries = interface.partial_dictionaries.find(current_dictionary_name)->value;
                for (auto& partial_dictionary : partial_dictionaries)
                    for (auto& member : partial_dictionary.members)
                        members.append(member);
            }

            for (auto& member : members) {
                dictionary_generator.set("member_key", member.name);
                auto member_js_name = make_input_acceptable_cpp(member.name.to_snakecase());
                auto member_value_name = ByteString::formatted("{}_value_{}", member_js_name, i);
                auto member_property_value_name = ByteString::formatted("{}_property_value_{}", member_js_name, i);
                dictionary_generator.set("member_name", member_js_name);
                dictionary_generator.set("member_value_name", member_value_name);
                dictionary_generator.set("member_property_value_name", member_property_value_name);
                dictionary_generator.append(R"~~~(
    auto @member_property_value_name@ = JS::js_undefined();
    if (@js_name@@js_suffix@.is_object())
        @member_property_value_name@ = TRY(@js_name@@js_suffix@.as_object().get("@member_key@"_utf16_fly_string));
)~~~");
                if (member.required) {
                    dictionary_generator.append(R"~~~(
    if (@member_property_value_name@.is_undefined())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::MissingRequiredProperty, "@member_key@");
)~~~");
                } else if (!member.default_value.has_value()) {
                    // Assume struct member is Optional<T> and _don't_ assign the generated default
                    // value (e.g. first enum member) when the dictionary member is optional (i.e.
                    // no `required` and doesn't have a default value).
                    // This is needed so that "dictionary has member" checks work as expected.
                    dictionary_generator.append(R"~~~(
    if (!@member_property_value_name@.is_undefined()) {
)~~~");
                }

                generate_to_cpp(dictionary_generator, member, member_property_value_name, "", member_value_name, interface, member.extended_attributes.contains("LegacyNullToEmptyString"), !member.required, member.default_value);

                bool may_be_null = !optional_default_value.has_value() || parameter.type->is_nullable() || optional_default_value.value() == "null";

                // Required dictionary members cannot be null.
                may_be_null &= !member.required && !member.default_value.has_value();

                if (member.type->is_string() && optional && may_be_null) {
                    dictionary_generator.append(R"~~~(
    if (@member_value_name@.has_value())
        @cpp_name@.@member_name@ = @member_value_name@.release_value();
)~~~");
                } else {
                    dictionary_generator.append(R"~~~(
    @cpp_name@.@member_name@ = @member_value_name@;
)~~~");
                }
                if (!member.required && !member.default_value.has_value()) {
                    dictionary_generator.append(R"~~~(
    }
)~~~");
                }
                i++;
            }
            if (current_dictionary->parent_name.is_empty())
                break;
            VERIFY(interface.dictionaries.contains(current_dictionary->parent_name));
            current_dictionary_name = current_dictionary->parent_name;
            current_dictionary = &interface.dictionaries.find(current_dictionary_name)->value;
        }
    } else if (interface.callback_functions.contains(parameter.type->name())) {
        // https://webidl.spec.whatwg.org/#es-callback-function

        auto callback_function_generator = scoped_generator.fork();
        auto& callback_function = interface.callback_functions.find(parameter.type->name())->value;

        if (callback_function.return_type->is_parameterized() && callback_function.return_type->name() == "Promise")
            callback_function_generator.set("operation_returns_promise", "WebIDL::OperationReturnsPromise::Yes");
        else
            callback_function_generator.set("operation_returns_promise", "WebIDL::OperationReturnsPromise::No");

        // An ECMAScript value V is converted to an IDL callback function type value by running the following algorithm:
        // 1. If the result of calling IsCallable(V) is false and the conversion to an IDL value is not being performed due to V being assigned to an attribute whose type is a nullable callback function that is annotated with [LegacyTreatNonObjectAsNull], then throw a TypeError.
        if (!parameter.type->is_nullable() && !callback_function.is_legacy_treat_non_object_as_null) {
            callback_function_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_function()
)~~~");
            if (optional)
                callback_function_generator.append("&& !@js_name@@js_suffix@.is_undefined()");
            callback_function_generator.append(R"~~~()
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAFunction, @js_name@@js_suffix@);
)~~~");
        }
        // 2. Return the IDL callback function type value that represents a reference to the same object that V represents, with the incumbent realm as the callback context.
        if (optional || parameter.type->is_nullable() || callback_function.is_legacy_treat_non_object_as_null) {
            callback_function_generator.append(R"~~~(
    GC::Ptr<WebIDL::CallbackType> @cpp_name@;
    if (@js_name@@js_suffix@.is_object())
        @cpp_name@ = vm.heap().allocate<WebIDL::CallbackType>(@js_name@@js_suffix@.as_object(), HTML::incumbent_realm(), @operation_returns_promise@);
)~~~");
            // FIXME: Handle default value for optional parameter here.
        } else {
            callback_function_generator.append(R"~~~(
    auto @cpp_name@ = vm.heap().allocate<WebIDL::CallbackType>(@js_name@@js_suffix@.as_object(), HTML::incumbent_realm(), @operation_returns_promise@);
)~~~");
        }
    } else if (parameter.type->name().is_one_of("sequence"sv, "FrozenArray"sv)) {
        // https://webidl.spec.whatwg.org/#js-sequence
        // https://webidl.spec.whatwg.org/#js-frozen-array

        auto sequence_generator = scoped_generator.fork();
        auto& parameterized_type = as<IDL::ParameterizedType>(*parameter.type);
        sequence_generator.set("recursion_depth", ByteString::number(recursion_depth));

        // A JavaScript value V is converted to an IDL sequence<T> value as follows:
        // 1. If V is not an Object, throw a TypeError.
        // 2. Let method be ? GetMethod(V, %Symbol.iterator%).
        // 3. If method is undefined, throw a TypeError.
        // 4. Return the result of creating a sequence from V and method.

        // A JavaScript value V is converted to an IDL FrozenArray<T> value by running the following algorithm:
        // 1. Let values be the result of converting V to IDL type sequence<T>.
        // 2. Return the result of creating a frozen array from values.

        if (optional || parameter.type->is_nullable()) {
            auto sequence_cpp_type = idl_type_name_to_cpp_type(parameterized_type.parameters().first(), interface);
            sequence_generator.set("sequence.type", sequence_cpp_type.name);
            sequence_generator.set("sequence.storage_type", sequence_storage_type_to_cpp_storage_type_name(sequence_cpp_type.sequence_storage_type));

            if (!optional_default_value.has_value()) {
                sequence_generator.append(R"~~~(
    Optional<@sequence.storage_type@<@sequence.type@>> @cpp_name@;
)~~~");
            } else {
                if (optional_default_value != "[]")
                    TODO();

                if (sequence_cpp_type.sequence_storage_type == IDL::SequenceStorageType::Vector) {
                    sequence_generator.append(R"~~~(
    @sequence.storage_type@<@sequence.type@> @cpp_name@;
)~~~");
                } else {
                    sequence_generator.append(R"~~~(
    @sequence.storage_type@<@sequence.type@> @cpp_name@ { vm.heap() };
)~~~");
                }
            }

            if (optional) {
                sequence_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_undefined()) {
)~~~");
            } else {
                sequence_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_nullish()) {
)~~~");
            }
        }

        sequence_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_object())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObject, @js_name@@js_suffix@);

    auto @js_name@@js_suffix@_iterator_method@recursion_depth@ = TRY(@js_name@@js_suffix@.get_method(vm, vm.well_known_symbol_iterator()));
    if (!@js_name@@js_suffix@_iterator_method@recursion_depth@)
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotIterable, @js_name@@js_suffix@);
)~~~");

        parameterized_type.generate_sequence_from_iterable(sequence_generator, ByteString::formatted("{}{}", acceptable_cpp_name, optional || parameter.type->is_nullable() ? "_non_optional" : ""), ByteString::formatted("{}{}", js_name, js_suffix), ByteString::formatted("{}{}_iterator_method{}", js_name, js_suffix, recursion_depth), interface, recursion_depth + 1);

        if (optional || parameter.type->is_nullable()) {
            sequence_generator.append(R"~~~(
        @cpp_name@ = move(@cpp_name@_non_optional);
    }
)~~~");
        }
    } else if (parameter.type->name() == "record") {
        // https://webidl.spec.whatwg.org/#es-record

        auto record_generator = scoped_generator.fork();
        auto& parameterized_type = as<IDL::ParameterizedType>(*parameter.type);
        record_generator.set("recursion_depth", ByteString::number(recursion_depth));

        // A record can only have two types: key type and value type.
        VERIFY(parameterized_type.parameters().size() == 2);

        // A record only allows the key to be a string.
        VERIFY(parameterized_type.parameters()[0]->is_string());

        // An ECMAScript value O is converted to an IDL record<K, V> value as follows:
        // 1. If Type(O) is not Object, throw a TypeError.
        // 2. Let result be a new empty instance of record<K, V>.
        // 3. Let keys be ? O.[[OwnPropertyKeys]]().
        // 4. For each key of keys:
        //    1. Let desc be ? O.[[GetOwnProperty]](key).
        //    2. If desc is not undefined and desc.[[Enumerable]] is true:
        //       1. Let typedKey be key converted to an IDL value of type K.
        //       2. Let value be ? Get(O, key).
        //       3. Let typedValue be value converted to an IDL value of type V.
        //       4. Set result[typedKey] to typedValue.
        // 5. Return result.

        auto record_cpp_type = IDL::idl_type_name_to_cpp_type(parameterized_type, interface);
        record_generator.set("record.type", record_cpp_type.name);

        // If this is a recursive call to generate_to_cpp, assume that the caller has already handled converting the JS value to an object for us.
        // This affects record types in unions for example.
        if (recursion_depth == 0) {
            record_generator.append(R"~~~(
    if (!@js_name@@js_suffix@.is_object())
        return vm.throw_completion<JS::TypeError>(JS::ErrorType::NotAnObject, @js_name@@js_suffix@);

    auto& @js_name@@js_suffix@_object = @js_name@@js_suffix@.as_object();
)~~~");
        }

        record_generator.append(R"~~~(
    @record.type@ @cpp_name@;

    auto record_keys@recursion_depth@ = TRY(@js_name@@js_suffix@_object.internal_own_property_keys());

    for (auto& key@recursion_depth@ : record_keys@recursion_depth@) {
        auto property_key@recursion_depth@ = MUST(JS::PropertyKey::from_value(vm, key@recursion_depth@));

        auto descriptor@recursion_depth@ = TRY(@js_name@@js_suffix@_object.internal_get_own_property(property_key@recursion_depth@));

        if (!descriptor@recursion_depth@.has_value() || !descriptor@recursion_depth@->enumerable.has_value() || !descriptor@recursion_depth@->enumerable.value())
            continue;
)~~~");

        IDL::Parameter key_parameter { .type = parameterized_type.parameters()[0], .name = acceptable_cpp_name, .optional_default_value = {}, .extended_attributes = {} };
        generate_to_cpp(record_generator, key_parameter, "key", ByteString::number(recursion_depth), ByteString::formatted("typed_key{}", recursion_depth), interface, false, false, {}, false, recursion_depth + 1);

        record_generator.append(R"~~~(
        auto value@recursion_depth@ = TRY(@js_name@@js_suffix@_object.get(property_key@recursion_depth@));
)~~~");

        // FIXME: Record value types should be TypeWithExtendedAttributes, which would allow us to get [LegacyNullToEmptyString] here.
        IDL::Parameter value_parameter { .type = parameterized_type.parameters()[1], .name = acceptable_cpp_name, .optional_default_value = {}, .extended_attributes = {} };
        generate_to_cpp(record_generator, value_parameter, "value", ByteString::number(recursion_depth), ByteString::formatted("typed_value{}", recursion_depth), interface, false, false, {}, false, recursion_depth + 1);

        record_generator.append(R"~~~(
        @cpp_name@.set(typed_key@recursion_depth@, typed_value@recursion_depth@);
    }
)~~~");
    } else if (is<IDL::UnionType>(*parameter.type)) {
        // https://webidl.spec.whatwg.org/#es-union

        auto union_generator = scoped_generator.fork();

        auto& union_type = as<IDL::UnionType>(*parameter.type);
        union_generator.set("union_type", union_type_to_variant(union_type, interface));
        union_generator.set("recursion_depth", ByteString::number(recursion_depth));

        // NOTE: This is handled out here as we need the dictionary conversion code for the {} optional default value.
        // 3. Let types be the flattened member types of the union type.
        auto types = union_type.flattened_member_types();

        RefPtr<Type const> dictionary_type;
        for (auto& dictionary : interface.dictionaries) {
            for (auto& type : types) {
                if (type->name() == dictionary.key) {
                    dictionary_type = type;
                    break;
                }
            }

            if (dictionary_type)
                break;
        }

        if (dictionary_type) {
            auto dictionary_generator = union_generator.fork();
            dictionary_generator.set("dictionary.type", dictionary_type->name());

            // The lambda must take the JS::Value to convert as a parameter instead of capturing it in order to support union types being variadic.
            dictionary_generator.append(R"~~~(
    auto @js_name@@js_suffix@_to_dictionary = [&vm, &realm](JS::Value @js_name@@js_suffix@) -> JS::ThrowCompletionOr<@dictionary.type@> {
        // This might be unused.
        (void)realm;
)~~~");

            IDL::Parameter dictionary_parameter { .type = *dictionary_type, .name = acceptable_cpp_name, .optional_default_value = {}, .extended_attributes = {} };
            generate_to_cpp(dictionary_generator, dictionary_parameter, js_name, js_suffix, "dictionary_union_type"sv, interface, false, false, {}, false, recursion_depth + 1);

            dictionary_generator.append(R"~~~(
        return dictionary_union_type;
    };
)~~~");
        }

        // A lambda is used because Variants without "Empty" can't easily be default initialized.
        // Plus, this would require the user of union types to always accept a Variant with an Empty type.

        // Additionally, it handles the case of unconditionally throwing a TypeError at the end if none of the types match.
        // This is because we cannot unconditionally throw in generate_to_cpp as generate_to_cpp is supposed to assign to a variable and then continue.
        // Note that all the other types only throw on a condition.

        // The lambda must take the JS::Value to convert as a parameter instead of capturing it in order to support union types being variadic.

        StringBuilder to_variant_captures;
        to_variant_captures.append("&vm, &realm"sv);

        if (dictionary_type)
            to_variant_captures.append(ByteString::formatted(", &{}{}_to_dictionary", js_name, js_suffix));

        union_generator.set("to_variant_captures", to_variant_captures.to_byte_string());

        union_generator.append(R"~~~(
    auto @js_name@@js_suffix@_to_variant = [@to_variant_captures@](JS::Value @js_name@@js_suffix@) -> JS::ThrowCompletionOr<@union_type@> {
        // These might be unused.
        (void)vm;
        (void)realm;
)~~~");

        // 1. If the union type includes undefined and V is undefined, then return the unique undefined value.
        if (union_type.includes_undefined()) {
            scoped_generator.append(R"~~~(
        if (@js_name@@js_suffix@.is_undefined())
            return Empty {};
)~~~");
        }

        // FIXME: 2. If the union type includes a nullable type and V is null or undefined, then return the IDL value null.
        if (union_type.includes_nullable_type()) {
            // Implement me
        } else if (dictionary_type) {
            // 4. If V is null or undefined, then
            //    4.1 If types includes a dictionary type, then return the result of converting V to that dictionary type.
            union_generator.append(R"~~~(
        if (@js_name@@js_suffix@.is_nullish())
            return @union_type@ { TRY(@js_name@@js_suffix@_to_dictionary(@js_name@@js_suffix@)) };
)~~~");
        }

        bool includes_object = false;
        for (auto& type : types) {
            if (type->name() == "object") {
                includes_object = true;
                break;
            }
        }

        // FIXME: Don't generate this if the union type doesn't include any object types.
        union_generator.append(R"~~~(
        if (@js_name@@js_suffix@.is_object()) {
            [[maybe_unused]] auto& @js_name@@js_suffix@_object = @js_name@@js_suffix@.as_object();
)~~~");

        bool includes_platform_object = false;
        for (auto& type : types) {
            if (IDL::is_platform_object(type)) {
                includes_platform_object = true;
                break;
            }
        }

        if (includes_platform_object) {
            // 5. If V is a platform object, then:
            union_generator.append(R"~~~(
            if (is<PlatformObject>(@js_name@@js_suffix@_object)) {
)~~~");

            // NOTE: This codegen assumes that all union types are cells or values we can create a handle for.

            //    1. If types includes an interface type that V implements, then return the IDL value that is a reference to the object V.
            for (auto& type : types) {
                if (!IDL::is_platform_object(type))
                    continue;

                auto union_platform_object_type_generator = union_generator.fork();
                union_platform_object_type_generator.set("platform_object_type", type->name());

                union_platform_object_type_generator.append(R"~~~(
                if (auto* @js_name@@js_suffix@_result = as_if<@platform_object_type@>(@js_name@@js_suffix@_object))
                    return GC::make_root(*@js_name@@js_suffix@_result);
)~~~");
            }

            //    2. If types includes object, then return the IDL value that is a reference to the object V.
            if (includes_object) {
                union_generator.append(R"~~~(
                return GC::make_root(@js_name@@js_suffix@_object);
)~~~");
            }

            union_generator.append(R"~~~(
            }
)~~~");
        }

        bool includes_window_proxy = false;
        for (auto& type : types) {
            if (type->name() == "WindowProxy"sv) {
                includes_window_proxy = true;
                break;
            }
        }

        if (includes_window_proxy) {
            union_generator.append(R"~~~(
            if (auto* @js_name@@js_suffix@_result = as_if<WindowProxy>(@js_name@@js_suffix@_object))
                return GC::make_root(*@js_name@@js_suffix@_result);
)~~~");
        }

        // Note: This covers steps 6-8 for when Buffersource is in a union with a type other than "object".
        //       Since in that case, the return type would be Handle<BufferSource>, and not Handle<Object>.
        if (any_of(types, [](auto const& type) { return type->name() == "BufferSource"; }) && !includes_object) {
            union_generator.append(R"~~~(
            if (is<JS::ArrayBuffer>(@js_name@@js_suffix@_object) || is<JS::DataView>(@js_name@@js_suffix@_object) || is<JS::TypedArrayBase>(@js_name@@js_suffix@_object)) {
                GC::Ref<WebIDL::BufferSource> source_object = realm.create<WebIDL::BufferSource>(@js_name@@js_suffix@_object);
                return GC::make_root(source_object);
            }
)~~~");
        }

        // 6. If Type(V) is Object and V has an [[ArrayBufferData]] internal slot, then
        //    1. If types includes ArrayBuffer, then return the result of converting V to ArrayBuffer.
        //    2. If types includes object, then return the IDL value that is a reference to the object V.
        if (any_of(types, [](auto const& type) { return type->name() == "ArrayBuffer"; }) || includes_object) {
            union_generator.append(R"~~~(
            if (is<JS::ArrayBuffer>(@js_name@@js_suffix@_object))
                return GC::make_root(@js_name@@js_suffix@_object);
)~~~");
        }

        // 7. If Type(V) is Object and V has a [[DataView]] internal slot, then:
        //    1. If types includes DataView, then return the result of converting V to DataView.
        //    2. If types includes object, then return the IDL value that is a reference to the object V.
        if (any_of(types, [](auto const& type) { return type->name() == "DataView"; }) || includes_object) {
            union_generator.append(R"~~~(
            if (is<JS::DataView>(@js_name@@js_suffix@_object))
                return GC::make_root(@js_name@@js_suffix@_object);
)~~~");
        }

        // 8. If Type(V) is Object and V has a [[TypedArrayName]] internal slot, then:
        //    1. If types includes a typed array type whose name is the value of V’s [[TypedArrayName]] internal slot, then return the result of converting V to that type.
        //    2. If types includes object, then return the IDL value that is a reference to the object V.
        auto typed_array_name = types.find_if([](auto const& type) {
            return type->name().is_one_of("Int8Array"sv, "Int16Array"sv, "Int32Array"sv, "Uint8Array"sv, "Uint16Array"sv, "Uint32Array"sv, "Uint8ClampedArray"sv, "BigInt64Array"sv, "BigUint64Array"sv, "Float16Array"sv, "Float32Array"sv, "Float64Array"sv);
        });

        if (typed_array_name != types.end()) {
            union_generator.set("typed_array_type", (*typed_array_name)->name());
            union_generator.append(R"~~~(
            if (auto* typed_array = as_if<JS::@typed_array_type@>(@js_name@@js_suffix@_object))
                return GC::make_root(*typed_array);
)~~~");
        } else if (includes_object) {
            union_generator.append(R"~~~(
            if (is<JS::TypedArrayBase>(@js_name@@js_suffix@_object))
                return GC::make_root(@js_name@@js_suffix@_object);
)~~~");
        }

        // 9. If IsCallable(V) is true, then:
        //     1. If types includes a callback function type, then return the result of converting V to that callback function type.
        //     2. If types includes object, then return the IDL value that is a reference to the object V.
        bool includes_callable = false;
        for (auto const& type : types) {
            if (type->name() == "Function"sv) {
                includes_callable = true;
                break;
            }
        }

        if (includes_callable) {
            union_generator.append(R"~~~(
            if (@js_name@@js_suffix@_object.is_function())
                return vm.heap().allocate<WebIDL::CallbackType>(@js_name@@js_suffix@.as_function(), HTML::incumbent_realm());
)~~~");
        }

        // 10. If Type(V) is Object, then:
        //     1. If types includes a sequence type, then:
        RefPtr<IDL::ParameterizedType const> sequence_type;
        for (auto& type : types) {
            if (type->name() == "sequence") {
                sequence_type = as<IDL::ParameterizedType>(*type);
                break;
            }
        }

        if (sequence_type) {
            // 1. Let method be ? GetMethod(V, @@iterator).
            union_generator.append(R"~~~(
        auto method = TRY(@js_name@@js_suffix@.get_method(vm, vm.well_known_symbol_iterator()));
)~~~");

            // 2. If method is not undefined, return the result of creating a sequence of that type from V and method.
            union_generator.append(R"~~~(
        if (method) {
)~~~");

            sequence_type->generate_sequence_from_iterable(union_generator, acceptable_cpp_name, ByteString::formatted("{}{}", js_name, js_suffix), "method", interface, recursion_depth + 1);

            union_generator.append(R"~~~(

            return @cpp_name@;
        }
)~~~");
        }

        // FIXME: 2. If types includes a frozen array type, then
        //           1. Let method be ? GetMethod(V, @@iterator).
        //           2. If method is not undefined, return the result of creating a frozen array of that type from V and method.

        // 3. If types includes a dictionary type, then return the result of converting V to that dictionary type.
        if (dictionary_type) {
            union_generator.append(R"~~~(
        return @union_type@ { TRY(@js_name@@js_suffix@_to_dictionary(@js_name@@js_suffix@)) };
)~~~");
        }

        // 4. If types includes a record type, then return the result of converting V to that record type.
        RefPtr<IDL::ParameterizedType const> record_type;
        for (auto& type : types) {
            if (type->name() == "record") {
                record_type = as<IDL::ParameterizedType>(*type);
                break;
            }
        }

        if (record_type) {
            IDL::Parameter record_parameter { .type = *record_type, .name = acceptable_cpp_name, .optional_default_value = {}, .extended_attributes = {} };
            generate_to_cpp(union_generator, record_parameter, js_name, js_suffix, "record_union_type"sv, interface, false, false, {}, false, recursion_depth + 1);

            union_generator.append(R"~~~(
        return record_union_type;
)~~~");
        }

        // 5. If types includes a callback interface type, then return the result of converting V to that callback interface type.
        for (auto& type : types) {
            if (!callback_interface_for_type(interface, type))
                continue;

            IDL::Parameter callback_interface_parameter { .type = *type, .name = acceptable_cpp_name, .optional_default_value = {}, .extended_attributes = {} };
            generate_to_cpp(union_generator, callback_interface_parameter, js_name, js_suffix, "callback_interface_union_type"sv, interface, false, false, {}, false, recursion_depth + 1);

            union_generator.append(R"~~~(
        return callback_interface_union_type;
)~~~");
            break;
        }

        // 6. If types includes object, then return the IDL value that is a reference to the object V.
        if (includes_object) {
            union_generator.append(R"~~~(
        return @js_name@@js_suffix@_object;
)~~~");
        }

        // End of is_object.
        union_generator.append(R"~~~(
        }
)~~~");

        // 11. If Type(V) is Boolean, then:
        //     1. If types includes boolean, then return the result of converting V to boolean.
        bool includes_boolean = false;
        for (auto& type : types) {
            if (type->name() == "boolean") {
                includes_boolean = true;
                break;
            }
        }

        if (includes_boolean) {
            union_generator.append(R"~~~(
        if (@js_name@@js_suffix@.is_boolean())
            return @union_type@ { @js_name@@js_suffix@.as_bool() };
)~~~");
        }

        RefPtr<IDL::Type const> numeric_type;
        for (auto& type : types) {
            if (type->is_numeric()) {
                numeric_type = type;
                break;
            }
        }

        // 12. If Type(V) is Number, then:
        //     1. If types includes a numeric type, then return the result of converting V to that numeric type.
        if (numeric_type) {
            union_generator.append(R"~~~(
        if (@js_name@@js_suffix@.is_number()) {
)~~~");
            // NOTE: generate_to_cpp doesn't use the parameter name.
            // NOTE: generate_to_cpp will use to_{u32,etc.} which uses to_number internally and will thus use TRY, but it cannot throw as we know we are dealing with a number.
            IDL::Parameter idl_parameter { .type = *numeric_type, .name = parameter.name, .optional_default_value = {}, .extended_attributes = {} };
            generate_to_cpp(union_generator, idl_parameter, js_name, js_suffix, ByteString::formatted("{}{}_number", js_name, js_suffix), interface, false, false, {}, false, recursion_depth + 1);

            union_generator.append(R"~~~(
            return { @js_name@@js_suffix@_number };
        }
)~~~");
        }

        // 13. If Type(V) is BigInt, then:
        //     1. If types includes bigint, then return the result of converting V to bigint
        bool includes_bigint = false;
        for (auto& type : types) {
            if (type->name() == "bigint") {
                includes_bigint = true;
                break;
            }
        }

        if (includes_bigint) {
            union_generator.append(R"~~~(
        if (@js_name@@js_suffix@.is_bigint())
            return @js_name@@js_suffix@.as_bigint();
)~~~");
        }

        bool includes_enumeration = false;
        for (auto& type : types) {
            if (interface.enumerations.contains(type->name())) {
                includes_enumeration = true;
                break;
            }
        }

        // If V is a string and types includes an enumeration type, attempt to convert the string to that enumeration.
        // Example: Libraries/LibWeb/WebAudio/AudioContext.idl (AudioContextOptions.latencyHint: (AudioContextLatencyCategory or double)).
        if (includes_enumeration) {
            union_generator.append(R"~~~(
        if (@js_name@@js_suffix@.is_string()) {
            auto @js_name@@js_suffix@_enum_string = TRY(@js_name@@js_suffix@.to_string(vm));
)~~~");

            for (auto& type : types) {
                if (!interface.enumerations.contains(type->name()))
                    continue;

                auto& enumeration = interface.enumerations.find(type->name())->value;
                auto enum_type = IDL::idl_type_name_to_cpp_type(*type, interface);

                auto enum_generator = union_generator.fork();
                enum_generator.set("enum.type", enum_type.name);

                for (auto& it : enumeration.translated_cpp_names) {
                    enum_generator.set("enum.alt.name", it.key);
                    enum_generator.set("enum.alt.value", it.value);
                    enum_generator.append(R"~~~(
            if (@js_name@@js_suffix@_enum_string == "@enum.alt.name@"sv)
                return @union_type@ { @enum.type@::@enum.alt.value@ };
)~~~");
                }
            }

            union_generator.append(R"~~~(
        }
)~~~");
        }

        RefPtr<IDL::Type const> string_type;
        for (auto& type : types) {
            if (type->is_string()) {
                string_type = type;
                break;
            }
        }

        if (string_type) {
            // 14. If types includes a string type, then return the result of converting V to that type.
            // NOTE: Currently all string types are converted to String.

            IDL::Parameter idl_parameter { .type = *string_type, .name = parameter.name, .optional_default_value = {}, .extended_attributes = {} };
            generate_to_cpp(union_generator, idl_parameter, js_name, js_suffix, ByteString::formatted("{}{}_string", js_name, js_suffix), interface, legacy_null_to_empty_string, false, {}, false, recursion_depth + 1);

            union_generator.append(R"~~~(
        return { @js_name@@js_suffix@_string };
)~~~");
        } else if (numeric_type && includes_bigint) {
            // 15. If types includes a numeric type and bigint, then return the result of converting V to either that numeric type or bigint.
            // https://webidl.spec.whatwg.org/#converted-to-a-numeric-type-or-bigint
            // NOTE: This algorithm is only used here.

            // An ECMAScript value V is converted to an IDL numeric type T or bigint value by running the following algorithm:
            // 1. Let x be ? ToNumeric(V).
            // 2. If Type(x) is BigInt, then
            //    1. Return the IDL bigint value that represents the same numeric value as x.
            // 3. Assert: Type(x) is Number.
            // 4. Return the result of converting x to T.

            auto union_numeric_type_generator = union_generator.fork();

            union_numeric_type_generator.append(R"~~~(
        auto x = TRY(@js_name@@js_suffix@.to_numeric(vm));
        if (x.is_bigint())
            return x.as_bigint();
        VERIFY(x.is_number());
)~~~");

            // NOTE: generate_to_cpp doesn't use the parameter name.
            // NOTE: generate_to_cpp will use to_{u32,etc.} which uses to_number internally and will thus use TRY, but it cannot throw as we know we are dealing with a number.
            IDL::Parameter idl_parameter { .type = *numeric_type, .name = parameter.name, .optional_default_value = {}, .extended_attributes = {} };
            generate_to_cpp(union_numeric_type_generator, idl_parameter, "x", ByteString::empty(), "x_number", interface, false, false, {}, false, recursion_depth + 1);

            union_numeric_type_generator.append(R"~~~(
        return x_number;
)~~~");
        } else if (numeric_type) {
            // 16. If types includes a numeric type, then return the result of converting V to that numeric type.

            // NOTE: generate_to_cpp doesn't use the parameter name.
            // NOTE: generate_to_cpp will use to_{u32,etc.} which uses to_number internally and will thus use TRY, but it cannot throw as we know we are dealing with a number.
            IDL::Parameter idl_parameter { .type = *numeric_type, .name = parameter.name, .optional_default_value = {}, .extended_attributes = {} };
            generate_to_cpp(union_generator, idl_parameter, js_name, js_suffix, ByteString::formatted("{}{}_number", js_name, js_suffix), interface, false, false, {}, false, recursion_depth + 1);

            union_generator.append(R"~~~(
        return { @js_name@@js_suffix@_number };
)~~~");
        } else if (includes_boolean) {
            // 17. If types includes boolean, then return the result of converting V to boolean.
            union_generator.append(R"~~~(
        return @union_type@ { @js_name@@js_suffix@.to_boolean() };
)~~~");
        } else if (includes_bigint) {
            // 18. If types includes bigint, then return the result of converting V to bigint.
            union_generator.append(R"~~~(
        return TRY(@js_name@@js_suffix@.to_bigint(vm));
)~~~");
        } else {
            // 19. Throw a TypeError.
            // FIXME: Replace the error message with something more descriptive.
            union_generator.append(R"~~~(
        return vm.throw_completion<JS::TypeError>("No union types matched"sv);
)~~~");
        }

        // Close the lambda and then perform the conversion.
        union_generator.append(R"~~~(
    };
)~~~");

        if (!variadic) {
            if (!optional) {
                union_generator.append(R"~~~(
    @union_type@ @cpp_name@ = TRY(@js_name@@js_suffix@_to_variant(@js_name@@js_suffix@));
)~~~");
            } else {
                if (!optional_default_value.has_value()) {
                    union_generator.set("nullish_or_undefined", union_type.is_nullable() ? "nullish" : "undefined");
                    union_generator.append(R"~~~(
    Optional<@union_type@> @cpp_name@;
    if (!@js_name@@js_suffix@.is_@nullish_or_undefined@())
        @cpp_name@ = TRY(@js_name@@js_suffix@_to_variant(@js_name@@js_suffix@));
)~~~");
                } else {
                    if (optional_default_value == "null"sv) {
                        union_generator.append(R"~~~(
    Optional<@union_type@> @cpp_name@;
    if (!@js_name@@js_suffix@.is_nullish())
        @cpp_name@ = TRY(@js_name@@js_suffix@_to_variant(@js_name@@js_suffix@));
)~~~");
                    } else if (optional_default_value == "\"\"") {
                        union_generator.append(R"~~~(
    @union_type@ @cpp_name@ = @js_name@@js_suffix@.is_undefined() ? TRY(@js_name@@js_suffix@_to_variant(JS::Value(JS::PrimitiveString::create(vm, String {})))) : TRY(@js_name@@js_suffix@_to_variant(@js_name@@js_suffix@));
)~~~");
                    } else if (optional_default_value->starts_with("\""sv) && optional_default_value->ends_with("\""sv)) {
                        union_generator.set("default_string_value", optional_default_value.value());
                        union_generator.append(R"~~~(
    @union_type@ @cpp_name@ = @js_name@@js_suffix@.is_undefined() ? TRY(@js_name@@js_suffix@_to_variant(JS::Value(JS::PrimitiveString::create(vm, MUST(String::from_utf8(@default_string_value@sv)))))) : TRY(@js_name@@js_suffix@_to_variant(@js_name@@js_suffix@));
)~~~");
                    } else if (optional_default_value == "{}") {
                        VERIFY(dictionary_type);
                        union_generator.append(R"~~~(
    @union_type@ @cpp_name@ = @js_name@@js_suffix@.is_undefined() ? TRY(@js_name@@js_suffix@_to_dictionary(@js_name@@js_suffix@)) : TRY(@js_name@@js_suffix@_to_variant(@js_name@@js_suffix@));
)~~~");
                    } else if (optional_default_value->to_number<int>().has_value() || optional_default_value->to_number<unsigned>().has_value()) {
                        union_generator.append(R"~~~(
    @union_type@ @cpp_name@ = @js_name@@js_suffix@.is_undefined() ? @parameter.optional_default_value@ : TRY(@js_name@@js_suffix@_to_variant(@js_name@@js_suffix@));
)~~~");
                    } else if (optional_default_value == "true"sv || optional_default_value == "false"sv) {
                        union_generator.append(R"~~~(
    @union_type@ @cpp_name@ = @js_name@@js_suffix@.is_undefined() ? @parameter.optional_default_value@ : TRY(@js_name@@js_suffix@_to_variant(@js_name@@js_suffix@));
)~~~");
                    } else {
                        dbgln("Don't know how to handle optional default value of `{}`", *optional_default_value);
                        TODO();
                    }
                }
            }
        } else {
            union_generator.append(R"~~~(
        Vector<@union_type@> @cpp_name@;

        if (vm.argument_count() > @js_suffix@) {
            @cpp_name@.ensure_capacity(vm.argument_count() - @js_suffix@);

            for (size_t i = @js_suffix@; i < vm.argument_count(); ++i) {
                auto result = TRY(@js_name@@js_suffix@_to_variant(vm.argument(i)));
                @cpp_name@.unchecked_append(move(result));
            }
        }
    )~~~");
        }
    } else {
        dbgln("Unimplemented JS-to-C++ conversion: {}", parameter.type->name());
        VERIFY_NOT_REACHED();
    }
}
void generate_wrap_statement(SourceGenerator& generator, ByteString const& value, IDL::Type const& type, IDL::Interface const& interface, StringView result_expression, size_t recursion_depth = 0, bool is_optional = false, size_t iteration_index = 0);

}
