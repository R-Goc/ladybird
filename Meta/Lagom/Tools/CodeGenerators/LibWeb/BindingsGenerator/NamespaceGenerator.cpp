#include "NamespaceGenerator.h"
#include "CodeGeneratorUtils.h"
#include "ConstructorGenerator.h"
#include "DictionaryGenerator.h"
#include "EnumGenerator.h"
#include "FunctionGenerator.h"
#include "PrototypeGenerator.h"
#include "TypeConversion.h"

namespace IDL {

void generate_namespace_header(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("namespace_class", interface.namespace_class);

    generator.append(R"~~~(
#pragma once

#include <LibJS/Runtime/Object.h>

namespace Web::Bindings {

class @namespace_class@ final : public JS::Object {
    JS_OBJECT(@namespace_class@, JS::Object);
    GC_DECLARE_ALLOCATOR(@namespace_class@);
public:
    explicit @namespace_class@(JS::Realm&);
    virtual void initialize(JS::Realm&) override;
    virtual ~@namespace_class@() override;

private:
)~~~");

    if (interface.extended_attributes.contains("WithGCVisitor"sv)) {
        generator.append(R"~~~(
    virtual void visit_edges(JS::Cell::Visitor&) override;
)~~~");
    }

    if (interface.extended_attributes.contains("WithFinalizer"sv)) {
        generator.append(R"~~~(
public:
    static constexpr bool OVERRIDES_FINALIZE = true;

private:
    virtual void finalize() override;
)~~~");
    }

    for (auto const& overload_set : interface.overload_sets) {
        auto function_generator = generator.fork();
        function_generator.set("function.name:snakecase", make_input_acceptable_cpp(overload_set.key.to_snakecase()));
        function_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(@function.name:snakecase@);
)~~~");
        if (overload_set.value.size() > 1) {
            for (auto i = 0u; i < overload_set.value.size(); ++i) {
                function_generator.set("overload_suffix", ByteString::number(i));
                function_generator.append(R"~~~(
    JS_DECLARE_NATIVE_FUNCTION(@function.name:snakecase@@overload_suffix@);
)~~~");
            }
        }
    }

    generator.append(R"~~~(
};

} // namespace Web::Bindings
)~~~");
}

void generate_using_namespace_definitions(SourceGenerator& generator)
{
    generator.append(R"~~~(
// FIXME: This is a total hack until we can figure out the namespace for a given type somehow.
using namespace Web::Animations;
using namespace Web::Clipboard;
using namespace Web::ContentSecurityPolicy;
using namespace Web::CookieStore;
using namespace Web::CredentialManagement;
using namespace Web::Crypto;
using namespace Web::CSS;
using namespace Web::DOM;
using namespace Web::DOMURL;
using namespace Web::Encoding;
using namespace Web::EncryptedMediaExtensions;
using namespace Web::EntriesAPI;
using namespace Web::EventTiming;
using namespace Web::Fetch;
using namespace Web::FileAPI;
using namespace Web::Gamepad;
using namespace Web::Geolocation;
using namespace Web::Geometry;
using namespace Web::HighResolutionTime;
using namespace Web::HTML;
using namespace Web::IndexedDB;
using namespace Web::Internals;
using namespace Web::IntersectionObserver;
using namespace Web::MediaCapabilitiesAPI;
using namespace Web::MediaSourceExtensions;
using namespace Web::NavigationTiming;
using namespace Web::NotificationsAPI;
using namespace Web::PerformanceTimeline;
using namespace Web::RequestIdleCallback;
using namespace Web::ResizeObserver;
using namespace Web::ResourceTiming;
using namespace Web::Selection;
using namespace Web::Serial;
using namespace Web::ServiceWorker;
using namespace Web::Speech;
using namespace Web::StorageAPI;
using namespace Web::Streams;
using namespace Web::SVG;
using namespace Web::TrustedTypes;
using namespace Web::UIEvents;
using namespace Web::URLPattern;
using namespace Web::UserTiming;
using namespace Web::WebAssembly;
using namespace Web::WebAudio;
using namespace Web::WebGL;
using namespace Web::WebGL::Extensions;
using namespace Web::WebIDL;
using namespace Web::WebVTT;
using namespace Web::WebXR;
using namespace Web::XHR;
using namespace Web::XPath;
)~~~"sv);
}

