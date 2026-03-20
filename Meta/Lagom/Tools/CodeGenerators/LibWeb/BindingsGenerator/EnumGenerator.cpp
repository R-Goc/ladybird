/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "EnumGenerator.h"
#include "CodeGeneratorUtils.h"
#include "ConstructorGenerator.h"
#include "DictionaryGenerator.h"
#include "FunctionGenerator.h"
#include "NamespaceGenerator.h"
#include "PrototypeGenerator.h"
#include "TypeConversion.h"

namespace IDL {

ByteString get_best_value_for_underlying_enum_type(size_t size)
{

    if (size < NumericLimits<u8>::max()) {
        return "u8";
    } else if (size < NumericLimits<u16>::max()) {
        return "u16";
    }

    VERIFY_NOT_REACHED();
}

void generate_enumerations(HashMap<ByteString, Enumeration> const& enumerations, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    for (auto const& it : enumerations) {
        if (!it.value.is_original_definition)
            continue;
        auto enum_generator = generator.fork();
        enum_generator.set("enum.type.name", it.key);
        enum_generator.set("enum.underlying_type", get_best_value_for_underlying_enum_type(it.value.translated_cpp_names.size()));
        enum_generator.append(R"~~~(
enum class @enum.type.name@ : @enum.underlying_type@ {
)~~~");
        for (auto const& entry : it.value.translated_cpp_names) {
            enum_generator.set("enum.entry", entry.value);
            enum_generator.append(R"~~~(
    @enum.entry@,
)~~~");
        }

        enum_generator.append(R"~~~(
};
)~~~");

        enum_generator.append(R"~~~(
inline String idl_enum_to_string(@enum.type.name@ value)
{
    switch (value) {
)~~~");
        for (auto const& entry : it.value.translated_cpp_names) {
            enum_generator.set("enum.entry", entry.value);
            enum_generator.set("enum.string", entry.key);
            enum_generator.append(R"~~~(
    case @enum.type.name@::@enum.entry@:
        return "@enum.string@"_string;
)~~~");
        }
        enum_generator.append(R"~~~(
    }
    VERIFY_NOT_REACHED();
}
)~~~");
    }
}

}
