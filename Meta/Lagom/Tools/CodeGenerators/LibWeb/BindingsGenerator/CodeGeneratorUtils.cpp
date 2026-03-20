#include "CodeGeneratorUtils.h"
#include "ConstructorGenerator.h"
#include "DictionaryGenerator.h"
#include "EnumGenerator.h"
#include "FunctionGenerator.h"
#include "NamespaceGenerator.h"
#include "PrototypeGenerator.h"
#include "TypeConversion.h"

namespace IDL {

Vector<StringView> g_header_search_paths;

}

namespace IDL {

bool is_platform_object(Type const& type)
{
    // NOTE: This is a hand-curated subset of platform object types that are actually relevant
    // in places where this function is used. If you add IDL code and get compile errors, you
    // might simply need to add another type here.
    constexpr Array types = {
        "AbortSignal"sv,
        "Animation"sv,
        "AnimationEffect"sv,
        "AnimationTimeline"sv,
        "Attr"sv,
        "AudioBuffer"sv,
        "AudioContext"sv,
        "AudioListener"sv,
        "AudioNode"sv,
        "AudioParam"sv,
        "AudioScheduledSourceNode"sv,
        "AudioTrack"sv,
        "BaseAudioContext"sv,
        "Blob"sv,
        "CacheStorage"sv,
        "CanvasGradient"sv,
        "CanvasPattern"sv,
        "CanvasRenderingContext2D"sv,
        "ClipboardItem"sv,
        "CloseWatcher"sv,
        "Credential"sv,
        "CredentialsContainer"sv,
        "CryptoKey"sv,
        "CSSKeywordValue"sv,
        "CSSNumericArray"sv,
        "CSSNumericValue"sv,
        "CSSStyleValue"sv,
        "CSSTransformComponent"sv,
        "CSSUnitValue"sv,
        "CSSUnparsedValue"sv,
        "CSSVariableReferenceValue"sv,
        "CustomStateSet"sv,
        "DataTransfer"sv,
        "Document"sv,
        "DocumentType"sv,
        "DOMMatrix"sv,
        "DOMMatrixReadOnly"sv,
        "DOMRectReadOnly"sv,
        "DynamicsCompressorNode"sv,
        "ElementInternals"sv,
        "EventTarget"sv,
        "External"sv,
        "FederatedCredential"sv,
        "File"sv,
        "FileList"sv,
        "FontFace"sv,
        "FormData"sv,
        "Gamepad"sv,
        "GamepadButton"sv,
        "GamepadHapticActuator"sv,
        "HTMLCollection"sv,
        "IDBCursor"sv,
        "IDBCursorWithValue"sv,
        "IDBIndex"sv,
        "IDBKeyRange"sv,
        "IDBObjectStore"sv,
        "IDBRecord"sv,
        "IDBTransaction"sv,
        "ImageBitmap"sv,
        "ImageData"sv,
        "Instance"sv,
        "IntersectionObserverEntry"sv,
        "KeyframeEffect"sv,
        "MediaKeySystemAccess"sv,
        "MediaList"sv,
        "Memory"sv,
        "MessagePort"sv,
        "Module"sv,
        "MutationRecord"sv,
        "NamedNodeMap"sv,
        "NavigationDestination"sv,
        "NavigationHistoryEntry"sv,
        "Node"sv,
        "OffscreenCanvas"sv,
        "OffscreenCanvasRenderingContext2D"sv,
        "Origin"sv,
        "PasswordCredential"sv,
        "Path2D"sv,
        "PerformanceEntry"sv,
        "PerformanceMark"sv,
        "PerformanceNavigation"sv,
        "PeriodicWave"sv,
        "ReadableStreamBYOBReader"sv,
        "ReadableStreamDefaultReader"sv,
        "RadioNodeList"sv,
        "Range"sv,
        "ReadableStream"sv,
        "Request"sv,
        "Selection"sv,
        "ServiceWorkerContainer"sv,
        "ServiceWorkerRegistration"sv,
        "SVGLength"sv,
        "SVGNumber"sv,
        "SVGTransform"sv,
        "ShadowRoot"sv,
        "SourceBuffer"sv,
        "SpeechGrammar"sv,
        "SpeechGrammarList"sv,
        "SpeechRecognition"sv,
        "SpeechRecognitionAlternative"sv,
        "SpeechRecognitionPhrase"sv,
        "SpeechRecognitionResult"sv,
        "SpeechRecognitionResultList"sv,
        "SpeechSynthesis"sv,
        "SpeechSynthesisUtterance"sv,
        "SpeechSynthesisVoice"sv,
        "Storage"sv,
        "Table"sv,
        "Text"sv,
        "TextMetrics"sv,
        "TextTrack"sv,
        "TimeRanges"sv,
        "TrustedHTML"sv,
        "TrustedScript"sv,
        "TrustedScriptURL"sv,
        "TrustedTypePolicy"sv,
        "TrustedTypePolicyFactory"sv,
        "URLSearchParams"sv,
        "VTTRegion"sv,
        "VideoTrack"sv,
        "VideoTrackList"sv,
        "ViewTransition"sv,
        "WebGL2RenderingContext"sv,
        "WebGLActiveInfo"sv,
        "WebGLBuffer"sv,
        "WebGLFramebuffer"sv,
        "WebGLObject"sv,
        "WebGLProgram"sv,
        "WebGLQuery"sv,
        "WebGLRenderbuffer"sv,
        "WebGLRenderingContext"sv,
        "WebGLSampler"sv,
        "WebGLShader"sv,
        "WebGLShaderPrecisionFormat"sv,
        "WebGLSync"sv,
        "WebGLTexture"sv,
        "WebGLTransformFeedback"sv,
        "WebGLUniformLocation"sv,
        "WebGLVertexArrayObject"sv,
        "WebGLVertexArrayObjectOES"sv,
        "Window"sv,
        "WindowProxy"sv,
        "WritableStream"sv,
        "XPathResult"sv,
        "XRSession"sv,
    };
    if (type.name().ends_with("Element"sv))
        return true;
    if (type.name().ends_with("Event"sv))
        return true;
    if (types.span().contains_slow(type.name()))
        return true;
    return false;
}

// FIXME: Generate this automatically somehow.
bool is_javascript_builtin(Type const& type)
{
    // NOTE: This is a hand-curated subset of JavaScript built-in types that are actually relevant
    // in places where this function is used. If you add IDL code and get compile errors, you
    // might simply need to add another type here.
    constexpr Array types = {
        "ArrayBuffer"sv,
        "Float16Array"sv,
        "Float32Array"sv,
        "Float64Array"sv,
        "Int32Array"sv,
        "Uint8Array"sv,
        "Uint32Array"sv,
        "Uint8ClampedArray"sv,
    };

    return types.span().contains_slow(type.name());
}

Interface const* callback_interface_for_type(Interface const& interface, Type const& type)
{
    auto const* referenced_interface = interface.referenced_interface(type.name());
    if (referenced_interface && referenced_interface->is_callback_interface)
        return referenced_interface;
    return nullptr;
}

StringView sequence_storage_type_to_cpp_storage_type_name(SequenceStorageType sequence_storage_type)
{
    switch (sequence_storage_type) {
    case SequenceStorageType::Vector:
        return "Vector"sv;
    case SequenceStorageType::RootVector:
        return "GC::RootVector"sv;
    default:
        VERIFY_NOT_REACHED();
    }
}

bool is_nullable_frozen_array_of_single_type(Type const& type, StringView type_name)
{
    if (!type.is_nullable() || type.name() != "FrozenArray"sv)
        return false;

    auto const& parameters = type.as_parameterized().parameters();
    if (parameters.size() != 1)
        return false;

    return parameters.first()->name() == type_name;
}

ByteString union_type_to_variant(UnionType const& union_type, Interface const& interface)
{
    StringBuilder builder;
    builder.append("Variant<"sv);

    auto flattened_types = union_type.flattened_member_types();
    for (size_t type_index = 0; type_index < flattened_types.size(); ++type_index) {
        auto& type = flattened_types.at(type_index);

        if (type_index > 0)
            builder.append(", "sv);

        auto cpp_type = idl_type_name_to_cpp_type(type, interface);
        builder.append(cpp_type.name);
    }

    if (union_type.includes_undefined())
        builder.append(", Empty"sv);

    builder.append('>');
    return builder.to_byte_string();
}

CppType idl_type_name_to_cpp_type(Type const& type, Interface const& interface)
{
    if (is_platform_object(type))
        return { .name = ByteString::formatted("GC::Root<{}>", type.name()), .sequence_storage_type = SequenceStorageType::RootVector };

    if (is_javascript_builtin(type))
        return { .name = ByteString::formatted("GC::Root<JS::{}>", type.name()), .sequence_storage_type = SequenceStorageType::RootVector };

    if (auto const* callback_interface = callback_interface_for_type(interface, type))
        return { .name = ByteString::formatted("GC::Root<{}>", callback_interface->implemented_name), .sequence_storage_type = SequenceStorageType::RootVector };

    if (interface.callback_functions.contains(type.name()))
        return { .name = "GC::Root<WebIDL::CallbackType>", .sequence_storage_type = SequenceStorageType::RootVector };

    if (type.is_string()) {
        if (type.name().contains("Utf16"sv))
            return { .name = "Utf16String", .sequence_storage_type = SequenceStorageType::Vector };
        return { .name = "String", .sequence_storage_type = SequenceStorageType::Vector };
    }

    if ((type.name() == "double" || type.name() == "unrestricted double") && !type.is_nullable())
        return { .name = "double", .sequence_storage_type = SequenceStorageType::Vector };

    if ((type.name() == "float" || type.name() == "unrestricted float") && !type.is_nullable())
        return { .name = "float", .sequence_storage_type = SequenceStorageType::Vector };

    if (type.name() == "boolean" && !type.is_nullable())
        return { .name = "bool", .sequence_storage_type = SequenceStorageType::Vector };

    if (type.name() == "unsigned long" && !type.is_nullable())
        return { .name = "WebIDL::UnsignedLong", .sequence_storage_type = SequenceStorageType::Vector };

    if (type.name() == "short" && !type.is_nullable())
        return { .name = "WebIDL::Short", .sequence_storage_type = SequenceStorageType::Vector };

    if (type.name() == "unsigned short" && !type.is_nullable())
        return { .name = "WebIDL::UnsignedShort", .sequence_storage_type = SequenceStorageType::Vector };

    if (type.name() == "long long" && !type.is_nullable())
        return { .name = "WebIDL::LongLong", .sequence_storage_type = SequenceStorageType::Vector };

    if (type.name() == "unsigned long long" && !type.is_nullable())
        return { .name = "WebIDL::UnsignedLongLong", .sequence_storage_type = SequenceStorageType::Vector };

    if (type.name() == "long" && !type.is_nullable())
        return { .name = "WebIDL::Long", .sequence_storage_type = SequenceStorageType::Vector };

    if (type.name() == "any")
        return { .name = "JS::Value", .sequence_storage_type = SequenceStorageType::RootVector };

    // NOTE: undefined is a somewhat special case that may be used in a union to represent the javascript 'undefined' (and
    //       only ever js_undefined). Therefore, we say that the type is Empty here, so that a union of (T, undefined) is
    //       generated as Variant<T, Empty>, which is then returned in the Variant's visit as undefined if it is Empty.
    if (type.name() == "undefined")
        return { .name = "Empty", .sequence_storage_type = SequenceStorageType::RootVector };

    if (type.name() == "object")
        return { .name = "GC::Root<JS::Object>", .sequence_storage_type = SequenceStorageType::Vector };

    if (type.name() == "BufferSource")
        return { .name = "GC::Root<WebIDL::BufferSource>", .sequence_storage_type = SequenceStorageType::RootVector };

    if (type.name() == "ArrayBufferView")
        return { .name = "GC::Root<WebIDL::ArrayBufferView>", .sequence_storage_type = SequenceStorageType::RootVector };

    if (type.name() == "File")
        return { .name = "GC::Root<FileAPI::File>", .sequence_storage_type = SequenceStorageType::RootVector };

    if (type.name() == "Function")
        return { .name = "GC::Ref<WebIDL::CallbackType>", .sequence_storage_type = SequenceStorageType::RootVector };

    if (type.name() == "Promise")
        return { .name = "GC::Root<WebIDL::Promise>", .sequence_storage_type = SequenceStorageType::RootVector };

    if (type.name() == "MediaSource")
        return { .name = "GC::Root<MediaSourceExtensions::MediaSource>", .sequence_storage_type = SequenceStorageType::RootVector };

    if (type.name().is_one_of("sequence"sv, "FrozenArray"sv)) {
        auto& parameterized_type = as<ParameterizedType>(type);
        auto& sequence_type = parameterized_type.parameters().first();
        auto sequence_cpp_type = idl_type_name_to_cpp_type(sequence_type, interface);
        auto storage_type_name = sequence_storage_type_to_cpp_storage_type_name(sequence_cpp_type.sequence_storage_type);

        if (sequence_cpp_type.sequence_storage_type == SequenceStorageType::RootVector)
            return { .name = storage_type_name, .sequence_storage_type = SequenceStorageType::Vector };

        return { .name = ByteString::formatted("{}<{}>", storage_type_name, sequence_cpp_type.name), .sequence_storage_type = SequenceStorageType::Vector };
    }

    if (type.name() == "record") {
        auto& parameterized_type = as<ParameterizedType>(type);
        auto& record_key_type = parameterized_type.parameters()[0];
        auto& record_value_type = parameterized_type.parameters()[1];
        auto record_key_cpp_type = idl_type_name_to_cpp_type(record_key_type, interface);
        auto record_value_cpp_type = idl_type_name_to_cpp_type(record_value_type, interface);

        return { .name = ByteString::formatted("OrderedHashMap<{}, {}>", record_key_cpp_type.name, record_value_cpp_type.name), .sequence_storage_type = SequenceStorageType::Vector };
    }

    if (is<UnionType>(type)) {
        auto& union_type = as<UnionType>(type);
        return { .name = union_type_to_variant(union_type, interface), .sequence_storage_type = SequenceStorageType::Vector };
    }

    if (!type.is_nullable()) {
        for (auto& dictionary : interface.dictionaries) {
            if (type.name() == dictionary.key)
                return { .name = type.name(), .sequence_storage_type = SequenceStorageType::Vector };
        }
    }

    if (interface.enumerations.contains(type.name()))
        return { .name = type.name(), .sequence_storage_type = SequenceStorageType::Vector };

    dbgln("Unimplemented type for idl_type_name_to_cpp_type: {}{}", type.name(), type.is_nullable() ? "?" : "");
    TODO();
}

ByteString make_input_acceptable_cpp(ByteString const& input)
{
    if (input.is_one_of(
            "break",
            "char",
            "class",
            "continue",
            "default",
            "delete",
            "for",
            "initialize",
            "inline",
            "mutable",
            "namespace",
            "operator",
            "register",
            "switch",
            "template")) {
        StringBuilder builder;
        builder.append(input);
        builder.append('_');
        return builder.to_byte_string();
    }

    return input.replace("-"sv, "_"sv, ReplaceMode::All);
}

}
