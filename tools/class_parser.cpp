

/**
 * This clang plugin looks for classes annotated with V8TOOLKIT_WRAPPED_CLASS and/or V8TOOLKIT_BIDIRECTIONAL_CLASS
 * and automatically generates source files for class wrappings and/or JSWrapper objects for those classes.
 *
 * Each JSWrapper object type will go into its own .h file called v8toolkit_generated_bidirectional_<ClassName>.h
 *   These files should be included from within the header file defining the class.
 *
 * MISSING DOCS FOR CLASS WRAPPER CODE GENERATION
 */

// This program will only work with clang but the output should be useable on any platform.

/**
 * KNOWN BUGS:
 * Doesn't properly understand virtual methods and will duplicate them across the inheritence hierarchy
 * Doesn't include root file of compilation - if this is pretty safe in a unity build, as the root file is a file that
 *   just includes all the other files
 */


/**
How to run over complete code base using cmake + cotire
add_library(api-gen-template OBJECT ${YOUR_SOURCE_FILES})
target_compile_options(api-gen-template
        PRIVATE -Xclang -ast-dump -fdiagnostics-color=never <== this isn't right yet, this just dumps the ast
        )
set_target_properties(api-gen-template PROPERTIES COTIRE_UNITY_TARGET_NAME "api-gen")
cotire(api-gen-template)
 */

// Having this too high can lead to VERY memory-intensive compilation units
// Single classes (+base classes) with more than this number of declarations will still be in one file.
#define MAX_DECLARATIONS_PER_FILE 50

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <vector>
#include <regex>

#include <cppformat/format.h>


#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"

#include "class_parser.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::driver;
using namespace clang::tooling;

using namespace std;

//#define PRINT_SKIPPED_EXPORT_REASONS true
#define PRINT_SKIPPED_EXPORT_REASONS false

int classes_wrapped = 0;
int methods_wrapped = 0;

namespace {



//
//    std::string decl2str(const clang::Decl *d, SourceManager &sm) {
//        // (T, U) => "T,,"
//        std::string text = Lexer::getSourceText(CharSourceRange::getTokenRange(d->getSourceRange()), sm, LangOptions(), 0);
//        if (text.at(text.size()-1) == ',')
//            return Lexer::getSourceText(CharSourceRange::getCharRange(d->getSourceRange()), sm, LangOptions(), 0);
//        return text;
//    }

    std::string get_source_for_source_range(SourceManager & sm, SourceRange source_range) {
        std::string text = Lexer::getSourceText(CharSourceRange::getTokenRange(source_range), sm, LangOptions(), 0);
        if (text.at(text.size()-1) == ',')
            return Lexer::getSourceText(CharSourceRange::getCharRange(source_range), sm, LangOptions(), 0);
        return text;
    }

    vector<string> count_top_level_template_parameters(const std::string & source) {
        int open_angle_count = 0;
        vector<string> parameter_strings;
        std::string * current;
        for (char c : source) {
            if (isspace(c)) {
                continue;
            }
            if (c == '<') {
                open_angle_count++;
                if (open_angle_count > 1) {
                    *current += c;
                }
            } else if (c == '>') {
                open_angle_count--;
                if (open_angle_count > 0) {
                    *current += c;
                }
            } else {
                if (open_angle_count == 1) {
                    if (parameter_strings.size() == 0) {
                        parameter_strings.push_back("");
                        current = &parameter_strings.back();
                    }
                    if (c == ',') {
                        parameter_strings.push_back("");
                        current = &parameter_strings.back();
                        if (open_angle_count > 1) {
                            *current += c;
                        }
                    } else {
                        *current += c;
                    }
                } else if (open_angle_count > 1) {
                    *current += c;
                }
            }
        }
        cerr << "^^^^^^^^^^^^^^^ Counted " << parameter_strings.size() << " for " << source << endl;
        for (auto & str : parameter_strings) {
            cerr <<  "^^^^^^^^^^^^^^^" << str << endl;
        }
        return parameter_strings;
    }

    struct WrappedClass {
        string class_name;
        set<string> include_files;
        int declaration_count = 0;
        std::stringstream contents;
        set<string> names;
        set<string> compatible_types;
        set<string> parent_types;
        WrappedClass(const std::string & class_name) : class_name(class_name) {}
    };



    // returns a vector of all the annotations on a Decl
    std::vector<std::string> get_annotations(const Decl * decl) {
        std::vector<std::string> results;
        for (auto attr : decl->getAttrs()) {
            AnnotateAttr * annotation =  dyn_cast<AnnotateAttr>(attr);
            if (annotation) {
                auto attribute_attr = dyn_cast<AnnotateAttr>(attr);
                auto annotation_string = attribute_attr->getAnnotation().str();
                //cerr << "Got annotation " << annotation_string << endl;
                results.emplace_back(annotation->getAnnotation().str());
            }
        }
        return results;
    }

    std::vector<std::string> get_annotation_regex(const Decl * decl, const std::string & regex_string) {
        auto regex = std::regex(regex_string);
        std::vector<std::string> results;

        auto annotations = get_annotations(decl);
        for (auto & annotation : annotations) {
            std::smatch matches;
            if (std::regex_match(annotation, matches, regex)) {
//                printf("GOT %d MATCHES\n", (int)matches.size());
                if (matches.size() > 1) {
                    results.push_back(matches[1]);
                }
            }
        }
        return results;
    }





    bool has_annotation(const Decl * decl, const std::string & target) {
        auto annotations = get_annotations(decl);
        return std::find(annotations.begin(), annotations.end(), target) != annotations.end();
    }


    enum EXPORT_TYPE {
        EXPORT_UNSPECIFIED = 0,
        EXPORT_NONE, // export nothing
        EXPORT_SOME, // only exports specifically marked entities
        EXPORT_EXCEPT, // exports everything except specifically marked entities
        EXPORT_ALL}; // exports everything

    EXPORT_TYPE get_export_type(const Decl * decl, EXPORT_TYPE previous = EXPORT_UNSPECIFIED) {
        auto &attrs = decl->getAttrs();
        EXPORT_TYPE export_type = previous;

        for (auto attr : attrs) {
            if (dyn_cast<AnnotateAttr>(attr)) {
                auto attribute_attr = dyn_cast<AnnotateAttr>(attr);
                auto annotation_string = attribute_attr->getAnnotation().str();

                if (annotation_string == V8TOOLKIT_ALL_STRING) {
                    export_type = EXPORT_ALL;
                } else if (annotation_string == "v8toolkit_generate_bindings_some") {
                    export_type = EXPORT_SOME;
                } else if (annotation_string == "v8toolkit_generate_bindings_except") {
                    export_type = EXPORT_EXCEPT;
                } else if (annotation_string == V8TOOLKIT_NONE_STRING) {
                    export_type = EXPORT_NONE; // just for completeness
                }
            }
        }
//            printf("Returning export type: %d\n", export_type);
        return export_type;
    }




