
#include <iostream>
#include <fmt/ostream.h>
#include <vector>

#include <xl/templates.h>
using namespace xl::templates;


#include "../wrapped_class.h"
#include "javascript_stub_output.h"
#include "../log.h"


namespace v8toolkit::class_parser::javascript_stub_output {

extern Template class_template;
extern Template constructor_template;
extern Template file_template;
extern Template header_template;
extern Template member_function_template;
extern Template static_function_template;

extern std::map<std::string, Template> templates;

std::ostream & JavascriptStubOutputStreamProvider::get_class_collection_stream() {
    if (!this->output_stream) {
        this->output_stream = std::make_unique<std::ofstream>("js-api.js");
    }
    return *this->output_stream;
}

JavascriptStubOutputStreamProvider::~JavascriptStubOutputStreamProvider() {
}


struct JavascriptStubProviderContainer {

    using P = xl::templates::DefaultProviders<JavascriptStubProviderContainer>;

    static ProviderPtr get_provider(WrappedClass const & c) {

        v8toolkit::class_parser::log.info(LogSubjects::Subjects::JavaScriptStubOutput, "Making provider for wrapped class: {}", c.get_name_alias());
        return P::make_provider(
            std::pair("comment", c.comment),
            std::pair("name", c.get_jsdoc_name()),
            std::pair("data_members", std::ref(c.get_members())),
            std::pair("constructors", std::ref(c.get_constructors())),

            std::pair("member_functions", xl::erase_if(xl::copy(c.get_member_functions()),
                                                 [](std::unique_ptr<MemberFunction> const & member_function){
                                                     return member_function->is_callable_overload();
                                                 })),

            std::pair("static_functions", std::ref(c.get_static_functions())),
            std::pair("inheritance", fmt::format("{}", c.base_types.empty() ? "" : " extends " +
                                                                                   (*c.base_types.begin())->get_jsdoc_name()))
        );
    }


    static ProviderPtr get_provider(DataMember const & d) {
        v8toolkit::class_parser::log.info(LogSubjects::Subjects::JavaScriptStubOutput, "Making provider for data member: {}", d.long_name);

        return P::make_provider(
            std::pair("comment", d.comment),
            std::pair("name", d.js_name),
            std::pair("type", d.type.get_jsdoc_type_name())
        );
    }


    static ProviderPtr get_provider(ConstructorFunction const & f) {
        v8toolkit::class_parser::log.info(LogSubjects::Subjects::JavaScriptStubOutput, "Making provider for constructor: {}", f.name);

        return P::make_provider(
            std::pair("comment", f.comment),
            std::pair("parameters", P::make_provider(f.parameters))
        );
    }


    static ProviderPtr get_provider(MemberFunction const & f) {
        v8toolkit::class_parser::log.info(LogSubjects::Subjects::JavaScriptStubOutput, "Making provider for member function: {}", f.name);

        return P::make_provider(
            std::pair("name", f.js_name),
            std::pair("comment", f.comment),
            std::pair("parameters", P::make_provider(f.parameters)),
            std::pair("return_type_name", f.return_type.get_jsdoc_type_name()),
            std::pair("return_comment", f.return_type_comment)
        );
    }


    static ProviderPtr get_provider(StaticFunction const & f) {
        v8toolkit::class_parser::log.info(LogSubjects::Subjects::JavaScriptStubOutput, "Making provider for static function: {}", f.name);

        return P::make_provider(
            std::pair("name", f.js_name),
            std::pair("comment", f.comment),
            std::pair("parameters", P::make_provider(f.parameters)),
            std::pair("return_type_name", f.return_type.get_jsdoc_type_name()),
            std::pair("return_comment", f.return_type_comment)
        );
    }


    static ProviderPtr get_provider(ClassFunction::ParameterInfo const & p) {
        v8toolkit::class_parser::log.info(LogSubjects::Subjects::JavaScriptStubOutput, "Making provider for parameter info: {}", p.name);

        return P::make_provider(
            std::pair("type", p.type.get_jsdoc_type_name()),
            std::pair("name", p.name),
            std::pair("comment", p.description)
        );
    }