// https://webidl.spec.whatwg.org/#define-the-operations
void define_the_operations(SourceGenerator& generator, OrderedHashMap<ByteString, Vector<Function&>> const& operations)
{
    for (auto const& operation : operations) {
        auto function_generator = generator.fork();
        function_generator.set("function.name", operation.key);
        function_generator.set("function.name:snakecase", make_input_acceptable_cpp(operation.key.to_snakecase()));
        function_generator.set("function.length", ByteString::number(get_shortest_function_length(operation.value)));

        // NOTE: This assumes that every function in the overload set has the same attribute set.
        if (operation.value[0].extended_attributes.contains("LegacyUnforgable"sv))
            function_generator.set("function.attributes", "JS::Attribute::Enumerable");
        else
            function_generator.set("function.attributes", "JS::Attribute::Writable | JS::Attribute::Enumerable | JS::Attribute::Configurable");

        function_generator.append(R"~~~(
    define_native_function(realm, "@function.name@"_utf16_fly_string, @function.name:snakecase@, @function.length@, @function.attributes@);
)~~~");
    }
}

void generate_namespace_implementation(IDL::Interface const& interface, StringBuilder& builder)
{
    SourceGenerator generator { builder };

    generator.set("name", interface.name);
    generator.set("namespace_class", interface.namespace_class);
    generator.set("interface_name", interface.name);

    generator.append(R"~~~(
#include <AK/Function.h>
#include <LibIDL/Types.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/DataView.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/PromiseConstructor.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibJS/Runtime/Value.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibWeb/Bindings/@namespace_class@.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/WindowProxy.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/Buffers.h>
#include <LibWeb/WebIDL/OverloadResolution.h>
#include <LibWeb/WebIDL/Promise.h>
#include <LibWeb/WebIDL/Tracing.h>
#include <LibWeb/WebIDL/Types.h>

)~~~");

    emit_includes_for_all_imports(interface, generator, interface.pair_iterator_types.has_value(), interface.async_value_iterator_type.has_value());

    generate_using_namespace_definitions(generator);

    generator.append(R"~~~(
namespace Web::Bindings {

GC_DEFINE_ALLOCATOR(@namespace_class@);

@namespace_class@::@namespace_class@(JS::Realm& realm)
    : Object(ConstructWithPrototypeTag::Tag, realm.intrinsics().object_prototype())
{
}

@namespace_class@::~@namespace_class@()
{
}

void @namespace_class@::initialize(JS::Realm& realm)
{
    [[maybe_unused]] auto& vm = this->vm();

    Base::initialize(realm);

    define_direct_property(vm.well_known_symbol_to_string_tag(), JS::PrimitiveString::create(vm, "@interface_name@"_string), JS::Attribute::Configurable);

)~~~");

    define_the_operations(generator, interface.overload_sets);

    if (interface.extended_attributes.contains("WithInitializer"sv)) {
        generator.append(R"~~~(

    @name@::initialize(*this, realm);
)~~~");
    }

    generator.append(R"~~~(
}
)~~~");

    if (interface.extended_attributes.contains("WithGCVisitor"sv)) {
        generator.append(R"~~~(
void @namespace_class@::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    @name@::visit_edges(*this, visitor);
}
)~~~");
    }

    if (interface.extended_attributes.contains("WithFinalizer"sv)) {
        generator.append(R"~~~(
void @namespace_class@::finalize()
{
    Base::finalize();
    @name@::finalize(*this);
}
)~~~");
    }

    for (auto const& function : interface.functions) {
        if (function.extended_attributes.contains("FIXME"))
            continue;
        generate_function(generator, function, StaticFunction::Yes, interface.namespace_class, interface.name, interface);
    }
    for (auto const& overload_set : interface.overload_sets) {
        if (overload_set.value.size() == 1)
            continue;
        generate_overload_arbiter(generator, overload_set, interface, interface.namespace_class, IsConstructor::No);
    }

    generator.append(R"~~~(
} // namespace Web::Bindings
)~~~");
}

}