    // Finds where file_id is included, how it was included, and returns the string to duplicate
    //   that inclusion
    std::string get_include_string_for_fileid(SourceManager & source_manager, FileID & file_id) {
        auto include_source_location = source_manager.getIncludeLoc(file_id);

        // If it's in the "root" file (file id 1), then there's no include for it
        if (include_source_location.isValid()) {
            auto header_file = include_source_location.printToString(source_manager);
//            cerr << "include source location: " << header_file << endl;
            //            wrapped_class.include_files.insert(header_file);
        } else {
//            cerr << "No valid source location" << endl;
            return "";
        }

        bool invalid;
        // This gets EVERYTHING after the start of the filename in the include.  "asdf.h"..... or <asdf.h>.....
        const char * text = source_manager.getCharacterData(include_source_location, &invalid);
        const char * text_end = text + 1;
        while(*text_end != '>' && *text_end != '"') {
            text_end++;
        }

        return string(text, (text_end - text) + 1);

    }

    std::string get_include_for_record_decl(SourceManager & source_manager, const CXXRecordDecl * record_decl) {
        auto full_source_loc = FullSourceLoc(record_decl->getLocStart(), source_manager);

        auto file_id = full_source_loc.getFileID();
        return get_include_string_for_fileid(source_manager, file_id);
    }

    template<class T>
    std::string join(const T & source, const std::string & between = ", ", bool leading_between = false) {
        if (source.empty()) {
            return "";
        }
        stringstream result;
        if (leading_between) {
            result << between;
        }
        bool first = true;
        for (auto & str : source) {
            if (!first) { result << between;}
            first = false;
            result << str;
        }
        return result.str();
    }


//    std::string strip_path_from_filename(const std::string & filename) {
//
//        // naive regex to grab everything after the last slash or backslash
//        auto regex = std::regex("([^/\\\\]*)$");
//
//        std::smatch matches;
//        if (std::regex_search(filename, matches, regex)) {
//            return matches[1];
//        }
//        cerr << fmt::format("Unrecognizable filename {}", filename);
//        throw std::exception();
//    }

    std::string handle_std(const std::string & input) {
        smatch matches;
        regex_match(input, matches, regex("^((?:const\\s+|volatile\\s+)*)(?:class |struct )?(?:std::(?:__1::)?)?(.*)"));
        // space before std:: is handled from const/volatile if needed
        auto result = matches[1].str() + "std::" + matches[2].str();

        cerr << "Stripping std from " << input << " results in " << result << endl;
        return result;
    }

    bool has_std(const std::string & input) {
        return std::regex_match(input, regex("^(const\\s+|volatile\\s+)*(class |struct )?\\s*std::.*$"));
    }


    // Returns true if qual_type is a 'trivial' std:: type like
    //   std::string
    bool is_trivial_std_type(QualType & qual_type, std::string & output) {
        std::string name = qual_type.getAsString();
        std::string canonical_name = qual_type.getCanonicalType().getAsString();

        // if it's a std:: type and not explicitly user-specialized, pass it through
        if (std::regex_match(name, regex("^(const\\s+|volatile\\s+)*(class |struct )?std::[^<]*$"))) {
            output = handle_std(name);
            return true;
        }
        // or if the canonical type has std:: in it and it's not user-customized
        else if (has_std(canonical_name) &&
                 std::regex_match(name, regex("^[^<]*$"))) {
            output = handle_std(name);
            return true;
        }
        return false;
    }

    // Returns true if qual_type is a 'non-trivial' std:: type (containing user-specified template types like
    //   std::map<MyType1, MyType2>
    bool is_nontrivial_std_type(QualType & qual_type, std::string & output) {

        std::string name = qual_type.getAsString();
        std::string canonical_name = qual_type.getCanonicalType().getAsString();
        cerr << "Checking nontrivial std type on " << name << " : " << canonical_name << endl;
        smatch matches;


        // if it's in standard (according to its canonical type) and has user-specified types
        if (has_std(canonical_name) &&
                 std::regex_match(name, matches, regex("^([^<]*<).*$"))) {
            output = handle_std(matches[1].str());
            cerr << "Yes" << endl;
            return true;
        }
        cerr << "No" << endl;
        return false;
    }