    static ProviderPtr get_provider(ClassFunction::TypeInfo const & t) {
        return P::make_provider("Implement me");

    }

}; // end BindingsProviderContainer

JavascriptStubOutputModule::JavascriptStubOutputModule() :
    OutputModule(std::make_unique<JavascriptStubOutputStreamProvider>())
{}

JavascriptStubOutputModule::JavascriptStubOutputModule(std::unique_ptr<OutputStreamProvider> output_stream_provider) :
    OutputModule(std::move(output_stream_provider))
{}


void JavascriptStubOutputModule::process(std::vector<WrappedClass const *> const & wrapped_classes) {
    v8toolkit::class_parser::log.info(LogSubjects::Subjects::JavaScriptStubOutput, "Starting Javascript Stub output module");

    auto result = templates["file"].fill<JavascriptStubProviderContainer>(make_provider<JavascriptStubProviderContainer>(std::pair("classes", wrapped_classes)), templates);
//        auto result = templates["file"].fill(xl::templates::Provider(std::pair("classes", [&wrapped_classes]()->auto&{return wrapped_classes;})), templates);

//    std::cerr << result << std::endl;
    output_stream_provider->get_class_collection_stream() << result << std::endl;
}

string JavascriptStubOutputModule::get_name() {
    return "JavascriptStubOutputModule";
}


Template class_template(R"(
/**
 * {{<comment}}
 * @class {{name}}
{{<data_members|!!
 * @property {{name}} {{comment}}}}
 */
class {{name}}{{inheritance}}
{
{{constructors|constructor}}
{{member_functions|member_function}}
{{static_functions|static_function}}
} // end class {{name}}

)");


Template constructor_template(R"(
    /**
     * {{<comment}}
{{<parameters|!!
     * @param \{{{type}}\} {{name}} {{comment}}}}
     */
    constructor({{parameters%, |!{{name}}}}) {})");


Template file_template(R"(
{{!header}}

{{classes|class}}
)");


Template header_template(R"(

/**
 * @type World
 */
var world;

/**
 * @type Map
 */
var map;

/**
 * @type Game
 */
var game;

/**
 * Prints a string and appends a newline
 * @param {String} str the string to be printed
 */
function println(str){}

/**
 * Prints a string without adding a newline to the end
 * @param {String} str the string to be printed
 */
function print(str){}

/**
 * Dumps the contents of the given variable - only 'own' properties
 * @param {Object} obj the variable to be dumped
 */
function printobj(obj){}

/**
 * Dumps the contents of the given variable - all properties including those of prototype chain
 * @param {Object} obj the variable to be dumped
 */
function printobjall(obj){}

/**
 * Attempts to load the given module and returns the exported data.  Requiring the same module
 *   more than once will return the cached os, not re-execute the source.
 * @param {String} module_name name of the module to require
 */
function require(module_name){}

)");


Template member_function_template(R"(
    /**
     * {{<comment}}
{{<parameters|!!
     * @param \{{{type}}\} {{name}} {{comment}}}}
     * @return \{{{return_type_name}}\} {{return_comment}}
     */
    {{name}}({{parameters%, |!{{name}}}}) {})");


Template static_function_template(R"(
    /**
     * {{<comment}}
{{<parameters|!!
     * @param \{{{type}}\} {{name}} {{comment}}}}
     * @return \{{{return_type_name}}\} {{return_comment}}
     */
    static {{name}}({{parameters%, |!{{name}}}}) {})");


std::map<std::string, Template> templates {
    std::pair("class", class_template),
    std::pair("constructor", constructor_template),
    std::pair("file", file_template),
    std::pair("header", header_template),
    std::pair("member_function", member_function_template),
    std::pair("static_function", static_function_template),
};


} // end namespace v8toolkit::class_parser::javascript_stub_output

namespace v8toolkit::class_parser {

void OutputModule::_begin() {
    this->callback = &v8toolkit::class_parser::log.add_callback(std::ref(this->log_watcher));

    this->begin();
}


void OutputModule::_end() {
    this->end();
    v8toolkit::class_parser::log.remove_callback(*this->callback);
}


OutputModule::OutputModule(std::unique_ptr<OutputStreamProvider> output_stream_provider) :
    output_stream_provider(std::move(output_stream_provider))
{

}


} // end namespace v8toolkit::class_parser