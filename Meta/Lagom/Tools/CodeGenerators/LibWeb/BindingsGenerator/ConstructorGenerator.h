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

ByteString generate_constructor_for_idl_type(Type const& type);
void generate_html_constructor(SourceGenerator& generator, IDL::Constructor const& constructor, IDL::Interface const& interface);
void generate_constructor(SourceGenerator& generator, IDL::Constructor const& constructor, IDL::Interface const& interface, bool is_html_constructor);
void generate_constructors(SourceGenerator& generator, IDL::Interface const& interface);
void generate_constructor_header(IDL::Interface const& interface, StringBuilder& builder);
void generate_constructor_implementation(IDL::Interface const& interface, StringBuilder& builder);

}
