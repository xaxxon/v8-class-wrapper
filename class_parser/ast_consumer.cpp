
#include "ast_consumer.h"

using namespace clang::ast_matchers;

namespace v8toolkit::class_parser {

MyASTConsumer::MyASTConsumer(CompilerInstance & ci) :
    HandlerForClass(ci), ci(ci) {

#ifdef TEMPLATE_INFO_ONLY

    Matcher.addMatcher(decl(anyOf(
                          classTemplateSpecializationDecl().bind("class"),
                          cxxMethodDecl().bind("method")
                          )),
                       &HandlerForClass);

#else
    Matcher.addMatcher(cxxRecordDecl(
        allOf(
            // skip classes from v8toolkit::JSWrapper
            unless(isDerivedFrom("::v8toolkit::JSWrapper")), // JS-Classes will be completely regenerated

            // must be a struct or class
            anyOf(isStruct(), isClass()),
            anyOf( // order of these matters.   If a class matches more than one it will only be returned as the first


                allOf(isDefinition(),
                      cxxRecordDecl(isDerivedFrom("::v8toolkit::WrappedClassBase")).bind(
                          "class derived from WrappedClassBase")),

                // mark a type you control with the wrapped class attribute
                allOf(isDefinition(),
                      unless(matchesName("^::std::")),
                      cxxRecordDecl(/*hasAttr(attr::Annotate)*/).bind("not std:: class")),

                // used for marking types not under your control with the wrapped class attribute
                allOf(unless(isDefinition()),
                      unless(matchesName("^::std::")),
                      cxxRecordDecl(hasAttr(attr::Annotate)).bind("forward declaration with annotation"))
            )
        )), &HandlerForClass);

    Matcher.addMatcher(
        namedDecl(
            allOf(
                hasAttr(attr::Annotate), // must have V8TOOLKIT_NAME_ALIAS set
                unless(matchesName("^::std::")),
                unless(matchesName("^::__")
                )
            )
        ).bind("named decl"), &HandlerForClass);
#endif

}

}

