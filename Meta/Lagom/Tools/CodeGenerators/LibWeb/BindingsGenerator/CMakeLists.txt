set(SOURCES
    CodeGeneratorUtils.cpp
    ConstructorGenerator.cpp
    DictionaryGenerator.cpp
    EnumGenerator.cpp
    FunctionGenerator.cpp
    NamespaceGenerator.cpp
    PrototypeGenerator.cpp
    TypeConversion.cpp
    main.cpp
)

lagom_tool(BindingsGenerator LIBS LibIDL LibMain)
target_compile_options(BindingsGenerator PUBLIC -g)
