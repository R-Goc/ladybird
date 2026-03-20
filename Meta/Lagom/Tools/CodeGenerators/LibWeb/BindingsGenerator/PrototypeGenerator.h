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

void generate_prototype_or_global_mixin_declarations(IDL::Interface const& interface, StringBuilder& builder);
Vector<Interface const&> create_an_inheritance_stack(IDL::Interface const& start_interface);
void collect_attribute_values_of_an_inheritance_stack(SourceGenerator& function_generator, Vector<Interface const&> const& inheritance_chain);
void generate_default_to_json_function(SourceGenerator& generator, ByteString const& class_name, IDL::Interface const& start_interface);
void generate_named_properties_object_declarations(IDL::Interface const& interface, StringBuilder& builder);
void generate_named_properties_object_definitions(IDL::Interface const& interface, StringBuilder& builder);
void generate_prototype_or_global_mixin_initialization(IDL::Interface const& interface, StringBuilder& builder, GenerateUnforgeables generate_unforgeables);
void generate_attribute_setter(SourceGenerator& attribute_generator, IDL::Attribute const& attribute, IDL::Interface const& interface);
void generate_prototype_or_global_mixin_definitions(IDL::Interface const& interface, StringBuilder& builder);
void generate_prototype_header(IDL::Interface const& interface, StringBuilder& builder);
void generate_prototype_implementation(IDL::Interface const& interface, StringBuilder& builder);
void generate_iterator_prototype_header(IDL::Interface const& interface, StringBuilder& builder);
void generate_iterator_prototype_implementation(IDL::Interface const& interface, StringBuilder& builder);
void generate_async_iterator_prototype_header(IDL::Interface const& interface, StringBuilder& builder);
void generate_async_iterator_prototype_implementation(IDL::Interface const& interface, StringBuilder& builder);
void generate_global_mixin_header(IDL::Interface const& interface, StringBuilder& builder);
void generate_global_mixin_implementation(IDL::Interface const& interface, StringBuilder& builder);

}
