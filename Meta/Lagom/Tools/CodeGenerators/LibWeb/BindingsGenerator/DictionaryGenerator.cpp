/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "DictionaryGenerator.h"
#include "CodeGeneratorUtils.h"
#include "ConstructorGenerator.h"
#include "EnumGenerator.h"
#include "FunctionGenerator.h"
#include "NamespaceGenerator.h"
#include "PrototypeGenerator.h"
#include "TypeConversion.h"

namespace IDL {

void generate_dictionary_types(SourceGenerator& generator, Vector<ByteString> const& dictionary_types)
{
    generator.append(R"~~~(
    Vector<StringView> dictionary_types {
)~~~");

    for (auto const& dictionary : dictionary_types) {
        generator.append("    \"");
        generator.append(dictionary);
        generator.appendln("\"sv,");
    }

    generator.append("};\n");
}

void generate_dictionaries(SourceGenerator& generator, IDL::Interface const& interface)
{
    for (auto const& it : interface.dictionaries) {
        if (!it.value.is_original_definition)
            continue;
        if (!it.value.extended_attributes.contains("GenerateToValue"sv))
            continue;
        auto dictionary_generator = generator.fork();
        dictionary_generator.set("dictionary.name", make_input_acceptable_cpp(it.key));
        dictionary_generator.set("dictionary.name:snakecase", make_input_acceptable_cpp(it.key.to_snakecase()));
        dictionary_generator.append(R"~~~(
JS::Value @dictionary.name:snakecase@_to_value(JS::Realm&, @dictionary.name@ const&);
JS::Value @dictionary.name:snakecase@_to_value(JS::Realm& realm, @dictionary.name@ const& dictionary)
{
    auto& vm = realm.vm();
    @dictionary.name@ copy = dictionary;
)~~~");
        // FIXME: Support generating wrap statements for lvalues and get rid of the copy above
        auto dictionary_type = adopt_ref(*new Type(it.key, false));
        generate_wrap_statement(dictionary_generator, "copy", dictionary_type, interface, "return"sv);

        dictionary_generator.append(R"~~~(
}
)~~~");
    }
}

}