    std::string get_type_string(QualType qual_type,
                                const std::string & source,
                                const std::string & indentation = "") {
        auto original_qual_type = qual_type;
        cerr << indentation << "Started at " << qual_type.getAsString() << endl;
        cerr << indentation << "  And canonical name: " << qual_type.getCanonicalType().getAsString() << endl;
        cerr << indentation << "  And source " << source << endl;

        std::string std_result;
        if (is_trivial_std_type(qual_type, std_result)) {
            cerr << indentation << "Returning trivial std:: type: " << std_result << endl << endl;
            return std_result;
        }

//        auto original_qual_type = qual_type;

        bool is_reference = qual_type->isReferenceType();
        string reference_suffix = is_reference ? "&" : "";
        qual_type = qual_type.getNonReferenceType();

        stringstream pointer_suffix;
        bool changed = true;
        while(changed) {
            changed = false;
            if (!qual_type->getPointeeType().isNull()) {
                changed = true;
                pointer_suffix << "*";
                qual_type = qual_type->getPointeeType();
                cerr << indentation << "stripped pointer, went to: " << qual_type.getAsString() << endl;
                continue; // check for more pointers first
            }

            // This code traverses all the typdefs and pointers to get to the actual base type
            if (dyn_cast<TypedefType>(qual_type) != nullptr) {
                changed = true;
                cerr << indentation << "stripped typedef, went to: " << qual_type.getAsString() << endl;
                qual_type = dyn_cast<TypedefType>(qual_type)->getDecl()->getUnderlyingType();
            }
        }

        cerr << indentation << "CHECKING TO SEE IF " << qual_type.getUnqualifiedType().getAsString() << " is a template specialization"<< endl;
        auto base_type_record_decl = qual_type.getUnqualifiedType()->getAsCXXRecordDecl();
        if (dyn_cast<ClassTemplateSpecializationDecl>(base_type_record_decl)) {



            cerr << indentation << "!!!!! Started with template specialization: " << qual_type.getAsString() << endl;
            stringstream result;

            std::smatch matches;
            string qual_type_string = qual_type.getAsString();

            std::string std_type_output;
            bool nontrivial_std_type = false;
            if (is_nontrivial_std_type(qual_type, std_type_output)) {
                cerr << indentation << "is nontrivial std type and got result: " << std_type_output << endl;
                nontrivial_std_type = true;
                result << std_type_output;
            }
            // Get everything EXCEPT the template parameters into matches[1] and [2]
            else if (!regex_match(qual_type_string, matches, regex("^([^<]+<).*(>[^>]*)$"))) {
                cerr << indentation << "ERROR: Template type must match regex" << endl;
            } else {
                result << matches[1];
                cerr << indentation << "is NOT nontrivial std type" << endl;
            }
            auto template_specialization_decl = dyn_cast<ClassTemplateSpecializationDecl>(base_type_record_decl);

            auto user_specified_template_parameters = count_top_level_template_parameters(source);


            auto & template_arg_list = template_specialization_decl->getTemplateArgs();
            if (user_specified_template_parameters.size() > template_arg_list.size()) {
                cerr << "ERROR: detected template parameters > actual list size" << endl;
            }

//            for (decltype(template_arg_list.size()) i = 0; i < template_arg_list.size(); i++) {
            for (decltype(template_arg_list.size()) i = 0; i < user_specified_template_parameters.size(); i++) {
                if (i > 0) {
                    result << ", ";
                }
                cerr << indentation << "Working on template parameter " << i << endl;
                auto & arg = template_arg_list[i];

                switch(arg.getKind()) {
                    case clang::TemplateArgument::Type: {
                        cerr << indentation << "processing as type argument" << endl;
                        auto template_arg_qual_type = arg.getAsType();
                        auto template_type_string = get_type_string(template_arg_qual_type,
                                                  user_specified_template_parameters[i],
                                                  indentation + "  ");
                        cerr << indentation << "About to append " << template_type_string << " template type string onto existing: " << result.str() << endl;
                        result << template_type_string;
                        break; }
                    case clang::TemplateArgument::Integral: {
                        cerr << indentation << "processing as integral argument" << endl;
                        auto integral_value = arg.getAsIntegral();
                        cerr << indentation << "integral value radix10: " << integral_value.toString(10) << endl;
                        result << integral_value.toString(10);
                        break;}
                    default:
                        cerr << indentation << "Oops, unhandled argument type" << endl;
                }
            }
            result << ">" << pointer_suffix.str() << reference_suffix;
            cerr << indentation << "!!!!!Finished stringifying templated type to: " << result.str() << endl << endl;
            return result.str();

//        } else if (std::regex_match(qual_type.getAsString(), regex("^(class |struct )?std::.*$"))) {
//
//
//            cerr << indentation << "checking " << qual_type.getAsString();
//            if (dyn_cast<TypedefType>(qual_type)) {
//                cerr << indentation << " and returning " << dyn_cast<TypedefType>(qual_type)->getDecl()->getQualifiedNameAsString() <<
//                endl << endl;
//                return dyn_cast<TypedefType>(qual_type)->getDecl()->getQualifiedNameAsString() +
//                       (is_reference ? " &" : "");
//            } else {
//                cerr << indentation << " and returning (no typedef) " << qual_type.getAsString() << endl << endl;
//                return qual_type.getAsString() + pointer_suffix.str() + reference_suffix;
//            }

        } else {

            // THIS APPROACH DOES NOT GENERATE PORTABLE STL NAMES LIKE THE LINE BELOW IS libc++ only not libstdc++
            // std::__1::basic_string<char, struct std::__1::char_traits<char>, class std::__1::allocator<char> >

            // this isn't great because it loses the typedef'd names of things, but it ALWAYS works
            // There is no confusion with reference types or typedefs or const/volatile
            // EXCEPT: it generates a elaborated type specifier which can't be used in certain places
            // http://en.cppreference.com/w/cpp/language/elaborated_type_specifier
            auto canonical_qual_type = original_qual_type.getCanonicalType();

            //printf("Canonical qualtype typedeftype cast: %p\n",(void*) dyn_cast<TypedefType>(canonical_qual_type));

            cerr << indentation << "returning canonical: " << canonical_qual_type.getAsString() << endl << endl;

            return canonical_qual_type.getAsString();
        }
    }

    // Gets the "most basic" type in a type.   Strips off ref, pointer, CV
    //   then calls out to get how to include that most basic type definition
    //   and puts it in wrapped_class.include_files
    void update_wrapped_class_for_type(SourceManager & source_manager,
                                       WrappedClass & wrapped_class,
                                       // don't capture qualtype by ref since it is changed
                                       QualType qual_type) {

//        cerr << "Went from " << qual_type.getAsString();
        qual_type = qual_type.getLocalUnqualifiedType();

        while(!qual_type->getPointeeType().isNull()) {
            qual_type = qual_type->getPointeeType();
        }
        qual_type = qual_type.getLocalUnqualifiedType();

//        cerr << " to " << qual_type.getAsString() << endl;
        auto base_type_record_decl = qual_type->getAsCXXRecordDecl();

        // primitive types don't have record decls
        if (base_type_record_decl == nullptr) {
            return;
        }

        auto full_source_loc = FullSourceLoc(base_type_record_decl->getLocStart(), source_manager);

        auto file_id = full_source_loc.getFileID();

        auto actual_include_string = get_include_string_for_fileid(source_manager, file_id);

        cerr << "Got include string for " << qual_type.getAsString() << ": " << actual_include_string << endl;
        wrapped_class.include_files.insert(actual_include_string);

        if (dyn_cast<ClassTemplateSpecializationDecl>(base_type_record_decl)) {

//            cerr << "##!#!#!#!# Oh shit, it's a template type " << qual_type.getAsString() << endl;

            auto template_specialization_decl = dyn_cast<ClassTemplateSpecializationDecl>(base_type_record_decl);

            auto & template_arg_list = template_specialization_decl->getTemplateArgs();
            for (decltype(template_arg_list.size()) i = 0; i < template_arg_list.size(); i++) {
                auto & arg = template_arg_list[i];

                // this code only cares about types
                if (arg.getKind() != clang::TemplateArgument::Type) {
                    continue;
                }
                auto template_arg_qual_type = arg.getAsType();
                if (template_arg_qual_type.isNull()) {
//                    cerr << "qual type is null" << endl;
                    continue;
                } else {
//                    cerr << "Recursing on templated type " << template_arg_qual_type.getAsString() << endl;
                }
                update_wrapped_class_for_type(source_manager, wrapped_class, template_arg_qual_type);

            }
        } else {
//            cerr << "Not a template specializaiton type " << qual_type.getAsString() << endl;
        }

//
//        auto header_file = strip_path_from_filename(source_manager.getFilename(full_source_loc).str());
//        cerr << fmt::format("{} needs {}", wrapped_class.class_name, header_file) << endl;
//        wrapped_class.include_files.insert(header_file);

    }


