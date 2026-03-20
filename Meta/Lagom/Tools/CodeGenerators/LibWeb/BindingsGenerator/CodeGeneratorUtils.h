#pragma once

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

enum class StaticFunction {
    No,
    Yes,
};

enum class IsConstructor {
    No,
    Yes,
};

enum class GenerateUnforgeables {
    No,
    Yes,
};

extern Vector<StringView> g_header_search_paths;

}

namespace IDL {

bool is_platform_object(Type const& type);
bool is_javascript_builtin(Type const& type);
Interface const* callback_interface_for_type(Interface const& interface, Type const& type);
StringView sequence_storage_type_to_cpp_storage_type_name(SequenceStorageType sequence_storage_type);
bool is_nullable_frozen_array_of_single_type(Type const& type, StringView type_name);
ByteString union_type_to_variant(UnionType const& union_type, Interface const& interface);
CppType idl_type_name_to_cpp_type(Type const& type, Interface const& interface);
ByteString make_input_acceptable_cpp(ByteString const& input);

void generate_include_for_iterator(auto& generator, auto& iterator_path)
{
    auto iterator_generator = generator.fork();
    iterator_generator.set("iterator_class.path", iterator_path);
    iterator_generator.append(R"~~~(
#   include <LibWeb/@iterator_class.path@.h>
)~~~");
}

void generate_include_for_interface(auto& generator, Interface const& interface)
{
    auto forked_generator = generator.fork();
    auto path_string = interface.module_own_path;
    for (auto& search_path : g_header_search_paths) {
        if (!interface.module_own_path.starts_with(search_path))
            continue;
        auto relative_path = *LexicalPath::relative_path(interface.module_own_path, search_path);
        if (relative_path.length() < path_string.length())
            path_string = relative_path;
    }

    LexicalPath include_path { path_string };
    ByteString include_title = interface.implemented_name.is_empty() ? include_path.title().to_byte_string() : interface.implemented_name;
    forked_generator.set("include.path", ByteString::formatted("{}/{}.h", include_path.dirname(), include_title));
    forked_generator.append(R"~~~(
#include <@include.path@>
)~~~");
}

void emit_includes_for_all_imports(auto& interface, auto& generator, bool is_iterator = false, bool is_async_iterator = false)
{
    Queue<RemoveCVReference<decltype(interface)> const*> interfaces;
    HashTable<ByteString> paths_imported;

    interfaces.enqueue(&interface);

    while (!interfaces.is_empty()) {
        auto interface = interfaces.dequeue();
        if (paths_imported.contains(interface->module_own_path))
            continue;

        paths_imported.set(interface->module_own_path);
        for (auto& imported_interface : interface->imported_modules) {
            if (!paths_imported.contains(imported_interface.module_own_path))
                interfaces.enqueue(&imported_interface);
        }

        if (!interface->will_generate_code())
            continue;

        generate_include_for_interface(generator, *interface);
    }

    if (is_iterator) {
        auto iterator_path = ByteString::formatted("{}Iterator", interface.fully_qualified_name.replace("::"sv, "/"sv, ReplaceMode::All));
        generate_include_for_iterator(generator, iterator_path);
    }
    if (is_async_iterator) {
        auto iterator_path = ByteString::formatted("{}AsyncIterator", interface.fully_qualified_name.replace("::"sv, "/"sv, ReplaceMode::All));
        generate_include_for_iterator(generator, iterator_path);
    }
}

}
