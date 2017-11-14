
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#include "clang/Frontend/CompilerInstance.h"
#pragma clang diagnostic pop

#include "wrapped_class.h"
#include "class_handler.h"
#include "helper_functions.h"

#include <xl/library_extensions.h>

namespace v8toolkit::class_parser {


ClassHandler::~ClassHandler() {
    log.info(LogSubjects::Subjects::ClassParser, "ClassHandler destructor");
}

void ClassHandler::run(const ast_matchers::MatchFinder::MatchResult & Result) {



    matched_classes_returned++;

    if (matched_classes_returned % 10000 == 0) {
        std::cerr << fmt::format("\n### Matcher results processed: {}", matched_classes_returned) << std::endl;
    }

    // if the current result is matched from the "not std:: class"-bound matcher
    if (const CXXRecordDecl * klass = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("not std:: class")) {
        auto class_name = get_canonical_name_for_decl(klass);

//        std::cerr << fmt::format("Looking at: {} - anything not filtered", class_name) << std::endl;


        if (klass->isDependentType()) {
//            cerr << "Skipping 'class with annotation' dependent type: " << class_name << endl;
            return;
        }

        auto name = get_canonical_name_for_decl(klass);
        if (std::regex_match(name, regex("^(class\\s+|struct\\s+)?std::.*$"))) {
            return;
        }
        if (std::regex_match(name, regex("^(class\\s+|struct\\s+)?__.*$"))) {
            return;
        }

//        cerr << endl << "Got class definition: " << class_name << endl;
//        fprintf(stderr, "decl ptr: %p\n", (void *) klass);


        if (!is_good_record_decl(klass)) {
            cerr << "SKIPPING BAD RECORD DECL" << endl;
        }

//        cerr << "Storing it for later processing (unless dupe)" << endl;

        WrappedClass::get_or_insert_wrapped_class(klass, this->ci, FOUND_UNSPECIFIED);
    }

    // Store any annotations on templates so the annotations can be merged later with types instantiated from the template
    if (const CXXRecordDecl * klass = Result.Nodes.getNodeAs<clang::CXXRecordDecl>(
        "forward declaration with annotation")) {

        auto class_name = get_canonical_name_for_decl(klass);
//        std::cerr << fmt::format("Looking at: {} - forward declaration with annotation", class_name) << std::endl;


        /* check to see if this has any annotations we should associate with its associated template */
        auto described_tmpl = klass->getDescribedClassTemplate();
        if (klass->isDependentType() && described_tmpl) {
//            fprintf(stderr, "described template %p, %s\n", (void *) described_tmpl, described_tmpl->getQualifiedNameAsString().c_str());
//            printf("Merging %d annotations with template %p\n", (int) Annotations(klass).get().size(), (void *) described_tmpl);
            Annotations::annotations_for_class_templates[described_tmpl].merge(Annotations(klass));
        }
    }


    if (const CXXRecordDecl * klass = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("class derived from WrappedClassBase")) {

//        std::cerr << fmt::format("Looking at: {} - class derived from WrappedClassBase", get_canonical_name_for_decl(klass)) << std::endl;

        if (!is_good_record_decl(klass)) {
//            cerr << "skipping 'bad' record decl" << endl;
            return;
        }
        if (klass->isDependentType()) {
//            cerr << "skipping dependent type" << endl;
            return;
        }

        auto name = get_canonical_name_for_decl(klass);
        if (std::regex_match(name, regex("^(class\\s+|struct\\s+)?std::.*$"))) {
            return;
        }
        if (std::regex_match(name, regex("^(class\\s+|struct\\s+)?__.*$"))) {
            return;
        }


        if (Annotations(klass).has(V8TOOLKIT_NONE_STRING)) {
//            cerr << "Skipping class because it's explicitly marked SKIP" << endl;
            return;
        }


//        print_specialization_info(klass);

//        cerr << "Storing it for later processing (unless dupe)" << endl;
        WrappedClass::get_or_insert_wrapped_class(klass, this->ci, FOUND_INHERITANCE);
    }

    // Store annotations associated with a "using" statement to be merged with the "real" type
    // only pick off the typedefNameDecl entries, but in 3.8, typedefNameDecl() matcher isn't available
    if (auto typedef_decl = Result.Nodes.getNodeAs<clang::TypedefNameDecl>("named decl")) {
        auto qual_type = typedef_decl->getUnderlyingType();
        auto record_decl = qual_type->getAsCXXRecordDecl();

        // not interesting - it's for something like a primitive type like 'long'
        if (!record_decl) {
            return;
        }
        auto name = get_canonical_name_for_decl(record_decl);
        if (std::regex_match(name, regex("^(class\\s+|struct\\s+)?std::.*$"))) {
            return;
        }
        if (std::regex_match(name, regex("^(class\\s+|struct\\s+)?__.*$"))) {
            return;
        }

        Annotations::annotations_for_record_decls[record_decl].merge(Annotations(typedef_decl));

        if (Annotations(typedef_decl).has(V8TOOLKIT_NAME_ALIAS_STRING)) {
            string name_alias = typedef_decl->getNameAsString();
//            std::cerr << fmt::format("Annotated type name: {} => {}", record_decl->getQualifiedNameAsString(), typedef_decl->getNameAsString()) << std::endl;
            Annotations::names_for_record_decls[record_decl] = name_alias;

            // if the class has already been parsed, update it now
            if (auto wrapped_class = WrappedClass::get_if_exists(record_decl)) {
//                std::cerr << fmt::format("Setting name alias for {} to {}", wrapped_class->get_name_alias(), name_alias) << std::endl;
                wrapped_class->set_name_alias(name_alias);
            }
        }
    }

#ifdef TEMPLATE_INFO_ONLY

    if (const ClassTemplateSpecializationDecl * klass = Result.Nodes.getNodeAs<clang::ClassTemplateSpecializationDecl>("class")) {
            auto class_name = get_canonical_name_for_decl(klass);

            bool print_logging = false;

            if (std::regex_search(class_name, std::regex("^(class|struct)\\s+v8toolkit"))) {
            //		if (std::regex_search(class_name, std::regex("remove_reference"))) {
                print_logging = true;
                cerr << fmt::format("Got class {}", class_name) << endl;
            }


#ifdef TEMPLATE_FILTER_STD
            if (std::regex_search(class_name, std::regex("^std::"))) {
                if (print_logging) cerr << "Filtering out because in std::" << endl;
                return;
            }
#endif



            auto tmpl = klass->getSpecializedTemplate();
            if (print_logging) {
                cerr << "got specialized template " << tmpl->getQualifiedNameAsString() << endl;
            }



#ifdef TEMPLATE_FILTER_STD
            if (std::regex_search(tmpl->getQualifiedNameAsString(), std::regex("^std::"))) {
                return;
            }
#endif


            ClassTemplate::get_or_create(tmpl).instantiated();


        }

        if (const CXXMethodDecl * method = Result.Nodes.getNodeAs<clang::CXXMethodDecl>("method")) {
            auto method_name = method->getQualifiedNameAsString();
            const FunctionDecl * pattern = nullptr;

            if (!method->isTemplateInstantiation()) {
                return;
            }
#ifdef TEMPLATE_FILTER_STD
            if (std::regex_search(method_name, std::regex("^std::"))) {
                return;
            }
#endif

            pattern = method->getTemplateInstantiationPattern();
            if (!pattern) {
                pattern = method;
            }

            if (!pattern) {
                llvm::report_fatal_error("method is template insantiation but pattern still nullptr");
            }

            FunctionTemplate::get_or_create(pattern).instantiated();


#if 0
            bool print_logging = false;

            if (std::regex_search(method_name, std::regex("function_in_temp"))) {
                cerr << endl << "*******Found function in templated class decl" << endl;
                fprintf(stderr, "Method decl ptr: %p\n", (void*) method);
                cerr << "is dependent context: " << method->isDependentContext() << endl;
                cerr << "has dependent template info: " << (method->getDependentSpecializationInfo() != nullptr) << endl;
                cerr << "is template instantiation: " << (method->isTemplateInstantiation()) << endl;
                cerr << "has instantiation pattern: " << (method->getTemplateInstantiationPattern() != nullptr) << endl;
                if (method->getTemplateInstantiationPattern()) {
                fprintf(stderr, "template instantiation pattern ptr: %p\n", (void*) method->getTemplateInstantiationPattern());
                }
                print_logging = true;
            }

            const FunctionTemplateDecl * function_template_decl = method->getDescribedFunctionTemplate();

            if (function_template_decl == nullptr && method->getTemplateSpecializationInfo()) {
                function_template_decl = method->getTemplateSpecializationInfo()->getTemplate();
            }

            if (function_template_decl) {
                cerr << fmt::format("'real' templated method {} has instantiation pattern: {}", method_name, method->getTemplateInstantiationPattern() != nullptr) << endl;
                fprintf(stderr, "method: %p, instantiation pattern: %p\n", (void *)method, (void*)method->getTemplateInstantiationPattern());
                if (print_logging)
                cerr << fmt::format("Got method {}", method_name) << endl;
                FunctionTemplate::get_or_create(function_template_decl).instantiated();
            } else {
                if (print_logging) cerr << "not interesting method" << endl;
            }
            return;

#endif
        }
#endif // end TEMPLATE_INFO_ONLY
}


void ClassHandler::onStartOfTranslationUnit() {
    log.info(LogSubjects::Subjects::ClassParser, "onStartOfTranslationUnit");

}


void ClassHandler::onEndOfTranslationUnit() {

    log.info(LogSubjects::Subjects::ClassParser, "onEndOfTranslationUnit");
    log.info(LogSubjects::Subjects::ClassParser, "Processed total of {} classes from ASTMatchers", matched_classes_returned);




    vector<WrappedClass const *> const should_be_wrapped_classes = [&] {
        vector<WrappedClass const *> results;
        bool found_data_error = false;
        for (auto & c : WrappedClass::wrapped_classes) {
            for (auto const & error : c->data_errors) {
                std::cerr << fmt::format("ERROR in {}: '{}'", c->get_name_alias(), error) << std::endl;
                found_data_error = true;
                continue;
            }
            if (c->should_be_wrapped()) {
                xl::log::LogCallbackGuard g(log, c->log_watcher);
                c->parse_enums();
                c->parse_members();
                c->parse_all_methods();
                results.push_back(c.get());
            }
        }
        if (found_data_error) {
            llvm::report_fatal_error("Aborting due to data errors listed above");
        }
        return results;
    }();


    if (this->output_modules.empty()) {
        cerr << "NO OUTPUT MODULES SPECIFIED - did you mean to pass --use-default-output-modules" << endl;
        llvm::report_fatal_error("No output modules specified, aborting...");
    }

//    std::cerr << fmt::format("right before processing output modules, log status: {}", log.get_status_string()) << std::endl;
    log.info(LogSubjects::Subjects::ClassParser, "About to run through {} output modules", this->output_modules.size());

    for (auto & output_module : this->output_modules) {

        output_module->_begin();
        log.info(LogSubjects::Subjects::ClassParser, "{} processing", output_module->get_name());

        output_module->process(xl::copy_if(
            should_be_wrapped_classes,
            [&](WrappedClass const * c){return output_module->get_criteria()(*c);}
        ));
        log.info(LogSubjects::Subjects::ClassParser, "{} done processing", output_module->get_name());

        output_module->_end();
        for (auto const & error : output_module->log_watcher.errors) {
            std::cerr << fmt::format("Error during output module: {}: '{}'", output_module->get_name(), error.string) << std::endl;
        }
    }

}




ClassHandler::ClassHandler(CompilerInstance & CI, vector<unique_ptr<OutputModule>> const & output_modules) :
    source_manager(CI.getSourceManager()),
    output_modules(output_modules),
    ci(CI)
{
    log.info(LogSubjects::Subjects::ClassParser, "ClassHandler constructor");
}


} // end namespace v8toolkit::class_parser