    vector<pair<QualType, SourceRange>> get_method_param_qual_types(const CXXMethodDecl * method,
                                                 const string & annotation = "") {
        vector<pair<QualType, SourceRange>> results;
        auto parameter_count = method->getNumParams();
        for (unsigned int i = 0; i < parameter_count; i++) {
            auto param_decl = method->getParamDecl(i);
            if (annotation != "" && !has_annotation(param_decl, annotation)) {
                cerr << "Skipping method parameter because it didn't have requested annotation: " << annotation << endl;
                continue;
            }
            auto param_qual_type = param_decl->getType();
            results.push_back(make_pair(param_qual_type, param_decl->getSourceRange()));
            cerr << "Got " << (param_decl->getSourceRange().isValid() ? "valid" : "invalid") << " source range for " << param_qual_type.getAsString() << endl;
        }
        return results;
    }

    vector<string> generate_variable_names(std::size_t count) {
        vector<string> results;
        for (std::size_t i = 0; i < count; i++) {
            results.push_back(fmt::format("var{}", i+1));
        }
        return results;
    }

    std::string get_method_parameters(SourceManager & source_manager,
                                      WrappedClass & wrapped_class,
                                      const CXXMethodDecl * method,
                                      bool add_leading_comma = false,
                                      bool insert_variable_names = false,
                                      const string & annotation = "") {
        std::stringstream result;
        bool first_param = true;
        auto type_list = get_method_param_qual_types(method, annotation);

        if (!type_list.empty() && add_leading_comma) {
            result << ", ";
        }
        int count = 0;
        auto var_names = generate_variable_names(type_list.size());
        for (auto & param_qual_type_pair : type_list) {

            if (!first_param) {
                result << ", ";
            }
            first_param = false;


            auto type_string = get_type_string(param_qual_type_pair.first,
                                               get_source_for_source_range(source_manager, param_qual_type_pair.second));
            result << type_string;

            if (insert_variable_names) {
                result << " " << var_names[count++];
            }

            update_wrapped_class_for_type(source_manager, wrapped_class, param_qual_type_pair.first);

        }
        return result.str();
    }

    std::string get_return_type(SourceManager & source_manager,
                                      WrappedClass & wrapped_class,
                                      const CXXMethodDecl * method) {
        auto qual_type = method->getReturnType();
        auto result = get_type_string(qual_type, get_source_for_source_range(source_manager,
                                                                             method->getReturnTypeSourceRange()));
//        auto return_type_decl = qual_type->getAsCXXRecordDecl();
//        auto full_source_loc = FullSourceLoc(return_type_decl->getLocStart(), source_manager);
//        auto header_file = strip_path_from_filename(source_manager.getFilename(full_source_loc).str());
//        cerr << fmt::format("{} needs {}", wrapped_class.class_name, header_file) << endl;
//        wrapped_class.include_files.insert(header_file);
//

        update_wrapped_class_for_type(source_manager, wrapped_class, qual_type);

        return result;

    }


    std::string get_method_return_type_class_and_parameters(SourceManager & source_manager,
                                                            WrappedClass & wrapped_class,
                                                            const CXXRecordDecl * klass, const CXXMethodDecl * method) {
        std::stringstream results;
        results << get_return_type(source_manager, wrapped_class, method);
        results << ", " << klass->getName().str();
        results << get_method_parameters(source_manager, wrapped_class, method, true);
        return results.str();
    }

    std::string get_method_return_type_and_parameters(SourceManager & source_manager,
                                                      WrappedClass & wrapped_class,
                                                      const CXXRecordDecl * klass, const CXXMethodDecl * method) {
        std::stringstream results;
        results << get_return_type(source_manager, wrapped_class, method);
        results << get_method_parameters(source_manager, wrapped_class, method, true);
        return results.str();
    }




    std::string get_method_string(SourceManager & source_manager,
                                  WrappedClass & wrapped_class,
                                  const CXXMethodDecl * method) {
        std::stringstream result;
        result << method->getReturnType().getAsString();

        result << method->getName().str();

        result << "(";

        result << get_method_parameters(source_manager, wrapped_class, method);

        result << ")";

        return result.str();
    }






    // calls callback for each constructor in the class.  If annotation specified, only
    //   constructors with that annotation will be sent to the callback
    template<class Callback>
    void foreach_constructor(const CXXRecordDecl * klass, Callback && callback,
                             const std::string & annotation = "") {
        for(CXXMethodDecl * method : klass->methods()) {
            CXXConstructorDecl * constructor = dyn_cast<CXXConstructorDecl>(method);
            if (constructor == nullptr) {
                continue;
            }
            if (constructor->getAccess() != AS_public) {
//                    cerr << "Skipping non-public constructor" << endl;
                continue;
            }
            if (get_export_type(constructor) == EXPORT_NONE) {
                continue;
            }

            if (annotation != "" && !has_annotation(constructor, annotation)) {
//                cerr << "Annotation " << annotation << " requested, but constructor doesn't have it" << endl;
                continue;
            }
            callback(constructor);
        }
    }

    CXXConstructorDecl * get_bidirectional_constructor(const CXXRecordDecl * klass) {
        CXXConstructorDecl * result = nullptr;
        bool got_constructor = false;
        foreach_constructor(klass, [&](auto constructor){
            if (got_constructor) {
                cerr << "ERROR, MORE THAN ONE BIDIRECTIONAL CONSTRUCTOR" << endl;
            }
            got_constructor = true;
            result = constructor;

        }, V8TOOLKIT_BIDIRECTIONAL_CONSTRUCTOR_STRING);
        if (!got_constructor) {
            cerr << "ERROR, NO BIDIRECTIONAL CONSTRUCTOR FOUND IN " << klass->getNameAsString() << endl;
        }
        return result;
    }

    string get_bidirectional_constructor_parameter_typelists(const CXXRecordDecl * klass, bool leading_comma) {
        auto constructor = get_bidirectional_constructor(klass);

	// all internal params must come before all external params
	bool found_external_param = false;
        auto parameter_count = constructor->getNumParams();
	vector<string> internal_params;
	vector<string> external_params;
        for (unsigned int i = 0; i < parameter_count; i++) {
            auto param_decl = constructor->getParamDecl(i);
	    if (has_annotation(param_decl, V8TOOLKIT_BIDIRECTIONAL_INTERNAL_PARAMETER_STRING)) {
		if (found_external_param) {
		    cerr << "ERROR: Found internal parameter after external parameter found in " << klass->getNameAsString() << endl;
		    throw std::exception();
		}
		internal_params.push_back(param_decl->getType().getAsString());
	    } else {
		found_external_param = true;
		external_params.push_back(param_decl->getType().getAsString());
	    }
	}

        stringstream result;
        if (leading_comma) {
            result << ", ";
        }
        result << "v8toolkit::TypeList<" << join(internal_params, ", ") << ">";
	result << ", ";
	result << "v8toolkit::TypeList<" << join(external_params, ", ") << ">";

        return result.str();
    }




