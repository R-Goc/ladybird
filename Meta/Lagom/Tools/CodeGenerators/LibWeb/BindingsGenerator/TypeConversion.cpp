/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TypeConversion.h"
#include "CodeGeneratorUtils.h"
#include "ConstructorGenerator.h"
#include "DictionaryGenerator.h"
#include "EnumGenerator.h"
#include "FunctionGenerator.h"
#include "NamespaceGenerator.h"
#include "PrototypeGenerator.h"

namespace IDL {

void generate_from_integral(SourceGenerator& scoped_generator, IDL::Type const& type, bool const optional_integral_type)
{
    struct TypeMap {
        StringView idl_type;
        StringView cpp_type;
    };
    constexpr auto idl_type_map = to_array<TypeMap>({
        { "byte"sv, "WebIDL::Byte"sv },
        { "octet"sv, "WebIDL::Octet"sv },
        { "short"sv, "WebIDL::Short"sv },
        { "unsigned short"sv, "WebIDL::UnsignedShort"sv },
        { "long"sv, "WebIDL::Long"sv },
        { "unsigned long"sv, "WebIDL::UnsignedLong"sv },
        { "long long"sv, "double"sv },
        { "unsigned long long"sv, "double"sv },
    });

    auto it = find_if(idl_type_map.begin(), idl_type_map.end(), [&](auto const& entry) {
        return entry.idl_type == type.name();
    });

    VERIFY(it != idl_type_map.end());
    scoped_generator.set("cpp_type"sv, it->cpp_type);

    if (type.is_nullable() || optional_integral_type) {
        scoped_generator.append(R"~~~(
    @result_expression@ JS::Value(static_cast<@cpp_type@>(@value@.value()));
)~~~");
    } else {
        scoped_generator.append(R"~~~(
    @result_expression@ JS::Value(static_cast<@cpp_type@>(@value@));
)~~~");
    }
}

void generate_wrap_statement(SourceGenerator& generator, ByteString const& value, IDL::Type const& type, IDL::Interface const& interface, StringView result_expression, size_t recursion_depth, bool is_optional, size_t iteration_index)
{
    auto scoped_generator = generator.fork();
    scoped_generator.set("value", value);
    scoped_generator.set("value_cpp_name", value.replace("."sv, "_"sv));
    // Use one non-optional expression for wrapping.
    // Some optional values are Optional<T> and need .value(), while others are pointer-like and do not.
    auto value_non_optional = value;

    bool optional_uses_value_access = false;
    if (is_optional) {
        optional_uses_value_access = is<UnionType>(type)
            || type.is_string()
            || type.name().is_one_of("sequence"sv, "FrozenArray"sv)
            || type.is_primitive()
            || interface.enumerations.contains(type.name())
            || interface.dictionaries.contains(type.name());
    }

    if (optional_uses_value_access)
        value_non_optional = ByteString::formatted("{}.value()", value);
    scoped_generator.set("value_non_optional", value_non_optional);
    if (!libweb_interface_namespaces.span().contains_slow(type.name())) {
        if (is_javascript_builtin(type))
            scoped_generator.set("type", ByteString::formatted("JS::{}", type.name()));
        else
            scoped_generator.set("type", type.name());
    } else {
        // e.g. Document.getSelection which returns Selection, which is in the Selection namespace.
        StringBuilder builder;
        builder.append(type.name());
        builder.append("::"sv);
        builder.append(type.name());
        scoped_generator.set("type", builder.to_byte_string());
    }
    scoped_generator.set("result_expression", result_expression);
    scoped_generator.set("recursion_depth", ByteString::number(recursion_depth));
    scoped_generator.set("iteration_index", ByteString::number(iteration_index));

    if (type.name() == "undefined") {
        scoped_generator.append(R"~~~(
    @result_expression@ JS::js_undefined();
)~~~");
        return;
    }

    bool generate_optional_integral_type = false;
    if ((is_optional || type.is_nullable()) && !is<UnionType>(type)) {
        generate_optional_integral_type = true;
        if (type.is_string()) {
            scoped_generator.append(R"~~~(
    if (@value@.has_value()) {
)~~~");
        } else if (type.name().is_one_of("sequence"sv, "FrozenArray"sv)) {
            generate_optional_integral_type = true;
            scoped_generator.append(R"~~~(
    if (@value@.has_value()) {
)~~~");
        } else if (type.is_primitive() || interface.enumerations.contains(type.name()) || interface.dictionaries.contains(type.name())) {
            generate_optional_integral_type = true;
            scoped_generator.append(R"~~~(
    if (@value@.has_value()) {
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    if (@value@) {
)~~~");
        }
    }
    if (is_optional && is<UnionType>(type)) {
        scoped_generator.append(R"~~~(
    if (@value@.has_value()) {
)~~~");
    }

    if (type.is_string()) {
        if (type.is_nullable() || is_optional) {
            // FIXME: Ideally we would not need to do this const_cast, but we currently rely on temporary
            //        lifetime extension to allow Variants to compile and handle an interface returning a
            //        GC::Ref while the generated code will visit it as a GC::Root.
            scoped_generator.append(R"~~~(
    @result_expression@ JS::PrimitiveString::create(vm, const_cast<decltype(@value@)&>(@value@).release_value());
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    @result_expression@ JS::PrimitiveString::create(vm, @value@);
)~~~");
        }
    } else if (type.name().is_one_of("sequence"sv, "FrozenArray"sv)) {
        // https://webidl.spec.whatwg.org/#js-sequence
        // https://webidl.spec.whatwg.org/#js-frozen-array
        auto& sequence_generic_type = as<IDL::ParameterizedType>(type);

        scoped_generator.append(R"~~~(
    auto new_array@recursion_depth@_@iteration_index@ = MUST(JS::Array::create(realm, 0));
)~~~");

        if (type.is_nullable() || is_optional) {
            scoped_generator.append(R"~~~(
    auto& @value_cpp_name@_non_optional = @value@.value();
    for (size_t i@recursion_depth@ = 0; i@recursion_depth@ < @value_cpp_name@_non_optional.size(); ++i@recursion_depth@) {
        auto& element@recursion_depth@ = @value_cpp_name@_non_optional.at(i@recursion_depth@);
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    for (size_t i@recursion_depth@ = 0; i@recursion_depth@ < @value@.size(); ++i@recursion_depth@) {
        auto& element@recursion_depth@ = @value@.at(i@recursion_depth@);
)~~~");
        }

        // If the type is a platform object we currently return a Vector<GC::Root<T>> from the
        // C++ implementation, thus allowing us to unwrap the element (a handle) like below.
        // This might need to change if we switch to a RootVector.
        if (is_platform_object(sequence_generic_type.parameters().first())) {
            scoped_generator.append(R"~~~(
        auto* wrapped_element@recursion_depth@ = &(*element@recursion_depth@);
)~~~");
        } else {
            scoped_generator.append("JS::Value wrapped_element@recursion_depth@;\n"sv);
            generate_wrap_statement(scoped_generator, ByteString::formatted("element{}", recursion_depth), sequence_generic_type.parameters().first(), interface, ByteString::formatted("wrapped_element{} =", recursion_depth), recursion_depth + 1);
        }

        scoped_generator.append(R"~~~(
        auto property_index@recursion_depth@ = JS::PropertyKey { i@recursion_depth@ };
        MUST(new_array@recursion_depth@_@iteration_index@->create_data_property(property_index@recursion_depth@, wrapped_element@recursion_depth@));
    }
)~~~");

        if (type.name() == "FrozenArray"sv) {
            scoped_generator.append(R"~~~(
    TRY(new_array@recursion_depth@_@iteration_index@->set_integrity_level(IntegrityLevel::Frozen));
)~~~");
        }

        scoped_generator.append(R"~~~(
    @result_expression@ new_array@recursion_depth@_@iteration_index@;
)~~~");
    } else if (type.name() == "record") {
        // https://webidl.spec.whatwg.org/#es-record

        auto& parameterized_type = as<IDL::ParameterizedType>(type);
        VERIFY(parameterized_type.parameters().size() == 2);
        VERIFY(parameterized_type.parameters()[0]->is_string());

        scoped_generator.append(R"~~~(
    {
        // An IDL record<…> value D is converted to a JavaScript value as follows:
        // 1. Let result be OrdinaryObjectCreate(%Object.prototype%).
        auto result = JS::Object::create(realm, realm.intrinsics().object_prototype());

        // 2. For each key → value of D:
        for (auto const& [key, value] : @value_non_optional@) {
            // 1. Let jsKey be key converted to a JavaScript value.
            auto js_key = JS::PropertyKey { Utf16FlyString::from_utf8(key) };

            // 2. Let jsValue be value converted to a JavaScript value.
)~~~");
        generate_wrap_statement(scoped_generator, "value"sv, parameterized_type.parameters()[1], interface, "auto js_value ="sv, recursion_depth + 1);
        scoped_generator.append(R"~~~(

            // 3. Let created be ! CreateDataProperty(result, jsKey, jsValue).
            bool created = MUST(result->create_data_property(js_key, js_value));

            // 4. Assert: created is true.
            VERIFY(created);
        }

        @result_expression@ result;
    }
)~~~");
    } else if (type.name() == "boolean" || type.is_floating_point()) {
        if (type.is_nullable()) {
            scoped_generator.append(R"~~~(
    @result_expression@ JS::Value(@value@.release_value());
)~~~");
        } else if (is_optional) {
            scoped_generator.append(R"~~~(
    @result_expression@ JS::Value(@value_non_optional@);
)~~~");
        } else {
            scoped_generator.append(R"~~~(
    @result_expression@ JS::Value(@value@);
)~~~");
        }
    } else if (type.is_integer()) {
        generate_from_integral(scoped_generator, type, generate_optional_integral_type);
    } else if (type.name() == "Location" || type.name() == "Uint8Array" || type.name() == "Uint8ClampedArray" || type.name() == "any") {
        scoped_generator.append(R"~~~(
    @result_expression@ @value_non_optional@;
)~~~");
    } else if (type.name() == "Promise") {
        scoped_generator.append(R"~~~(
    @result_expression@ GC::Ref { as<JS::Promise>(*@value_non_optional@->promise()) };
)~~~");
    } else if (type.name() == "ArrayBufferView" || type.name() == "BufferSource") {
        scoped_generator.append(R"~~~(
    @result_expression@ JS::Value(@value_non_optional@->raw_object());
)~~~");
    } else if (is<IDL::UnionType>(type)) {
        auto& union_type = as<IDL::UnionType>(type);
        auto union_types = union_type.flattened_member_types();
        auto union_generator = scoped_generator.fork();

        union_generator.append(R"~~~(
    @result_expression@ @value_non_optional@.visit(
)~~~");

        for (size_t current_union_type_index = 0; current_union_type_index < union_types.size(); ++current_union_type_index) {
            auto& current_union_type = union_types.at(current_union_type_index);
            auto cpp_type = IDL::idl_type_name_to_cpp_type(current_union_type, interface);
            union_generator.set("current_type", cpp_type.name);
            union_generator.append(R"~~~(
        [&vm, &realm]([[maybe_unused]] @current_type@ const& visited_union_value@recursion_depth@) -> JS::Value {
            // These may be unused.
            (void)vm;
            (void)realm;
)~~~");

            // NOTE: While we are using const&, the underlying type for wrappable types in unions is (Nonnull)RefPtr, which are not references.
            generate_wrap_statement(union_generator, ByteString::formatted("visited_union_value{}", recursion_depth), current_union_type, interface, "return"sv, recursion_depth + 1);

            // End of current visit lambda.
            // The last lambda cannot have a trailing comma on the closing brace, unless the type is nullable, where an extra lambda will be generated for the Empty case.
            if (current_union_type_index != union_types.size() - 1 || type.is_nullable()) {
                union_generator.append(R"~~~(
        },
)~~~");
            } else {
                union_generator.append(R"~~~(
        }
)~~~");
            }
        }

        if (type.is_nullable()) {
            union_generator.append(R"~~~(
        [](Empty) -> JS::Value {
            return JS::js_null();
        }
)~~~");
        }

        // End of visit.
        union_generator.append(R"~~~(
    );
)~~~");
    } else if (interface.enumerations.contains(type.name())) {
        // Handle Enum? values, which were null-checked above
        if (type.is_nullable() || is_optional)
            scoped_generator.set("value", ByteString::formatted("{}.value()", value));
        scoped_generator.append(R"~~~(
    @result_expression@ JS::PrimitiveString::create(vm, Bindings::idl_enum_to_string(@value@));
)~~~");
    } else if (interface.callback_functions.contains(type.name())) {
        // https://webidl.spec.whatwg.org/#es-callback-function

        auto& callback_function = interface.callback_functions.find(type.name())->value;

        // The result of converting an IDL callback function type value to an ECMAScript value is a reference to the same object that the IDL callback function type value represents.

        if (callback_function.is_legacy_treat_non_object_as_null && !type.is_nullable()) {
            scoped_generator.append(R"~~~(
  if (!@value_non_optional@) {
      @result_expression@ JS::js_null();
  } else {
      @result_expression@ @value_non_optional@->callback;
  }
)~~~");
        } else {
            scoped_generator.append(R"~~~(
  @result_expression@ @value_non_optional@->callback;
)~~~");
        }
    } else if (callback_interface_for_type(interface, type)) {
        scoped_generator.append(R"~~~(
  @result_expression@ @value@->callback().callback;
)~~~");
    } else if (interface.dictionaries.contains(type.name())) {
        // https://webidl.spec.whatwg.org/#es-dictionary
        auto dictionary_generator = scoped_generator.fork();

        dictionary_generator.append(R"~~~(
    {
        auto dictionary_object@recursion_depth@ = JS::Object::create(realm, realm.intrinsics().object_prototype());
)~~~");

        auto next_iteration_index = iteration_index + 1;
        auto* current_dictionary = &interface.dictionaries.find(type.name())->value;
        while (true) {
            for (auto& member : current_dictionary->members) {
                dictionary_generator.set("member_key", member.name);
                auto member_key_js_name = ByteString::formatted("{}{}", make_input_acceptable_cpp(member.name.to_snakecase()), recursion_depth);
                dictionary_generator.set("member_name", member_key_js_name);
                auto member_value_js_name = ByteString::formatted("{}_value", member_key_js_name);
                dictionary_generator.set("member_value", member_value_js_name);

                auto wrapped_value_name = ByteString::formatted("wrapped_{}", member_value_js_name);
                dictionary_generator.set("wrapped_value_name", wrapped_value_name);

                // NOTE: This has similar semantics as 'required' in WebIDL. However, the spec does not put 'required' on
                //       _returned_ dictionary members since with the way the spec is worded it has no normative effect to
                //      do so. We could implement this without the 'GenerateAsRequired' extended attribute, but it would require
                //      the generated code to do some metaprogramming to inspect the type of the member in the C++ struct to
                //      determine whether the type is present or not (e.g through a has_value() on an Optional<T>, or a null
                //      check on a GC::Ptr<T>). So to save some complexity in the generator, give ourselves a hint of what to do.
                bool is_optional = !member.required && !member.extended_attributes.contains("GenerateAsRequired") && !member.default_value.has_value();
                if (is_optional) {
                    dictionary_generator.append(R"~~~(
        Optional<JS::Value> @wrapped_value_name@;
)~~~");
                } else {
                    dictionary_generator.append(R"~~~(
        JS::Value @wrapped_value_name@;
)~~~");
                }

                next_iteration_index++;
                generate_wrap_statement(dictionary_generator, ByteString::formatted("{}{}{}", value_non_optional, type.is_nullable() ? "->" : ".", member.name.to_snakecase()), member.type, interface, ByteString::formatted("{} =", wrapped_value_name), recursion_depth + 1, is_optional, next_iteration_index);

                if (is_optional) {
                    dictionary_generator.append(R"~~~(
        if (@wrapped_value_name@.has_value())
            MUST(dictionary_object@recursion_depth@->create_data_property("@member_key@"_utf16_fly_string, @wrapped_value_name@.release_value()));
)~~~");
                } else {
                    dictionary_generator.append(R"~~~(
        MUST(dictionary_object@recursion_depth@->create_data_property("@member_key@"_utf16_fly_string, @wrapped_value_name@));
)~~~");
                }
            }

            if (current_dictionary->parent_name.is_empty())
                break;
            VERIFY(interface.dictionaries.contains(current_dictionary->parent_name));
            current_dictionary = &interface.dictionaries.find(current_dictionary->parent_name)->value;
        }

        dictionary_generator.append(R"~~~(
        @result_expression@ dictionary_object@recursion_depth@;
    }
)~~~");
    } else if (type.name() == "object") {
        scoped_generator.append(R"~~~(
    @result_expression@ JS::Value(const_cast<JS::Object*>(@value_non_optional@));
)~~~");
    } else {
        scoped_generator.append(R"~~~(
    @result_expression@ &const_cast<@type@&>(*@value_non_optional@);
)~~~");
    }

    if (type.is_nullable() && !is<UnionType>(type)) {
        scoped_generator.append(R"~~~(
    } else {
        @result_expression@ JS::js_null();
    }
)~~~");
    } else if (is_optional) {
        // Optional return values should not be assigned any value (including null) if the value is not present.
        scoped_generator.append(R"~~~(
    }
)~~~");
    }
}

}
