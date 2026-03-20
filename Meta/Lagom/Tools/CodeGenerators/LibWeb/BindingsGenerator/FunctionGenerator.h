/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "CodeGeneratorUtils.h"
#include "DictionaryGenerator.h"
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

void generate_argument_count_check(SourceGenerator& generator, ByteString const& function_name, size_t argument_count);
void generate_arguments(SourceGenerator& generator, Vector<IDL::Parameter> const& parameters, StringBuilder& arguments_builder, IDL::Interface const& interface);
void generate_return_statement(SourceGenerator& generator, IDL::Type const& return_type, IDL::Interface const& interface);
void generate_variable_statement(SourceGenerator& generator, ByteString const& variable_name, IDL::Type const& value_type, ByteString const& value_name, IDL::Interface const& interface);
void generate_function(SourceGenerator& generator, IDL::Function const& function, StaticFunction is_static_function, ByteString const& class_name, ByteString const& interface_fully_qualified_name, IDL::Interface const& interface);
Vector<EffectiveOverloadSet::Item> compute_the_effective_overload_set(auto const& overload_set)
{
    // 1. Let S be an ordered set.
    Vector<EffectiveOverloadSet::Item> overloads;

    // 2. Let F be an ordered set with items as follows, according to the kind of effective overload set:
    // Note: This is determined by the caller of generate_overload_arbiter()

    // 3. Let maxarg be the maximum number of arguments the operations, legacy factory functions, or
    //    callback functions in F are declared to take. For variadic operations and legacy factory functions,
    //    the argument on which the ellipsis appears counts as a single argument.
    auto overloaded_functions = overload_set.value;
    auto maximum_arguments = 0;
    for (auto const& function : overloaded_functions)
        maximum_arguments = max(maximum_arguments, static_cast<int>(function.parameters.size()));

    // 4. Let max be max(maxarg, N).
    // NOTE: We don't do this step. `N` is a runtime value, so we just use `maxarg` here instead.
    //       Later, `generate_overload_arbiter()` produces individual overload sets for each possible N.

    // 5. For each operation or extended attribute X in F:
    auto overload_id = 0;
    for (auto const& overload : overloaded_functions) {
        // 1. Let arguments be the list of arguments X is declared to take.
        auto const& arguments = overload.parameters;

        // 2. Let n be the size of arguments.
        int argument_count = (int)arguments.size();

        // 3. Let types be a type list.
        Vector<NonnullRefPtr<Type const>> types;

        // 4. Let optionalityValues be an optionality list.
        Vector<Optionality> optionality_values;

        bool overload_is_variadic = false;

        // 5. For each argument in arguments:
        for (auto const& argument : arguments) {
            // 1. Append the type of argument to types.
            types.append(argument.type);

            // 2. Append "variadic" to optionalityValues if argument is a final, variadic argument, "optional" if argument is optional, and "required" otherwise.
            if (argument.variadic) {
                optionality_values.append(Optionality::Variadic);
                overload_is_variadic = true;
            } else if (argument.optional) {
                optionality_values.append(Optionality::Optional);
            } else {
                optionality_values.append(Optionality::Required);
            }
        }

        // 6. Append the tuple (X, types, optionalityValues) to S.
        overloads.empend(overload_id, types, optionality_values);

        // 7. If X is declared to be variadic, then:
        if (overload_is_variadic) {
            // 1. For each i in the range n to max − 1, inclusive:
            for (auto i = argument_count; i < maximum_arguments; ++i) {
                // 1. Let t be a type list.
                // 2. Let o be an optionality list.
                // NOTE: We hold both of these in an Item instead.
                EffectiveOverloadSet::Item item;
                item.callable_id = overload_id;

                // 3. For each j in the range 0 to n − 1, inclusive:
                for (auto j = 0; j < argument_count; ++j) {
                    // 1. Append types[j] to t.
                    item.types.append(types[j]);

                    // 2. Append optionalityValues[j] to o.
                    item.optionality_values.append(optionality_values[j]);
                }

                // 4. For each j in the range n to i, inclusive:
                for (auto j = argument_count; j <= i; ++j) {
                    // 1. Append types[n − 1] to t.
                    item.types.append(types[argument_count - 1]);

                    // 2. Append "variadic" to o.
                    item.optionality_values.append(Optionality::Variadic);
                }

                // 5. Append the tuple (X, t, o) to S.
                overloads.append(move(item));
            }
        }

        // 8. Let i be n − 1.
        auto i = argument_count - 1;

        // 9. While i ≥ 0:
        while (i >= 0) {
            // 1. If arguments[i] is not optional (i.e., it is not marked as "optional" and is not a final, variadic argument), then break.
            if (!arguments[i].optional && !arguments[i].variadic)
                break;

            // 2. Let t be a type list.
            // 3. Let o be an optionality list.
            // NOTE: We hold both of these in an Item instead.
            EffectiveOverloadSet::Item item;
            item.callable_id = overload_id;

            // 4. For each j in the range 0 to i − 1, inclusive:
            for (auto j = 0; j < i; ++j) {
                // 1. Append types[j] to t.
                item.types.append(types[j]);

                // 2. Append optionalityValues[j] to o.
                item.optionality_values.append(optionality_values[j]);
            }

            // 5. Append the tuple (X, t, o) to S.
            overloads.append(move(item));

            // 6. Set i to i − 1.
            --i;
        }

        overload_id++;
    }

    return overloads;
}
size_t resolve_distinguishing_argument_index(Interface const& interface, Vector<EffectiveOverloadSet::Item> const& items, size_t argument_count);

