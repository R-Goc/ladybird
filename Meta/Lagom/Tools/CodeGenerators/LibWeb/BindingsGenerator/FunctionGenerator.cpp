/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "FunctionGenerator.h"
#include "CodeGeneratorUtils.h"
#include "ConstructorGenerator.h"
#include "DictionaryGenerator.h"
#include "EnumGenerator.h"
#include "NamespaceGenerator.h"
#include "PrototypeGenerator.h"
#include "TypeConversion.h"

namespace IDL {

void generate_argument_count_check(SourceGenerator& generator, ByteString const& function_name, size_t argument_count)
{
    if (argument_count == 0)
        return;

    auto argument_count_check_generator = generator.fork();
    argument_count_check_generator.set("function.name", function_name);
    argument_count_check_generator.set("function.nargs", ByteString::number(argument_count));

    if (argument_count == 1) {
        argument_count_check_generator.set(".bad_arg_count", "JS::ErrorType::BadArgCountOne");
        argument_count_check_generator.set(".arg_count_suffix", "");
    } else {
        argument_count_check_generator.set(".bad_arg_count", "JS::ErrorType::BadArgCountMany");
        argument_count_check_generator.set(".arg_count_suffix", ByteString::formatted(", \"{}\"", argument_count));
    }

    argument_count_check_generator.append(R"~~~(
    if (vm.argument_count() < @function.nargs@)
        return vm.throw_completion<JS::TypeError>(@.bad_arg_count@, "@function.name@"@.arg_count_suffix@);
)~~~");
}

void generate_arguments(SourceGenerator& generator, Vector<IDL::Parameter> const& parameters, StringBuilder& arguments_builder, IDL::Interface const& interface)
{
    auto arguments_generator = generator.fork();

    Vector<ByteString> parameter_names;
    size_t argument_index = 0;
    for (auto& parameter : parameters) {
        auto parameter_name = make_input_acceptable_cpp(parameter.name.to_snakecase());

        if (parameter.variadic) {
            // GC::RootVector is non-copyable, and the implementations likely want ownership of the
            // list, so we move() it into the parameter list.
            parameter_names.append(ByteString::formatted("move({})", parameter_name));
        } else {
            parameter_names.append(move(parameter_name));

            arguments_generator.set("argument.index", ByteString::number(argument_index));

            if (parameter.extended_attributes.contains("ExplicitNull")) {
                arguments_generator.set("argument.size", ByteString::number(argument_index + 1));
                arguments_generator.append(R"~~~(
    auto maybe_arg@argument.index@ = vm.argument_count() >= @argument.size@ ? Optional<JS::Value> { vm.argument(@argument.index@) } : OptionalNone {};
)~~~");
            } else {
                arguments_generator.append(R"~~~(
    auto arg@argument.index@ = vm.argument(@argument.index@);
)~~~");
            }
        }

        bool legacy_null_to_empty_string = parameter.extended_attributes.contains("LegacyNullToEmptyString");
        generate_to_cpp(generator, parameter, "arg", ByteString::number(argument_index), parameter.name.to_snakecase(), interface, legacy_null_to_empty_string, parameter.optional, parameter.optional_default_value, parameter.variadic, 0);
        ++argument_index;
    }

    arguments_builder.join(", "sv, parameter_names);
}

// https://webidl.spec.whatwg.org/#create-sequence-from-iterable
void IDL::ParameterizedType::generate_sequence_from_iterable(SourceGenerator& generator, ByteString const& cpp_name, ByteString const& iterable_cpp_name, ByteString const& iterator_method_cpp_name, IDL::Interface const& interface, size_t recursion_depth) const
{
    auto sequence_generator = generator.fork();
    sequence_generator.set("cpp_name", cpp_name);
    sequence_generator.set("iterable_cpp_name", iterable_cpp_name);
    sequence_generator.set("iterator_method_cpp_name", iterator_method_cpp_name);
    sequence_generator.set("recursion_depth", ByteString::number(recursion_depth));
    auto sequence_cpp_type = idl_type_name_to_cpp_type(parameters().first(), interface);
    sequence_generator.set("sequence.type", sequence_cpp_type.name);
    sequence_generator.set("sequence.storage_type", sequence_storage_type_to_cpp_storage_type_name(sequence_cpp_type.sequence_storage_type));

    // To create an IDL value of type sequence<T> given an iterable iterable and an iterator getter method, perform the following steps:
    // 1. Let iter be ? GetIterator(iterable, sync, method).
    // 2. Initialize i to be 0.
    // 3. Repeat
    //      1. Let next be ? IteratorStep(iter).
    //      2. If next is false, then return an IDL sequence value of type sequence<T> of length i, where the value of the element at index j is Sj.
    //      3. Let nextItem be ? IteratorValue(next).
    //      4. Initialize Si to the result of converting nextItem to an IDL value of type T.
    //      5. Set i to i + 1.

    // FIXME: The WebIDL spec is out of date - it should be using GetIteratorFromMethod.
    sequence_generator.append(R"~~~(
    auto @iterable_cpp_name@_iterator@recursion_depth@ = TRY(JS::get_iterator_from_method(vm, @iterable_cpp_name@, *@iterator_method_cpp_name@));
)~~~");

    if (sequence_cpp_type.sequence_storage_type == SequenceStorageType::Vector) {
        sequence_generator.append(R"~~~(
    @sequence.storage_type@<@sequence.type@> @cpp_name@;
)~~~");
    } else {
        sequence_generator.append(R"~~~(
    @sequence.storage_type@<@sequence.type@> @cpp_name@ { vm.heap() };
)~~~");
    }

    sequence_generator.append(R"~~~(
    for (;;) {
        auto next@recursion_depth@ = TRY(JS::iterator_step(vm, @iterable_cpp_name@_iterator@recursion_depth@));
        if (!next@recursion_depth@.has<JS::IterationResult>())
            break;

        auto next_item@recursion_depth@ = TRY(next@recursion_depth@.get<JS::IterationResult>().value);
)~~~");

    // FIXME: Sequences types should be TypeWithExtendedAttributes, which would allow us to get [LegacyNullToEmptyString] here.
    IDL::Parameter parameter { .type = parameters().first(), .name = iterable_cpp_name, .optional_default_value = {}, .extended_attributes = {} };
    generate_to_cpp(sequence_generator, parameter, "next_item", ByteString::number(recursion_depth), ByteString::formatted("sequence_item{}", recursion_depth), interface, false, false, {}, false, recursion_depth);

    sequence_generator.append(R"~~~(
    @cpp_name@.append(sequence_item@recursion_depth@);
    }
)~~~");
}

void generate_return_statement(SourceGenerator& generator, IDL::Type const& return_type, IDL::Interface const& interface)
{
    return generate_wrap_statement(generator, "retval", return_type, interface, "return"sv);
}

void generate_variable_statement(SourceGenerator& generator, ByteString const& variable_name, IDL::Type const& value_type, ByteString const& value_name, IDL::Interface const& interface)
{
    auto variable_generator = generator.fork();
    variable_generator.set("variable_name", variable_name);
    variable_generator.append(R"~~~(
    JS::Value @variable_name@;
)~~~");
    return generate_wrap_statement(generator, value_name, value_type, interface, ByteString::formatted("{} = ", variable_name));
}

void generate_function(SourceGenerator& generator, IDL::Function const& function, StaticFunction is_static_function, ByteString const& class_name, ByteString const& interface_fully_qualified_name, IDL::Interface const& interface)
{
    auto function_generator = generator.fork();
    function_generator.set("class_name", class_name);
    function_generator.set("interface_fully_qualified_name", interface_fully_qualified_name);
    function_generator.set("function.name", function.name);
    function_generator.set("function.name:snakecase", make_input_acceptable_cpp(function.name.to_snakecase()));
    function_generator.set("overload_suffix", function.is_overloaded ? ByteString::number(function.overload_index) : ByteString::empty());

    if (function.extended_attributes.contains("ImplementedAs")) {
        auto implemented_as = function.extended_attributes.get("ImplementedAs").value();
        function_generator.set("function.cpp_name", implemented_as);
    } else {
        function_generator.set("function.cpp_name", make_input_acceptable_cpp(function.name.to_snakecase()));
    }

    function_generator.append(R"~~~(
JS_DEFINE_NATIVE_FUNCTION(@class_name@::@function.name:snakecase@@overload_suffix@)
{
    WebIDL::log_trace(vm, "@class_name@::@function.name:snakecase@@overload_suffix@");
    [[maybe_unused]] auto& realm = *vm.current_realm();
)~~~");

    // NOTE: Create a wrapper lambda so that if the function steps return an exception, we can return that in a rejected promise.
    if (function.return_type->name() == "Promise"sv) {
        function_generator.append(R"~~~(
    auto steps = [&realm, &vm]() -> JS::ThrowCompletionOr<GC::Ref<WebIDL::Promise>> {
        (void)realm;
)~~~");
    }

    if (is_static_function == StaticFunction::No) {
        function_generator.append(R"~~~(
    auto* impl = TRY(impl_from(vm));
)~~~");
    }

    // Optimization: overloaded functions' arguments count is checked by the overload arbiter
    if (!function.is_overloaded)
        generate_argument_count_check(generator, function.name, function.shortest_length());

    StringBuilder arguments_builder;
    generate_arguments(generator, function.parameters, arguments_builder, interface);
    function_generator.set(".arguments", arguments_builder.string_view());

    if (is_static_function == StaticFunction::No) {
        // For [CEReactions]: https://html.spec.whatwg.org/multipage/custom-elements.html#cereactions

        if (function.extended_attributes.contains("CEReactions")) {
            // 1. Push a new element queue onto this object's relevant agent's custom element reactions stack.
            function_generator.append(R"~~~(
    auto& reactions_stack = HTML::relevant_similar_origin_window_agent(*impl).custom_element_reactions_stack;
    reactions_stack.element_queue_stack.append({});
)~~~");
        }

        if (!function.extended_attributes.contains("CEReactions")) {
            function_generator.append(R"~~~(
    [[maybe_unused]] auto retval = TRY(throw_dom_exception_if_needed(vm, [&] { return impl->@function.cpp_name@(@.arguments@); }));
)~~~");
        } else {
            // 2. Run the originally-specified steps for this construct, catching any exceptions. If the steps return a value, let value be the returned value. If they throw an exception, let exception be the thrown exception.
            // 3. Let queue be the result of popping from this object's relevant agent's custom element reactions stack.
            // 4. Invoke custom element reactions in queue.
            // 5. If an exception exception was thrown by the original steps, rethrow exception.
            // 6. If a value value was returned from the original steps, return value.
            function_generator.append(R"~~~(
    auto retval_or_exception = throw_dom_exception_if_needed(vm, [&] { return impl->@function.cpp_name@(@.arguments@); });

    auto queue = reactions_stack.element_queue_stack.take_last();
    Bindings::invoke_custom_element_reactions(queue);

    if (retval_or_exception.is_error())
        return retval_or_exception.release_error();

    [[maybe_unused]] auto retval = retval_or_exception.release_value();
)~~~");
        }
    } else {
        // Make sure first argument for static functions is the Realm.
        if (arguments_builder.is_empty())
            function_generator.set(".arguments", "vm");
        else
            function_generator.set(".arguments", ByteString::formatted("vm, {}", arguments_builder.string_view()));

        function_generator.append(R"~~~(
    [[maybe_unused]] auto retval = TRY(throw_dom_exception_if_needed(vm, [&] { return @interface_fully_qualified_name@::@function.cpp_name@(@.arguments@); }));
)~~~");
    }

    if (function.return_type->name() == "Promise"sv) {
        // https://webidl.spec.whatwg.org/#dfn-create-operation-function
        // If we had an exception running the steps and are meant to return a Promise, wrap that exception in a rejected promise.
        function_generator.append(R"~~~(
        return retval;
    };

    auto maybe_retval = steps();

    // And then, if an exception E was thrown:
    // 1. If op has a return type that is a promise type, then return ! Call(%Promise.reject%, %Promise%, «E»).
    // 2. Otherwise, end these steps and allow the exception to propagate.
    // NOTE: We know that this is a Promise return type statically by the IDL.
    if (maybe_retval.is_throw_completion())
        return WebIDL::create_rejected_promise(realm, maybe_retval.error_value())->promise();

    auto retval = maybe_retval.release_value();
)~~~");
    }

    generate_return_statement(generator, *function.return_type, interface);

    function_generator.append(R"~~~(
}
)~~~");
}

// https://webidl.spec.whatwg.org/#compute-the-effective-overload-set
size_t resolve_distinguishing_argument_index(Interface const& interface, Vector<EffectiveOverloadSet::Item> const& items, size_t argument_count)
{
    for (auto argument_index = 0u; argument_index < argument_count; ++argument_index) {
        bool found_indistinguishable = false;

        for (auto first_item_index = 0u; first_item_index < items.size(); ++first_item_index) {
            for (auto second_item_index = first_item_index + 1; second_item_index < items.size(); ++second_item_index) {
                if (!items[first_item_index].types[argument_index]->is_distinguishable_from(interface, items[second_item_index].types[argument_index])) {
                    found_indistinguishable = true;
                    break;
                }
            }
            if (found_indistinguishable)
                break;
        }

        if (!found_indistinguishable)
            return argument_index;
    }

    VERIFY_NOT_REACHED();
}

}
