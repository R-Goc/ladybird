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

void generate_namespace_header(IDL::Interface const& interface, StringBuilder& builder);
void generate_using_namespace_definitions(SourceGenerator& generator);
void define_the_operations(SourceGenerator& generator, OrderedHashMap<ByteString, Vector<Function&>> const& operations);
void generate_namespace_implementation(IDL::Interface const& interface, StringBuilder& builder);

}