void generate_overload_arbiter(SourceGenerator& generator, auto const& overload_set, IDL::Interface const& interface, ByteString const& class_name, IsConstructor is_constructor)
{
    auto function_generator = generator.fork();
    if (is_constructor == IsConstructor::Yes)
        function_generator.set("constructor_class", class_name);
    else
        function_generator.set("class_name", class_name);

    function_generator.set("function.name:snakecase", make_input_acceptable_cpp(overload_set.key.to_snakecase()));

    HashTable<ByteString> dictionary_types;

    if (is_constructor == IsConstructor::Yes) {
        function_generator.append(R"~~~(
JS::ThrowCompletionOr<GC::Ref<JS::Object>> @constructor_class@::construct(JS::FunctionObject& new_target)
{
    auto& vm = this->vm();
    WebIDL::log_trace(vm, "@constructor_class@::construct");
)~~~");
    } else {
        function_generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@class_name@::@function.name:snakecase@)
{
    WebIDL::log_trace(vm, "@class_name@::@function.name:snakecase@");
)~~~");
    }

    function_generator.append(R"~~~(
    Optional<int> chosen_overload_callable_id;
    Optional<IDL::EffectiveOverloadSet> effective_overload_set;
)~~~");

    auto overloads_set = compute_the_effective_overload_set(overload_set);
    auto maximum_argument_count = 0u;
    for (auto const& overload : overloads_set)
        maximum_argument_count = max(maximum_argument_count, overload.types.size());
    function_generator.set("max_argument_count", ByteString::number(maximum_argument_count));
    function_generator.appendln("    switch (min(@max_argument_count@, vm.argument_count())) {");

    // Generate the effective overload set for each argument count.
    // This skips part of the Overload Resolution Algorithm https://webidl.spec.whatwg.org/#es-overloads
    // Namely, since that discards any overloads that don't have the exact number of arguments that were given,
    // we simply only provide the overloads that do have that number of arguments.
    for (auto argument_count = 0u; argument_count <= maximum_argument_count; ++argument_count) {
        Vector<EffectiveOverloadSet::Item> effective_overload_set;
        for (auto const& overload : overloads_set) {
            if (overload.types.size() == argument_count)
                effective_overload_set.append(overload);
        }

        if (effective_overload_set.size() == 0)
            continue;

        auto distinguishing_argument_index = 0u;
        if (effective_overload_set.size() > 1)
            distinguishing_argument_index = resolve_distinguishing_argument_index(interface, effective_overload_set, argument_count);

        function_generator.set("current_argument_count", ByteString::number(argument_count));

        // When there's only a single overload for this argument count, we can skip constructing an EffectiveOverloadSet
        // and calling resolve_overload() entirely, avoiding multiple heap allocations.
        if (effective_overload_set.size() == 1) {
            for (auto const& type : effective_overload_set[0].types) {
                if (interface.dictionaries.contains(type->name()))
                    dictionary_types.set(type->name());
            }

            function_generator.set("overload.callable_id", ByteString::number(effective_overload_set[0].callable_id));
            function_generator.appendln(R"~~~(
    case @current_argument_count@:
        chosen_overload_callable_id = @overload.callable_id@;
        break;
)~~~");
        } else {
            function_generator.set("overload_count", ByteString::number(effective_overload_set.size()));
            function_generator.appendln(R"~~~(
    case @current_argument_count@: {
        Vector<IDL::EffectiveOverloadSet::Item> overloads;
        overloads.ensure_capacity(@overload_count@);
)~~~");

            for (auto& overload : effective_overload_set) {
                StringBuilder types_builder;
                types_builder.append("Vector<NonnullRefPtr<IDL::Type const>> { "sv);
                StringBuilder optionality_builder;
                optionality_builder.append("Vector<IDL::Optionality> { "sv);

                for (auto i = 0u; i < overload.types.size(); ++i) {
                    if (i > 0) {
                        types_builder.append(", "sv);
                        optionality_builder.append(", "sv);
                    }

                    auto const& type = overload.types[i];
                    if (interface.dictionaries.contains(type->name()))
                        dictionary_types.set(type->name());

                    types_builder.append(generate_constructor_for_idl_type(overload.types[i]));

                    optionality_builder.append("IDL::Optionality::"sv);
                    switch (overload.optionality_values[i]) {
                    case Optionality::Required:
                        optionality_builder.append("Required"sv);
                        break;
                    case Optionality::Optional:
                        optionality_builder.append("Optional"sv);
                        break;
                    case Optionality::Variadic:
                        optionality_builder.append("Variadic"sv);
                        break;
                    }
                }

                types_builder.append("}"sv);
                optionality_builder.append("}"sv);

                function_generator.set("overload.callable_id", ByteString::number(overload.callable_id));
                function_generator.set("overload.types", types_builder.to_byte_string());
                function_generator.set("overload.optionality_values", optionality_builder.to_byte_string());

                function_generator.appendln("        overloads.empend(@overload.callable_id@, @overload.types@, @overload.optionality_values@);");
            }

            function_generator.set("overload_set.distinguishing_argument_index", ByteString::number(distinguishing_argument_index));
            function_generator.append(R"~~~(
        effective_overload_set.emplace(move(overloads), @overload_set.distinguishing_argument_index@);
        break;
    }
)~~~");
        }
    }

    function_generator.append(R"~~~(
    }
)~~~");

    generate_dictionary_types(function_generator, dictionary_types.values());

    function_generator.append(R"~~~(

    if (!chosen_overload_callable_id.has_value()) {
        if (!effective_overload_set.has_value())
            return vm.throw_completion<JS::TypeError>(JS::ErrorType::OverloadResolutionFailed);
        chosen_overload_callable_id = TRY(WebIDL::resolve_overload(vm, effective_overload_set.value(), dictionary_types)).callable_id;
    }

    switch (chosen_overload_callable_id.value()) {
)~~~");

    for (auto i = 0u; i < overload_set.value.size(); ++i) {
        function_generator.set("overload_id", ByteString::number(i));
        function_generator.append(R"~~~(
    case @overload_id@:
)~~~");
        if (is_constructor == IsConstructor::Yes) {
            function_generator.append(R"~~~(
        return construct@overload_id@(new_target);
)~~~");
        } else {
            function_generator.append(R"~~~(
        return @function.name:snakecase@@overload_id@(vm);
)~~~");
        }
    }

    function_generator.append(R"~~~(
    default:
        VERIFY_NOT_REACHED();
    }
}
)~~~");
}

}