    class BidirectionalBindings {
    private:
        SourceManager & source_manager;
        const CXXRecordDecl * starting_class;
        WrappedClass & wrapped_class;

    public:
        BidirectionalBindings(SourceManager & source_manager,
                              const CXXRecordDecl * starting_class,
                              WrappedClass & wrapped_class) :
                source_manager(source_manager),
                starting_class(starting_class),
                wrapped_class(wrapped_class) {}

        std::string short_name(){return starting_class->getName();}


        std::vector<const CXXMethodDecl *> get_all_virtual_methods_for_class(const CXXRecordDecl * klass) {
            std::vector<const CXXMethodDecl *> results;
            std::deque<const CXXRecordDecl *> stack{klass};

            while (!stack.empty()) {
                auto current_class = stack.front();
                stack.pop_front();

                for(CXXMethodDecl * method : current_class->methods()) {
                    if (dyn_cast<CXXDestructorDecl>(method)) {
                        //cerr << "Skipping virtual destructor while gathering virtual methods" << endl;
                        continue;
                    }
                    if (dyn_cast<CXXConversionDecl>(method)) {
                        //cerr << "Skipping user-defined conversion operator" << endl;
                        continue;
                    }
                    if (method->isVirtual() && !method->isPure()) {
                        // go through existing ones and check for match
                        if (std::find_if(results.begin(), results.end(), [&](auto found){
                            if(get_method_string(source_manager, wrapped_class, method) ==
                                    get_method_string(source_manager, wrapped_class, found)) {
//                                printf("Found dupe: %s\n", get_method_string(method).c_str());
                                return true;
                            }
                            return false;
                        }) == results.end()) {
                            results.push_back(method);
                        }
                    }
                }

                for (auto base_class : current_class->bases()) {
                    auto base_decl = base_class.getType()->getAsCXXRecordDecl();
                    stack.push_back(base_decl);
                }
            }
            return results;
        }

        std::string handle_virtual(const CXXMethodDecl * method) {

            // skip pure virtual functions
            if (method->isPure()) {
                return "";
            }

            auto num_params = method->getNumParams();
//            printf("Dealing with %s\n", method->getQualifiedNameAsString().c_str());
            std::stringstream result;


            result << "  JS_ACCESS_" << num_params << (method->isConst() ? "_CONST(" : "(");

            auto return_type_string = method->getReturnType().getAsString();
            result << return_type_string << ", ";

            auto method_name = method->getName();

            result << method_name.str();

            if (num_params > 0) {
                auto types = get_method_param_qual_types(method);
                vector<string>type_names;
                for (auto & type_pair : types) {
                    type_names.push_back(std::regex_replace(type_pair.first.getAsString(), std::regex("\\s*,\\s*"), " V8TOOLKIT_COMMA "));
                }

                result << join(type_names, ", ", true);
            }

            result  << ");\n";

            return result.str();

        }

        std::string handle_class(const CXXRecordDecl * klass) {
            std::stringstream result;
            auto virtuals = get_all_virtual_methods_for_class(klass);
            for (auto method : virtuals) {
                result << handle_virtual(method);
            }
            return result.str();

        }

        void generate_bindings() {
            std::stringstream result;
            auto annotations = get_annotations(starting_class);
            auto matches = get_annotation_regex(starting_class, "v8toolkit_generate_(.*)");
            if (has_annotation(starting_class, std::string(V8TOOLKIT_BIDIRECTIONAL_CLASS_STRING))) {
                result << fmt::format("class JS{} : public {}, public v8toolkit::JSWrapper<{}> {{\npublic:\n", // {{ is escaped {
                                      short_name(), short_name(), short_name());
                result << fmt::format("    JS{}(v8::Local<v8::Context> context, v8::Local<v8::Object> object,\n", short_name());
                result << fmt::format("        v8::Local<v8::FunctionTemplate> created_by");
                bool got_constructor = false;
                int constructor_parameter_count;
                foreach_constructor(starting_class, [&](auto constructor_decl){
                    if (got_constructor) { cerr << "ERROR: Got more than one constructor" << endl; return;}
                    got_constructor = true;
                    result << get_method_parameters(source_manager, wrapped_class, constructor_decl, true, true);
                    constructor_parameter_count = constructor_decl->getNumParams();

                }, V8TOOLKIT_BIDIRECTIONAL_CONSTRUCTOR_STRING);
                if (!got_constructor) {
                    cerr << "ERROR: Got no constructor for " << starting_class->getNameAsString() << endl;
                }
                result << fmt::format(") :\n");

                auto variable_names = generate_variable_names(constructor_parameter_count);

                result << fmt::format("      {}({}),\n", short_name(), join(variable_names));
                result << fmt::format("      v8toolkit::JSWrapper<{}>(context, object, created_by) {{}}\n", short_name()); // {{}} is escaped {}
                result << handle_class(starting_class);
                result << "};\n";
            } else {
//                printf("Class %s not marked bidirectional\n", short_name().c_str());
                return;
            }

            // dumps a file per class
//            cerr << "Dumping JSWrapper type for " << short_name() << endl;
            ofstream bidirectional_class_file;
            auto bidirectional_class_filename = fmt::format("v8toolkit_generated_bidirectional_{}.h", short_name());
            bidirectional_class_file.open(bidirectional_class_filename, ios::out);
            assert(bidirectional_class_file);

	    bidirectional_class_file << "#pragma once\n";
	    bidirectional_class_file << "#include " << get_include_for_record_decl(source_manager, starting_class) << "\n";
            bidirectional_class_file << result.str();
            bidirectional_class_file.close();


        }
    };








    class ClassHandler : public MatchFinder::MatchCallback {
    private:


//        CompilerInstance &CI;

        SourceManager & source_manager;
        std::vector<WrappedClass> & wrapped_classes;
        WrappedClass * current_wrapped_class; // the class currently being wrapped
        std::set<std::string> names_used;
        const CXXRecordDecl * top_level_class_decl = nullptr;

    public:




        ClassHandler(CompilerInstance &CI,
                     std::vector<WrappedClass> & wrapped_classes) :
            source_manager(CI.getSourceManager()),
            wrapped_classes(wrapped_classes),
            current_wrapped_class(&wrapped_classes[0])
        {}



