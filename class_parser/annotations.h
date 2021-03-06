#pragma once

#include "clang_fwd.h"

#include <regex>
#include <iostream>
#include <fmt/ostream.h>
#include <set>
#include <string>
#include <unordered_map>


namespace v8toolkit::class_parser {


class Annotations {
    std::set<std::string> annotations;

    void get_annotations_for_decl(const Decl * decl_to_check);


public:

    Annotations(const Decl * decl_to_check) {
        get_annotations_for_decl(decl_to_check);
    }


    Annotations(const CXXMethodDecl * decl_to_check);

    Annotations() = default;

    const std::vector<std::string> get() const;

    std::vector<std::string> get_regex(const std::string & regex_string) const;

    bool has(const std::string & target) const {
        return std::find(annotations.begin(), annotations.end(), target) != annotations.end();
    }

    void merge(const Annotations & other) {
//        cerr << fmt::format("Merging in {} annotations onto {} existing ones", other.get().size(), this->get().size()) << endl;
        annotations.insert(other.annotations.begin(), other.annotations.end());
    }


    // holds a list of templates and associated annotations.  These annotations will be merged with classes created
    //   from the template.  This allows metadata associated with all instantiations of a template
    static inline std::unordered_map<const ClassTemplateDecl *, Annotations> annotations_for_class_templates;

    // any annotations on 'using' statements should be applied to the actual CXXRecordDecl being aliased (the right side)
    static inline std::unordered_map<const CXXRecordDecl *, Annotations> annotations_for_record_decls;


    // if a template instantiation is named with a 'using' statement, use that alias for the type isntead of the template/class name itself
    //   this stops them all from being named the same thing - aka CppFactory, CppFactory, ...  instead of MyThingFactory, MyOtherThingFactory, ...
    static inline std::unordered_map<const CXXRecordDecl *, std::string> names_for_record_decls;


    Annotations(const CXXRecordDecl * decl_to_check);
};


}