        std::string handle_data_member(const CXXRecordDecl * containing_class, FieldDecl * field, EXPORT_TYPE parent_export_type, const std::string & indentation) {
            std::stringstream result;
            auto export_type = get_export_type(field, parent_export_type);
            auto short_field_name = field->getNameAsString();
            auto full_field_name = field->getQualifiedNameAsString();

//            if (containing_class != top_level_class_decl) {
//                cerr << "************";
//            }
//            cerr << "changing data member from " << full_field_name << " to ";
//
//            std::string regex_string = fmt::format("{}::{}$", containing_class->getName().str(), short_field_name);
//            auto regex = std::regex(regex_string);
//            auto replacement = fmt::format("{}::{}", top_level_class_decl->getName().str(), short_field_name);
//            full_field_name = std::regex_replace(full_field_name, regex, replacement);
//            cerr << full_field_name << endl;


            if (export_type != EXPORT_ALL && export_type != EXPORT_EXCEPT) {
                if (PRINT_SKIPPED_EXPORT_REASONS) printf("%sSkipping data member %s because not supposed to be exported %d\n",
                       indentation.c_str(),
                       short_field_name.c_str(), export_type);
                return "";
            }

            if (field->getAccess() != AS_public) {
                if (PRINT_SKIPPED_EXPORT_REASONS) printf("%s**%s is not public, skipping\n", indentation.c_str(), short_field_name.c_str());
                return "";
            }

            if (current_wrapped_class->names.count(short_field_name)) {
                printf("WARNING: Skipping duplicate name %s/%s :: %s\n",
                       top_level_class_decl->getName().str().c_str(),
                        containing_class->getName().str().c_str(),
                        short_field_name.c_str());
                return "";
            }
            current_wrapped_class->names.insert(short_field_name);


            current_wrapped_class->declaration_count++;

            update_wrapped_class_for_type(source_manager, *current_wrapped_class, field->getType());

            if (has_annotation(field, V8TOOLKIT_READONLY_STRING)) {
                result << fmt::format("{}class_wrapper.add_member_readonly(\"{}\", &{});\n", indentation,
                                      short_field_name, full_field_name);
            } else {
                result << fmt::format("{}class_wrapper.add_member(\"{}\", &{});\n", indentation,
                                      short_field_name, full_field_name);
            }
//            printf("%sData member %s, type: %s\n",
//                   indentation.c_str(),
//                   field->getNameAsString().c_str(),
//                   field->getType().getAsString().c_str());
            return result.str();
        }


        std::string handle_method(const CXXRecordDecl * containing_class, CXXMethodDecl * method, EXPORT_TYPE parent_export_type, const std::string & indentation) {

            std::stringstream result;

            std::string full_method_name(method->getQualifiedNameAsString());
            std::string short_method_name(method->getNameAsString());

//            cerr << "changing method name from " << full_method_name << " to ";
//
//            auto regex = std::regex(fmt::format("{}::{}$", containing_class->getName().str(), short_method_name));
//            auto replacement = fmt::format("{}::{}", top_level_class_decl->getName().str(), short_method_name);
//            full_method_name = std::regex_replace(full_method_name, regex, replacement);
//            cerr << full_method_name << endl;




            auto export_type = get_export_type(method, parent_export_type);

            if (export_type != EXPORT_ALL && export_type != EXPORT_EXCEPT) {
                if (PRINT_SKIPPED_EXPORT_REASONS) printf("%sSkipping method %s because not supposed to be exported %d\n",
                       indentation.c_str(), full_method_name.c_str(), export_type);
                return "";
            }

            // only deal with public methods
            if (method->getAccess() != AS_public) {
                if (PRINT_SKIPPED_EXPORT_REASONS) printf("%s**%s is not public, skipping\n", indentation.c_str(), full_method_name.c_str());
                return "";
            }
            if (method->isOverloadedOperator()) {
                if (PRINT_SKIPPED_EXPORT_REASONS) printf("%s**skipping overloaded operator %s\n", indentation.c_str(), full_method_name.c_str());
                return "";
            }
            if (dyn_cast<CXXConstructorDecl>(method)) {
                if (PRINT_SKIPPED_EXPORT_REASONS) printf("%s**skipping constructor %s\n", indentation.c_str(), full_method_name.c_str());
                return "";
            }
            if (dyn_cast<CXXDestructorDecl>(method)) {
                if (PRINT_SKIPPED_EXPORT_REASONS) printf("%s**skipping destructor %s\n", indentation.c_str(), full_method_name.c_str());
                return "";
            }
            if (method->isPure()) {
                assert(method->isVirtual());
                if (PRINT_SKIPPED_EXPORT_REASONS) printf("%s**skipping pure virtual %s\n", indentation.c_str(), full_method_name.c_str());
                return "";
            }
            if (dyn_cast<CXXConversionDecl>(method)) {
                if (PRINT_SKIPPED_EXPORT_REASONS) cerr << fmt::format("{}**skipping user-defined conversion operator", indentation) << endl;
                return "";
            }

            if (current_wrapped_class->names.count(short_method_name)) {
                printf("Skipping duplicate name %s/%s :: %s\n",
                       top_level_class_decl->getName().str().c_str(),
                       containing_class->getName().str().c_str(),
                       short_method_name.c_str());
                return "";
            }
            current_wrapped_class->names.insert(short_method_name);



            result << indentation;



            if (method->isStatic()) {
                current_wrapped_class->declaration_count++;
                result << fmt::format("class_wrapper.add_static_method<{}>(\"{}\", &{});\n",
                       get_method_return_type_and_parameters(source_manager, *current_wrapped_class, containing_class, method),
                       short_method_name, full_method_name);
            } else {
                current_wrapped_class->declaration_count++;
                result << fmt::format("class_wrapper.add_method<{}>(\"{}\", &{});\n",
                       get_method_return_type_class_and_parameters(source_manager, *current_wrapped_class, containing_class, method),
                       short_method_name, full_method_name);
                methods_wrapped++;

            }
            return result.str();
        }


        void handle_class(const CXXRecordDecl * klass,
                          EXPORT_TYPE parent_export_type = EXPORT_UNSPECIFIED,
                          bool top_level = true,
                          const std::string & indentation = "") {



            if (top_level) {

                classes_wrapped++;
                names_used.clear();

                wrapped_classes.emplace_back(WrappedClass(klass->getName().str()));
                current_wrapped_class = &wrapped_classes.back();


                cerr << "*&&&&&&&&&&&&&&&adding include for class being handled: " << klass->getName().str() << " : " << get_include_for_record_decl(source_manager, klass) << endl;
                current_wrapped_class->include_files.insert(get_include_for_record_decl(source_manager, klass));

                // if this is a bidirectional class, make a minimal wrapper for it
                if (has_annotation(klass, V8TOOLKIT_BIDIRECTIONAL_CLASS_STRING)) {
                    cerr << "Type " << current_wrapped_class->class_name << " **IS** bidirectional" << endl;

                    auto bidirectional_class_name = fmt::format("JS{}", current_wrapped_class->class_name);
                    WrappedClass bidirectional(bidirectional_class_name);
                    bidirectional.parent_types.insert(current_wrapped_class->class_name);
                    bidirectional.contents <<
                    fmt::format("  {{\n") <<
                    fmt::format("    // {}\n", bidirectional.class_name) <<
                    fmt::format("    v8toolkit::V8ClassWrapper<{}> & class_wrapper = isolate.wrap_class<{}>();\n",
                                bidirectional.class_name, bidirectional.class_name) <<
                    fmt::format("    class_wrapper.set_parent_type<{}>();\n", current_wrapped_class->class_name) <<
                    fmt::format("    class_wrapper.finalize();\n") <<
                    fmt::format("  }}\n\n");

                    // not sure if this is needed
                    //bidirectional.include_files.insert(get_include_for_record_decl(klass));
                    bidirectional.include_files.insert(fmt::format("\"v8toolkit_generated_bidirectional_{}.h\"", current_wrapped_class->class_name));


                    wrapped_classes.emplace_back(move(bidirectional));



                    current_wrapped_class->compatible_types.insert(bidirectional_class_name);
                } else {
                    cerr << "Type " << current_wrapped_class->class_name << " is not bidirectional" << endl;
                }
            }



            std::stringstream & result = current_wrapped_class->contents;

            bool is_bidirectional = false;
            if (top_level) {
                if (has_annotation(klass, V8TOOLKIT_BIDIRECTIONAL_CLASS_STRING)) {
//                    fprintf(stderr,"Is bidirectional\n");
                    is_bidirectional = true;
                } else {
//                    fprintf(stderr,"Is *not* bidirectional\n");
                }
            }
            auto class_name = klass->getQualifiedNameAsString();
            auto export_type = get_export_type(klass, parent_export_type);
            if (export_type == EXPORT_NONE) {
                if (PRINT_SKIPPED_EXPORT_REASONS) fprintf(stderr,"%sSkipping class %s marked EXPORT_NONE\n", indentation.c_str(), class_name.c_str());
                return;
            }

            // prints out source for decl
            //fprintf(stderr,"class at %s", decl2str(klass,  source_manager).c_str());

            auto full_source_loc = FullSourceLoc(klass->getLocation(), source_manager);
            auto file_id = full_source_loc.getFileID();

//            fprintf(stderr,"%sClass/struct: %s\n", indentation.c_str(), class_name.c_str());
            // don't do the following code for inherited classes
            if (top_level){

                result << indentation << "{\n";

                result << fmt::format("{}  // {}", indentation, class_name) << "\n";

                current_wrapped_class->include_files.insert(get_include_string_for_fileid(source_manager, file_id));
                result << fmt::format("{}  v8toolkit::V8ClassWrapper<{}> & class_wrapper = isolate.wrap_class<{}>();\n",
                                      indentation, class_name, class_name);
                result << fmt::format("{}  class_wrapper.set_class_name(\"{}\");\n", indentation, class_name);
            }
//            fprintf(stderr,"%s Decl at line %d, file id: %d %s\n", indentation.c_str(), full_source_loc.getExpansionLineNumber(),
//                   full_source_loc.getFileID().getHashValue(), source_manager.getBufferName(full_source_loc));

//                auto type_decl = dyn_cast<TypeDecl>(klass);
//                assert(type_decl);
//                auto type = type_decl->getTypeForDecl();

//
            for(CXXMethodDecl * method : klass->methods()) {
                result << handle_method(klass, method, export_type, indentation + "  ");
            }

            for (FieldDecl * field : klass->fields()) {
                result << handle_data_member(klass, field, export_type, indentation + "  ");
            }

            for (auto base_class : klass->bases()) {
                auto qual_type = base_class.getType();
                auto record_decl = qual_type->getAsCXXRecordDecl();
                handle_class(record_decl, export_type, false, indentation + "  ");
            }
            if (is_bidirectional) {

                result << fmt::format("{}  v8toolkit::JSFactory<{}, JS{}{}>::add_subclass_static_method(class_wrapper);\n",
                                         indentation,
                                         class_name, class_name, get_bidirectional_constructor_parameter_typelists(klass, true));
            }
            if (top_level) {
                if (!current_wrapped_class->compatible_types.empty()) {
                    result << fmt::format("{}  class_wrapper.set_compatible_types<{}>();\n", indentation,
                                          join(current_wrapped_class->compatible_types));
                }
                if (!current_wrapped_class->parent_types.empty()) {
                    result << fmt::format("{}  class_wrapper.set_parent_type<{}>();\n", indentation,
                                          join(current_wrapped_class->parent_types));
                }
                result << fmt::format("{}  class_wrapper.finalize();\n", indentation);
            }

            std::vector<std::string> used_constructor_names;

            if (top_level) {
                if (klass->isAbstract()) {
//                    cerr << "Skipping all constructors because class is abstract: " << class_name << endl;
                } else {
                    foreach_constructor(klass, [&](auto constructor) {

//                        auto full_source_loc = FullSourceLoc(constructor->getLocation(), source_manager);
//                        fprintf(stderr,"%s %s constructor Decl at line %d, file id: %d %s\n", indentation.c_str(),
//                                top_level_class_decl->getName().str().c_str(),
//                                full_source_loc.getExpansionLineNumber(),
//                                full_source_loc.getFileID().getHashValue(),
//                                source_manager.getBufferName(full_source_loc));


                        if (constructor->isCopyConstructor()) {
//                            fprintf(stderr,"Skipping copy constructor\n");
                            return;
                        } else if (constructor->isMoveConstructor()) {
//                            fprintf(stderr,"Skipping move constructor\n");
                            return;
                        } else if (constructor->isDeleted()) {
//                            cerr << "Skipping deleted constructor" << endl;
                            return;
                        }
                        auto annotations = get_annotation_regex(constructor, V8TOOLKIT_CONSTRUCTOR_PREFIX "(.*)");
//                        fprintf(stderr,"Got %d annotations on constructor\n", (int)annotations.size());
                        std::string constructor_name = class_name;
                        if (!annotations.empty()) {
                            constructor_name = annotations[0];
                        }
                        if (std::find(used_constructor_names.begin(), used_constructor_names.end(), constructor_name) !=
                            used_constructor_names.end()) {
                            cerr << fmt::format("Error: because duplicate JS constructor function name {}",
                                                constructor_name.c_str()) << endl;
                            for (auto &name : used_constructor_names) {
                                cerr << name << endl;
                            }
                            throw std::exception();
                        }
                        used_constructor_names.push_back(constructor_name);

                        result << fmt::format("{}  class_wrapper.add_constructor<{}>(\"{}\", isolate);\n",
                                              indentation, get_method_parameters(source_manager,
                                                                                 *current_wrapped_class,
                                                                                 constructor), constructor_name);
                    });
                }
                result << indentation << "}\n\n";
            }
        }


        /**
         * This runs per-match from MyASTConsumer, but always on the same ClassHandler object
         */
        virtual void run(const MatchFinder::MatchResult &Result) {

            if (const CXXRecordDecl * klass = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("class")) {
                this->top_level_class_decl = klass;
                auto full_class_name = klass->getQualifiedNameAsString();

                handle_class(klass, EXPORT_UNSPECIFIED, true, "  ");


                BidirectionalBindings bidirectional(source_manager, klass, *current_wrapped_class);
                bidirectional.generate_bindings();

            }
        }
    };













    // Implementation of the ASTConsumer interface for reading an AST produced
    // by the Clang parser. It registers a couple of matchers and runs them on
    // the AST.
    class MyASTConsumer : public ASTConsumer {
    public:
        MyASTConsumer(CompilerInstance &CI,
                      std::vector<WrappedClass> & wrapped_classes) :
                HandlerForClass(CI, wrapped_classes) {
            Matcher.addMatcher(cxxRecordDecl(anyOf(isStruct(), isClass()), // select all structs and classes
                                             hasAttr(attr::Annotate), // can't check the actual annotation value here
                                             isDefinition() // skip forward declarations
            ).bind("class"),
                &HandlerForClass);
        }

        void HandleTranslationUnit(ASTContext &Context) override {
            // Run the matchers when we have the whole TU parsed.
            Matcher.matchAST(Context);
        }

    private:
        ClassHandler HandlerForClass;
        MatchFinder Matcher;
    };













    // This is the class that is registered with LLVM.  PluginASTAction is-a ASTFrontEndAction
    class PrintFunctionNamesAction : public PluginASTAction {
    public:
        // open up output files
        PrintFunctionNamesAction() {

        }

        // This is called when all parsing is done
        void EndSourceFileAction() {
            static bool already_called = false;

            if (already_called) {
                cerr << "This plugin doesn't work if there's more than one file.   Use it on a unity build" << endl;
                throw std::exception();
            }
            already_called = true;

            // Write class wrapper data to a file
            int file_count = 1;

            int declaration_count_this_file = 0;
            vector<WrappedClass*> classes_for_this_file;

            for (auto wrapped_class_iterator = wrapped_classes.begin(); wrapped_class_iterator != wrapped_classes.end(); wrapped_class_iterator++) {
                cerr << "dumping wrapped class " << wrapped_class_iterator->class_name << endl;
                // if there's room in the current file, add this class
                auto space_available = declaration_count_this_file == 0 || declaration_count_this_file + wrapped_class_iterator->declaration_count < MAX_DECLARATIONS_PER_FILE;
                auto last_class = wrapped_class_iterator + 1 == wrapped_classes.end();

                if (!space_available) {
                    write_classes(file_count, classes_for_this_file, last_class);

                    // reset for next file
                    classes_for_this_file.clear();
                    declaration_count_this_file = 0;
                    file_count++;
                }

                classes_for_this_file.push_back(&*wrapped_class_iterator);
                declaration_count_this_file += wrapped_class_iterator->declaration_count;


                if (last_class) {
                    write_classes(file_count++, classes_for_this_file, true);
                }
            }

            cerr << "Wrapped " << classes_wrapped << " classes with " << methods_wrapped << " methods" << endl;

        }

        // takes a file number starting at 1 and incrementing 1 each time
        // a list of WrappedClasses to print
        // and whether or not this is the last file to be written
        void write_classes(int file_count, vector<WrappedClass*> & classes, bool last_one) {
            // Open file
            string class_wrapper_filename = fmt::format("v8toolkit_generated_class_wrapper_{}.cpp", file_count);
            ofstream class_wrapper_file;

            class_wrapper_file.open(class_wrapper_filename, ios::out);
            if (!class_wrapper_file) {
                cerr << "Couldn't open " << class_wrapper_filename << endl;
                throw std::exception();
            }

            // Write includes
            class_wrapper_file << "#include <v8toolkit/bidirectional.h>\n";


            set<string> already_included_this_file;

            for (WrappedClass * wrapped_class : classes) {
                for(auto & include_file : wrapped_class->include_files) {
                    if (include_file != "" && already_included_this_file.count(include_file) == 0) {
                        class_wrapper_file << fmt::format("#include {}\n", include_file);
                        already_included_this_file.insert(include_file);
                    }
                }
            }

            // Write function header
            class_wrapper_file << fmt::format("void v8toolkit_initialize_class_wrappers_{}(v8toolkit::Isolate &); // may not exist that's fine\n", file_count+1);
            if (file_count == 1) {
                class_wrapper_file << fmt::format("void v8toolkit_initialize_class_wrappers(v8toolkit::Isolate & isolate) {{\n");

            } else {
                class_wrapper_file << fmt::format("void v8toolkit_initialize_class_wrappers_{}(v8toolkit::Isolate & isolate) {{\n",
                                                  file_count);
            }

            // Print function body
            for (auto wrapped_class : classes) {
                class_wrapper_file << wrapped_class->contents.str();
            }


            // if there's going to be another file, call the function in it
            if (!last_one) {
                class_wrapper_file << fmt::format("  v8toolkit_initialize_class_wrappers_{}(isolate);\n", file_count + 1);
            }

            // Close function and file
            class_wrapper_file << "}\n";
            class_wrapper_file.close();

        }








    protected:
        std::vector<WrappedClass> wrapped_classes;


        // The value returned here is used internally to run checks against
        std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                       llvm::StringRef) {

            return llvm::make_unique<MyASTConsumer>(CI, wrapped_classes);
        }

        bool ParseArgs(const CompilerInstance &CI,
                       const std::vector<std::string> &args) {
            for (unsigned i = 0, e = args.size(); i != e; ++i) {
                llvm::errs() << "PrintFunctionNames arg = " << args[i] << "\n";

                // Example error handling.
                if (args[i] == "-an-error") {
                    DiagnosticsEngine &D = CI.getDiagnostics();
                    unsigned DiagID = D.getCustomDiagID(DiagnosticsEngine::Error,
                                                        "invalid argument '%0'");
                    D.Report(DiagID) << args[i];
                    return false;
                }
            }
            if (args.size() && args[0] == "help")
                PrintHelp(llvm::errs());

            return true;
        }
        void PrintHelp(llvm::raw_ostream &ros) {
            ros << "Help for PrintFunctionNames plugin goes here\n";
        }
    };
}

static FrontendPluginRegistry::Add<PrintFunctionNamesAction>
        X("v8toolkit-generate-bindings", "generate v8toolkit bindings");
