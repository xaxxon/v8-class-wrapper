#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

#include <functional>
#include <iostream>
#include <map>
#include <vector>
#include <utility>
#include <assert.h>

#include "v8toolkit.h"
#include "casts.hpp"

namespace v8toolkit {

template<class T>
struct is_const_member_function{enum {value = 0};};

template<class R, class T, class...Args>
struct is_const_member_function<R(T::*)(Args...)const> {
        enum {value = 1};
};

template<bool> struct const_type_method_adder;



#define V8_CLASS_WRAPPER_DEBUG false

/**
* Design Questions:
* - When a c++ object returns a new object represented by one of its members, should it
*   return the same javascript object each time as well?  
*     class Thing {
*       OtherClass other_class;
*       OtherClass & get_other_class(){return this->other_class;} <== should this return the same javascript object on each call for the same Thing object
*     }
*   - that's currently what the existing_wrapped_objects map is for, but if a new object
*     of the same time is created at the same address as an old one, the old javascript 
*     object will be returned.
*     - how would you track if the c++ object source object for an object went away?
*     - how would you actually GC the old object when containing object went away?
*   - Maybe allow some type of optional class customization to help give hints to V8ClassWrapper to have better behavior
*
*/


/*
    How to add static methods to every object as it is created?  You can add them by hand afterwards
    with v8toolkit::add_function, but there should be a way in v8classwrapper to say every object
    of the type gets the method, too
*/



/***
* set of classes for determining what to do do the underlying c++
*   object when the javascript object is garbage collected
*/
template<class T>
struct DestructorBehavior 
{
	virtual void operator()(v8::Isolate * isolate, T* object) const = 0;
};


/**
* Helper to delete a C++ object when the corresponding javascript object is garbage collected
*/
template<class T>
struct DestructorBehavior_Delete : DestructorBehavior<T> 
{
	void operator()(v8::Isolate * isolate, T* object) const 
	{
		if (V8_CLASS_WRAPPER_DEBUG) printf("Deleting object at %p during V8 garbage collection\n", object);
		delete object;
		isolate->AdjustAmountOfExternalAllocatedMemory(-static_cast<int64_t>(sizeof(T)));
	}
};

/**
* Helper to not do anything to the underlying C++ object when the corresponding javascript object
*   is garbage collected
*/
template<class T>
struct DestructorBehavior_LeaveAlone : DestructorBehavior<T> 
{
	void operator()(v8::Isolate * isolate, T* object) const 
	{
		if (V8_CLASS_WRAPPER_DEBUG) printf("Not deleting object %p during V8 garbage collection\n", object);
	}
};



template<class T>
struct TypeCheckerBase {
  public:
      virtual ~TypeCheckerBase(){}
      virtual T * check(AnyBase *) = 0;
};

template<class, class...>
struct TypeChecker;

template<class T, class Head>
struct TypeChecker<T, Head> : public TypeCheckerBase<T>
{
    T * check(AnyBase * any_base) {
        AnyPtr<Head> * any = nullptr;
        if (V8_CLASS_WRAPPER_DEBUG) printf("TypeChecker checking against %s\n", typeid(Head).name());
        if ((any = dynamic_cast<AnyPtr<Head> *>(any_base)) != nullptr) {
            return static_cast<T*>(any->get());
        } else {
            return nullptr;
        }
    }
};

// tests an AnyBase * against a list of types compatible with T
//   to see if the AnyBase is an Any<TypeList...> ihn
template<class T, class Head, class... Tail>
struct TypeChecker<T, Head, Tail...> : public TypeChecker<T, Tail...> {
    using SUPER = TypeChecker<T, Tail...>;
    T * check(AnyBase * any_base) {
        AnyPtr<Head> * any = nullptr;
        if((any = dynamic_cast<AnyPtr<Head> *>(any_base)) != nullptr) {
            return static_cast<T*>(any->get());
        } else {
            return SUPER::check(any_base);
        }
    }
};


/**
* Provides a mechanism for creating javascript-ready objects from an arbitrary C++ class
* Can provide a JS constructor method or wrap objects created in another c++ function
*
* Const types should not be wrapped directly.   Instead, a const version of a non-const type will
* automatically be created and populated with read-only members and any const-qualified method added
* to the non-const version.
*
* All members/methods must be added, then finalize() called, then any desired constructors may be created.
*
*
*/
template<class T>
class V8ClassWrapper
{
private:

	/**
	 * Wrapped classes are per-isolate, so this tracks each wrapped class/isolate tuple for later retrieval
	 */
	static std::map<v8::Isolate *, V8ClassWrapper<T> *> isolate_to_wrapper_map;


	V8ClassWrapper<T>() = delete;
	V8ClassWrapper<T>(const V8ClassWrapper<T> &) = delete;
	V8ClassWrapper<T>(const V8ClassWrapper<T> &&) = delete;
	V8ClassWrapper<T>& operator=(const V8ClassWrapper<T> &) = delete;
	V8ClassWrapper<T>& operator=(const V8ClassWrapper<T> &&) = delete;
	
	
	/**
	 * users of the library should call get_instance, not this constructor directly
	 */
	V8ClassWrapper(v8::Isolate * isolate) : isolate(isolate) {
		this->isolate_to_wrapper_map.emplace(isolate, this);
	}
	
    // function used to return the value of a C++ variable backing a javascript variable visible
    //   via the V8 SetAccessor method
	template<class VALUE_T> // type being returned
	static void _getter_helper(v8::Local<v8::String> property,
	                  const v8::PropertyCallbackInfo<v8::Value>& info) 
	{
		auto isolate = info.GetIsolate();
		v8::Local<v8::Object> self = info.Holder();				   
		v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(self->GetInternalField(0));
        auto cpp_object = V8ClassWrapper<T>::get_instance(isolate).cast(static_cast<AnyBase *>(wrap->Value()));
        if (V8_CLASS_WRAPPER_DEBUG) printf("Getter helper got cpp object: %p\n", cpp_object);
		// This function returns a reference to member in question
		auto member_reference_getter = (std::function<VALUE_T&(T*)> *)v8::External::Cast(*(info.Data()))->Value();
	
		auto & member_ref = (*member_reference_getter)(cpp_object);
		info.GetReturnValue().Set(CastToJS<VALUE_T>()(isolate, member_ref));
	}

    // function used to set the value of a C++ variable backing a javascript variable visible
    //   via the V8 SetAccessor method
	template<typename VALUE_T>
	static void _setter_helper(v8::Local<v8::String> property, v8::Local<v8::Value> value,
	               const v8::PropertyCallbackInfo<void>& info) 
	{
        auto isolate = info.GetIsolate();
		v8::Local<v8::Object> self = info.Holder();		   
		v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(self->GetInternalField(0));
		T * cpp_object = V8ClassWrapper<T>::get_instance(isolate).cast(static_cast<AnyBase *>(wrap->Value()));

		auto member_reference_getter = (std::function<VALUE_T&(T*)> *)v8::External::Cast(*(info.Data()))->Value();
		auto & member_ref = (*member_reference_getter)(cpp_object);
	  	member_ref = CastToNative<typename std::remove_reference<VALUE_T>::type>()(isolate, value);
	}
    


	// Helper for creating objects when "new MyClass" is called from javascript
	template<typename ... CONSTRUCTOR_PARAMETER_TYPES>
	static void v8_constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
		auto isolate = args.GetIsolate();
        
        
        // printf("v8 constructor creating type %s\n", typeid(T).name());
		T * new_cpp_object = nullptr;
		std::function<void(CONSTRUCTOR_PARAMETER_TYPES...)> constructor = [&new_cpp_object](auto... args)->void{new_cpp_object = new T(args...);};
        
        using PB_TYPE = ParameterBuilder<0, decltype(constructor), TypeList<CONSTRUCTOR_PARAMETER_TYPES...>>;
        if (!check_parameter_builder_parameter_count<PB_TYPE, 0>(args)) {
            // printf("v8_constructor for %s got %d parameters but needed %d parameters\n", typeid(T).name(), (int)args.Length(), (int)PB_TYPE::ARITY);
            isolate->ThrowException(v8::String::NewFromUtf8(isolate, "Constructor parameter mismatch"));
            return;
        }
        
		PB_TYPE()(constructor, args);

		if (V8_CLASS_WRAPPER_DEBUG) printf("In v8_constructor and created new cpp object at %p\n", new_cpp_object);

		// if the object was created by calling new in javascript, it should be deleted when the garbage collector 
		//   GC's the javascript object, there should be no c++ references to it
		initialize_new_js_object<DestructorBehavior_Delete<T>>(isolate, args.This(), new_cpp_object);
		
		// // return the object to the javascript caller
		args.GetReturnValue().Set(args.This());
	}
	
	// takes a Data() parameter of a StdFunctionCallbackType lambda and calls it
	//   Useful because capturing lambdas don't have a traditional function pointer type
	static void callback_helper(const v8::FunctionCallbackInfo<v8::Value>& args) 
	{
		StdFunctionCallbackType * callback_lambda = (StdFunctionCallbackType *)v8::External::Cast(*(args.Data()))->Value();		
		(*callback_lambda)(args);
	}

	std::map<T *, v8::Global<v8::Object>> existing_wrapped_objects;
	v8::Isolate * isolate;

    // Stores a functor capable of converting compatible types into a <T> object
    std::unique_ptr<TypeCheckerBase<T>> type_checker;
        
	/**
	* Stores a function template with any methods from the parent already in place.
	* Used as the prototype for any new object
	*/
    v8::Global<v8::FunctionTemplate> global_parent_function_template;

    /**
    * Have to store all the function templates this class wrapper has ever made so
    *   they can all be tried as parameters to v8::Object::GetInstanceFromPrototypeChain
    */
    std::vector<v8::Global<v8::FunctionTemplate>> this_class_function_templates;

    
    /**
    * Forces user to state that all members/methods have been added before any
    *   instances of the wrapped object are created
    */
    bool finalized = false;
	
public:
	
	
	// Common tasks to do for any new js object regardless of how it is created
	template<class DestructorBehavior>
	static void initialize_new_js_object(v8::Isolate * isolate, v8::Local<v8::Object> js_object, T * cpp_object) 
	{
        if (V8_CLASS_WRAPPER_DEBUG) printf("Initializing new js object for %s for v8::object at %p and cpp object at %p\n", typeid(T).name(), *js_object, cpp_object);
        auto any = new AnyPtr<T>(cpp_object);
        if (V8_CLASS_WRAPPER_DEBUG) printf("inserting anyptr<%s>at address %p pointing to cpp object at %p\n", typeid(T).name(), any, cpp_object);
		assert(js_object->InternalFieldCount() >= 1);
	    js_object->SetInternalField(0, v8::External::New(isolate, static_cast<AnyBase*>(any)));
		
		// tell V8 about the memory we allocated so it knows when to do garbage collection
		isolate->AdjustAmountOfExternalAllocatedMemory(sizeof(T));
		
		v8toolkit::global_set_weak(isolate, js_object, [isolate, cpp_object]() {
				DestructorBehavior()(isolate, cpp_object);
			}
		);
	}
	
	
    /**
    * Creates a new v8::FunctionTemplate capabale of creating wrapped T objects based on previously added methods and members.
    * TODO: This needs to track all FunctionTemplates ever created so it can try to use them in GetInstanceByPrototypeChain
    */
    v8::Local<v8::FunctionTemplate> make_function_template(v8::FunctionCallback callback = nullptr,
														   const v8::Local<v8::Value> & data = v8::Local<v8::Value>()) {
        assert(this->finalized == true);

        auto function_template = v8::FunctionTemplate::New(isolate, callback, data);
        init_instance_object_template(function_template->InstanceTemplate());
        init_prototype_object_template(function_template->PrototypeTemplate());
		init_static_methods(function_template);

        function_template->SetClassName(v8::String::NewFromUtf8(isolate, typeid(T).name()));
        
        // printf("Making function template for type %s\n", typeid(T).name());
        
        // if there is a parent type set, set that as this object's prototype
        auto parent_function_template = global_parent_function_template.Get(isolate);
        if (!parent_function_template.IsEmpty()) {
            // printf("FOUND PARENT TYPE of %s, USING ITS PROTOTYPE AS PARENT PROTOTYPE\n", typeid(T).name());
            function_template->Inherit(parent_function_template);
        }

		// printf("Adding this_class_function_template for %s\n", typeid(T).name());
        this_class_function_templates.emplace_back(v8::Global<v8::FunctionTemplate>(isolate, function_template));
        return function_template;
    }


    /**
    * Returns an existing constructor function template for the class/isolate OR creates one if none exist.
    *   This is to keep the number of constructor function templates as small as possible because looking up
    *   which one created an object takes linear time based on the number that exist
    */
    v8::Local<v8::FunctionTemplate> get_function_template()
    {
        if (this_class_function_templates.empty()){
			// printf("Making function template because there isn't one %s\n", typeid(T).name());
            // this will store it for later use automatically
            return make_function_template();
        } else {
			// printf("Not making function template because there is already one %s\n", typeid(T).name());
            // return an arbitrary one, since they're all the same when used to call .NewInstance()
            return this_class_function_templates[0].Get(isolate);
        }
    }


	T * get_cpp_object(v8::Local<v8::Object> object) {
		auto wrap = v8::Local<v8::External>::Cast(object->GetInternalField(0));

	    if (V8_CLASS_WRAPPER_DEBUG) printf("uncasted internal field: %p\n", wrap->Value());
	    return this->cast(static_cast<AnyBase *>(wrap->Value()));
    
	}
	
	
	/**
	 * Check to see if an object can be converted to type T, else return nullptr
	 */
    T * cast(AnyBase * any_base)
    {
        if (V8_CLASS_WRAPPER_DEBUG) printf("In ClassWrapper::cast for type %s\n", typeid(T).name());
        if(type_checker != nullptr) {
            if (V8_CLASS_WRAPPER_DEBUG) printf("Explicit compatible types set, using that\n");
            return type_checker->check(any_base);
        } else if (dynamic_cast<AnyPtr<T>*>(any_base)) {
            if (V8_CLASS_WRAPPER_DEBUG) printf("No explicit compatible types, but successfully cast to self-type\n");
            return static_cast<AnyPtr<T>*>(any_base)->get();
        }
        // if it's already not const, it's ok to run it again
        else if (dynamic_cast<AnyPtr<typename std::remove_const<T>::type>*>(any_base)) {
            return static_cast<AnyPtr<typename std::remove_const<T>::type>*>(any_base)->get();
        }
        if (V8_CLASS_WRAPPER_DEBUG) printf("Cast was sad :( returning nullptr\n");
        return nullptr;
    }
    
    void init_instance_object_template(v8::Local<v8::ObjectTemplate> object_template) {
		object_template->SetInternalFieldCount(1);
        for (auto & adder : this->member_adders) {
            adder(object_template);
        }
    }

    void init_prototype_object_template(v8::Local<v8::ObjectTemplate> object_template) {
        for (auto & adder : this->method_adders) {
            adder(object_template);
        }
		for (auto & adder : this->fake_method_adders) {
			adder(object_template);
		}
    }

	void init_static_methods(v8::Local<v8::FunctionTemplate> constructor_function_template) {

		for (auto & adder : this->static_method_adders) {
			adder(constructor_function_template);
		}
	}


    
	
	/**
	* Returns a "singleton-per-isolate" instance of the V8ClassWrapper for the wrapped class type.
	* For each isolate you need to add constructors/methods/members separately.
	*/
	static V8ClassWrapper<T> & get_instance(v8::Isolate * isolate) 
	{
		if (V8_CLASS_WRAPPER_DEBUG) printf("isolate to wrapper map %p size: %d\n", &isolate_to_wrapper_map, (int)isolate_to_wrapper_map.size());
		if (isolate_to_wrapper_map.find(isolate) == isolate_to_wrapper_map.end()) {
			auto new_object = new V8ClassWrapper<T>(isolate);
			if (V8_CLASS_WRAPPER_DEBUG) printf("Creating instance %p for isolate: %p\n", new_object, isolate);
		}
		if (V8_CLASS_WRAPPER_DEBUG) printf("(after) isolate to wrapper map size: %d\n", (int)isolate_to_wrapper_map.size());
		
		auto object = isolate_to_wrapper_map[isolate];
		if (V8_CLASS_WRAPPER_DEBUG) printf("Returning v8 wrapper: %p\n", object);
		return *object;
	}
    

    /**
    * Species other types that can be substituted for T when calling a function expecting T
    *   but T is not being passsed.   Only available for classes derived from T.
    * T is always compatible and should not be specified here.
    * Not calling this means that only T objects will be accepted for things that want a T.
    * There is no automatic determination of inherited types by this library because I cannot
    *   figure out how.
    * It's VERY important that any type marked as having this type as a parent be marked as
    *   being a compatible type.
    */
    template<class... CompatibleTypes>
    std::enable_if_t<static_all_of<std::is_base_of<T,CompatibleTypes>::value...>::value, V8ClassWrapper<T>&>
    set_compatible_types()
    {
        assert(!is_finalized());
        type_checker.reset(new TypeChecker<T, T, typename std::remove_const<T>::type, CompatibleTypes...>());
        return *this;
    }
	
	
	/**
	* This wrapped class will inherit all the methods from the parent type (and its parent...)
    *
    * It is VERY important that the type being marked as the parent type has this type set with
    *   set_compatible_types<>()
	*/
    template<class ParentType>
    std::enable_if_t<std::is_base_of<ParentType, T>::value, V8ClassWrapper<T>&>
    set_parent_type()
    {
        assert(!is_finalized());
        assert(V8ClassWrapper<ParentType>::get_instance(isolate).is_finalized());
        scoped_run(isolate, [this]{
            global_parent_function_template =
                v8::Global<v8::FunctionTemplate>(isolate, V8ClassWrapper<ParentType>::get_instance(isolate).get_function_template());
        });
        return *this;
    }
    
    
	/**
	* V8ClassWrapper objects shouldn't be deleted during the normal flow of your program unless the associated isolate
	*   is going away forever.   Things will break otherwise as no additional objects will be able to be created
	*   even though V8 will still present the ability to your javascript (I think)
	*/
	virtual ~V8ClassWrapper()
	{
		// this was happening when it wasn't supposed to, like when making temp copies.   need to disable copying or something
		//   if this line is to be added back
		// isolate_to_wrapper_map.erase(this->isolate);
	}


	/**
	* Creates a javascript method with the specified name inside `parent_template` which, when called with the "new" keyword, will return
	*   a new object of this type.
	*/
	template<typename ... CONSTRUCTOR_PARAMETER_TYPES>
	v8toolkit::V8ClassWrapper<T>& add_constructor(std::string js_constructor_name, v8::Local<v8::ObjectTemplate> parent_template)
	{				
        assert(((void)"Type must be finalized before calling add_constructor", this->finalized) == true);
        
		// create a function template even if no javascript constructor will be used so 
		//   FunctionTemplate::InstanceTemplate can be populated.   That way if a javascript constructor is added
		//   later the FunctionTemplate will be ready to go
        auto constructor_template = make_function_template(V8ClassWrapper<T>::v8_constructor<CONSTRUCTOR_PARAMETER_TYPES...>, v8::Local<v8::Value>());


		// Add the constructor function to the parent object template (often the global template)
		parent_template->Set(v8::String::NewFromUtf8(isolate, js_constructor_name.c_str()), constructor_template);



		return *this;
	}


	/**
	* Used when wanting to return an object from a c++ function call back to javascript, or in conjunction with
    *   add_variable to give a javascript name to an existing c++ object 
    * \code{cpp}
    * add_variable(context, context->GetGlobal(), "js_name", class_wrapper.wrap_existing_cpp_object(context, some_c++_object));
    * \endcode
	*/
	template<class BEHAVIOR>
	v8::Local<v8::Value> wrap_existing_cpp_object(v8::Local<v8::Context> context, T * existing_cpp_object) 
	{
		auto isolate = this->isolate;
        
        
        // if it's not finalized, try to find an existing CastToJS conversion because it's not a wrapped class
        if (!this->is_finalized()) {    
            // printf("wrap existing cpp object cast to js %s\n", typeid(T).name());
            return CastToJS<T>()(isolate, *existing_cpp_object);
        }
                
		if (V8_CLASS_WRAPPER_DEBUG) printf("Wrapping existing c++ object %p in v8 wrapper this: %p isolate %p\n", existing_cpp_object, this, isolate);
		
		// if there's currently a javascript object wrapping this pointer, return that instead of making a new one
        //   This makes sure if the same object is returned multiple times, the javascript object is also the same
		v8::Local<v8::Object> javascript_object;
		if(this->existing_wrapped_objects.find(existing_cpp_object) != this->existing_wrapped_objects.end()) {
			if (V8_CLASS_WRAPPER_DEBUG) printf("Found existing javascript object for c++ object %p\n", existing_cpp_object);
			javascript_object = v8::Local<v8::Object>::New(isolate, this->existing_wrapped_objects[existing_cpp_object]);
			
		} else {
		
			if (V8_CLASS_WRAPPER_DEBUG) printf("Creating new javascript object for c++ object %p\n", existing_cpp_object);
		
			// TODO: Remove these?
			v8::Isolate::Scope is(isolate);
			v8::Context::Scope cs(context);
		
            javascript_object = get_function_template()->GetFunction()->NewInstance();
            // printf("New object is empty?  %s\n", javascript_object.IsEmpty()?"yes":"no");
            // printf("Created new JS object to wrap existing C++ object.  Internal field count: %d\n", javascript_object->InternalFieldCount());
            
			initialize_new_js_object<BEHAVIOR>(isolate, javascript_object, existing_cpp_object);
			
            this->existing_wrapped_objects.emplace(existing_cpp_object, v8::Global<v8::Object>(isolate, javascript_object));
			if (V8_CLASS_WRAPPER_DEBUG) printf("Inserting new %s object into existing_wrapped_objects hash that is now of size: %d\n", typeid(T).name(), (int)this->existing_wrapped_objects.size());			
		}
        if (V8_CLASS_WRAPPER_DEBUG) printf("Wrap existing cpp object returning object about to be cast to a value: %s\n", *v8::String::Utf8Value(javascript_object));
		return v8::Local<v8::Value>::Cast(javascript_object);
	}


	typedef std::function<void(const v8::FunctionCallbackInfo<v8::Value>& info)> StdFunctionCallbackType;

    using AttributeAdder = std::function<void(v8::Local<v8::ObjectTemplate> &)>;
    std::vector<AttributeAdder> member_adders;

	using StaticMethodAdder = std::function<void(v8::Local<v8::FunctionTemplate>)>;
	std::vector<StaticMethodAdder> static_method_adders;

	// stores callbacks to add calls to lambdas whos first parameter is of type T* and are automatically passed
	//   the "this" pointer before any javascript parameters are passed in
	using FakeMethodAdder = std::function<void(v8::Local<v8::ObjectTemplate>)>;
	std::vector<FakeMethodAdder> fake_method_adders;


	template<class Callable>
	V8ClassWrapper<T> & add_static_method(const std::string & method_name, Callable callable) {

		if (!std::is_const<T>::value) {
			V8ClassWrapper<typename std::add_const<T>::type>::get_instance(isolate).add_static_method(method_name, callable);
		}

		// must be set before finalization
		assert(!this->finalized);

		auto static_method_adder = [this, method_name, callable](v8::Local<v8::FunctionTemplate> constructor_function_template) {

			auto static_method_function_template = v8toolkit::make_function_template(this->isolate,
																					 callable);
			constructor_function_template->Set(this->isolate,
											   method_name.c_str(),
											   static_method_function_template);
		};

		this->static_method_adders.emplace_back(static_method_adder);

		return *this;
	}

    
    /**
    * Function to force API user to declare that all members/methods have been added before any
    *   objects of the wrapped type can be created to make sure everything stays consistent
    * Must be called before adding any constructors or using wrap_existing_object()
    */
    V8ClassWrapper<T> & finalize() {

        if (!std::is_const<T>::value) {
            V8ClassWrapper<typename std::add_const<T>::type>::get_instance(isolate).finalize();
        }

        this->finalized = true;
        get_function_template(); // force creation of a function template that doesn't call v8_constructo
        return *this;
    }


    /**
    * returns whether finalize() has been called on this type for this isolate
    */
    bool is_finalized()
    {
        return this->finalized;
    }

//    template<class Method, std::enable_if_t<is_const_member_function<Method>::value && !std::is_const<T>::value, int> = 0>
//    void add_member_for_const_type(const std::string & method_name, Method method) {
//        V8ClassWrapper<typename std::add_const<T>::type>::get_instance(isolate).add_method(method_name, method);
//        printf("Adding to const version: %d %s :: %s\n", std::is_const<T>::value, typeid(T).name(), typeid(Method).name());
//    };
//
//    template<class Method, std::enable_if_t<!(is_const_member_function<Method>::value && !std::is_const<T>::value), int> = 0>
//    void add_member_for_const_type(const std::string & method_name, Method method) {
//        printf("Not adding to const version: %d %s :: %s\n", std::is_const<T>::value, typeid(T).name(), typeid(Method).name());
//    };


    /**
    * Adds a getter and setter method for the specified class member
    * add_member(&ClassName::member_name, "javascript_attribute_name");
    */
    // allow members from parent types of T
    template<class MEMBER_TYPE, class MemberClass, std::enable_if_t<std::is_base_of<MemberClass, T>::value, int> = 0>
	V8ClassWrapper<T> & add_member(std::string member_name, MEMBER_TYPE MemberClass::* member)
	{
        assert(this->finalized == false);

        if (!std::is_const<T>::value) {
            V8ClassWrapper<typename std::add_const<T>::type>::get_instance(isolate).add_member_readonly(member_name, member);
        }

		// store a function for adding the member on to an object template in the future
		member_adders.emplace_back([this, member, member_name](v8::Local<v8::ObjectTemplate> & constructor_template){
             
    		auto get_member_reference = new std::function<MEMBER_TYPE&(T*)>([member](T * cpp_object)->MEMBER_TYPE&{
    			return cpp_object->*member;
    		});
            
			constructor_template->SetAccessor(v8::String::NewFromUtf8(isolate, member_name.c_str()), 
				_getter_helper<MEMBER_TYPE>, 
				_setter_helper<MEMBER_TYPE>, 
				v8::External::New(isolate, get_member_reference));
        });
        return *this;
	}


	// allow members from parent types of T
    template<class MEMBER_TYPE, class MemberClass, std::enable_if_t<std::is_base_of<MemberClass, T>::value, int> = 0>
	V8ClassWrapper<T> & add_member_readonly(std::string member_name, MEMBER_TYPE MemberClass::* member)
	{
		// the field may be added read-only even to a non-const type, so make sure it's added to the const type, too
		if (!std::is_const<T>::value) {
			V8ClassWrapper<typename std::add_const<T>::type>::get_instance(isolate).add_member_readonly(member_name, member);
		}


		using RESULT_REF_TYPE = typename std::conditional<std::is_const<T>::value,
                                                 const MEMBER_TYPE &,
                                                 MEMBER_TYPE &>::type;

        assert(this->finalized == false);
        
         member_adders.emplace_back([this, member, member_name](v8::Local<v8::ObjectTemplate> & constructor_template){
             
    		auto get_member_reference = new std::function<RESULT_REF_TYPE(T*)>([member](T * cpp_object)->RESULT_REF_TYPE{
    			return cpp_object->*member;
    		});
            
			constructor_template->SetAccessor(v8::String::NewFromUtf8(isolate, member_name.c_str()), 
				_getter_helper<MEMBER_TYPE>,
                0,
				v8::External::New(isolate, get_member_reference));
        });
        return *this;
	}


	template<class R, class... Args>
	V8ClassWrapper<T> & add_method(const std::string & method_name, R(T::*method)(Args...) const) {
		return _add_method(method_name, method);
	}

    
	/**
	* Adds the ability to call the specified class instance method on an object of this type
	*/
	template<class R, class... Args>
	V8ClassWrapper<T> & add_method(const std::string & method_name, R(T::*method)(Args...))
	{
		return _add_method(method_name, method);
    }
    
    
	/**
	 * If the method is marked const, add it to the const version of the wrapped type
	 */
    template<class Method, std::enable_if_t<is_const_member_function<Method>::value && !std::is_const<T>::value, int> = 0>
    void add_method_for_const_type(const std::string & method_name, Method method) {
        V8ClassWrapper<typename std::add_const<T>::type>::get_instance(isolate).add_method(method_name, method);
//        printf("Adding to const version: %d %s :: %s\n", std::is_const<T>::value, typeid(T).name(), typeid(Method).name());
    };


	/**
	 * If the method is not marked const, don't add it to the const type (since it's incompatible)
	 */
    template<class Method, std::enable_if_t<!(is_const_member_function<Method>::value && !std::is_const<T>::value), int> = 0>
    void add_method_for_const_type(const std::string & method_name, Method method) {
//        printf("Not adding to const version: %d %s :: %s\n", std::is_const<T>::value, typeid(T).name(), typeid(Method).name());
    };




	/**
	* If the method is marked const, add it to the const version of the wrapped type
	*/
	template<class R, class Head, class... Tail, std::enable_if_t<std::is_const<Head>::value && !std::is_const<T>::value, int> = 0>
	void add_fake_method_for_const_type(const std::string & method_name, std::function<R(Head, Tail...)> method) {
		V8ClassWrapper<typename std::add_const<T>::type>::get_instance(isolate).add_fake_method(method_name, method);
	};


	/**
	 * If the method is not marked const, don't add it to the const type (since it's incompatible)
	 */
	template<class R, class Head, class... Tail, std::enable_if_t<!(std::is_const<Head>::value && !std::is_const<T>::value), int> = 0>
	void add_fake_method_for_const_type(const std::string & method_name, std::function<R(Head, Tail...)> method) {
		// nothing to do here
	};


    template<class R, class... Args>
	void add_method(const std::string & method_name, std::function<R(T*, Args...)> & method) {
		_add_fake_method(method_name, method);
	}


	template<class Callback>
	V8ClassWrapper<T> & add_method(const std::string & method_name, Callback && callback) {
		decltype(LTG<Callback>::go(&Callback::operator())) f(callback);
		this->_add_fake_method(method_name, f);

		return *this;
	}


	template<class R, class Head, class... Tail>
	V8ClassWrapper<T> & _add_fake_method(const std::string & method_name, std::function<R(Head, Tail...)> method)
	{
		assert(this->finalized == false);

		add_fake_method_for_const_type(method_name, method);

		// This puts a function on a list that creates a new v8::FunctionTemplate and maps it to "method_name" on the
		// Object template that will be passed in later when the list is traversed
		fake_method_adders.emplace_back([this, method_name, method](v8::Local<v8::ObjectTemplate> prototype_template) {

			auto copy = new std::function<R(Head, Tail...)>(method);


			// This is the actual code associated with "method_name" and called when javascript calls the method
			StdFunctionCallbackType * method_caller =
					new StdFunctionCallbackType([method_name, copy](const v8::FunctionCallbackInfo<v8::Value>& info) {


				auto fake_method = *(std::function<R(Head, Tail...)>*)v8::External::Cast(*(info.Data()))->Value();
				auto isolate = info.GetIsolate();

				auto holder = info.Holder();


				v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(holder->GetInternalField(0));
				T * cpp_object = V8ClassWrapper<T>::get_instance(isolate).cast(static_cast<AnyBase *>(wrap->Value()));


				// the typelist and std::function parameters don't match because the first parameter doesn't come
				// from the javascript value array in 'info', it is passed in from this function as the 'this' pointer
				using PB_TYPE = v8toolkit::ParameterBuilder<0, std::function<R(Head, Tail...)>, TypeList<Tail...>>;

				PB_TYPE pb;
				auto arity = PB_TYPE::ARITY;
				// 1  because the first parameter doesn't count because it's reserved for "this"
				if (!check_parameter_builder_parameter_count<PB_TYPE, 0>(info)) {
				  std::stringstream ss;
				  ss << "Function '" << method_name << "' called from javascript with insufficient parameters.  Requires " << arity << " provided " << info.Length();
				  isolate->ThrowException(v8::String::NewFromUtf8(isolate, ss.str().c_str()));
				  return; // return now so the exception can be thrown inside the javascript
				}

				// V8 does not support C++ exceptions, so all exceptions must be caught before control
				//   is returned to V8 or the program will instantly terminate
				try {
					pb(*copy, info, cpp_object);
				} catch(std::exception & e) {
					isolate->ThrowException(v8::String::NewFromUtf8(isolate, e.what()));
					return;
				}
				return;
			});

			// create a function template, set the lambda created above to be the handler
			auto function_template = v8::FunctionTemplate::New(this->isolate);
			function_template->SetCallHandler(callback_helper, v8::External::New(this->isolate, method_caller));

			// methods are put into the protype of the newly created javascript object
			prototype_template->Set(v8::String::NewFromUtf8(isolate, method_name.c_str()), function_template);
		});
		return *this;
	}

	/**
	 * A list of methods to be added to each object
	 */
    std::vector<AttributeAdder> method_adders;


    template<class M>
    V8ClassWrapper<T> & _add_method(const std::string & method_name, M method)
    {
        assert(this->finalized == false);

        add_method_for_const_type(method_name, method);


		// This puts a function on a list that creates a new v8::FunctionTemplate and maps it to "method_name" on the
		// Object template that will be passed in later when the list is traversed
        method_adders.emplace_back([this, method, method_name](v8::Local<v8::ObjectTemplate> & prototype_template) {

			// This is the actual code associated with "method_name" and called when javascript calls the method
    		StdFunctionCallbackType * method_caller = new StdFunctionCallbackType([this, method, method_name](const v8::FunctionCallbackInfo<v8::Value>& info)
    		{
//                if (V8_CLASS_WRAPPER_DEBUG) printf("In add_method callback for %s for js object at %p / %p (this)\n", typeid(T).name(), *info.Holder(), *info.This());
//                // print_v8_value_details(info.Holder());
//                // print_v8_value_details(info.This());
//                if (V8_CLASS_WRAPPER_DEBUG) printf("holder: %s This: %s\n", *v8::String::Utf8Value(info.Holder()), *v8::String::Utf8Value(info.This()));

                auto isolate = info.GetIsolate();

    			// get the behind-the-scenes c++ object
                // However, Holder() refers to the most-derived object, so the prototype chain must be 
                //   inspected to find the appropriate v8::Object with the T* in its internal field
    			auto holder = info.Holder();
                v8::Local<v8::Object> self;
                                
                if (V8_CLASS_WRAPPER_DEBUG) printf("Looking for instance match in prototype chain %s :: %s\n", typeid(T).name(), typeid(M).name());
                for(auto & function_template : this->this_class_function_templates) {
                    self = holder->FindInstanceInPrototypeChain(function_template.Get(isolate));
                    if(!self.IsEmpty() && !self->IsNull()) {
                        if (V8_CLASS_WRAPPER_DEBUG) printf("Found instance match in prototype chain\n");
                        break;
                    }
                }
                //
                // if(!compare_contents(isolate, holder, self)) {
                //     printf("FOUND DIFFERENT OBJECT");
                // }
//                if (V8_CLASS_WRAPPER_DEBUG) printf("Done looking for instance match in prototype chain\n");
//                if (V8_CLASS_WRAPPER_DEBUG) printf("Match: %s:\n", *v8::String::Utf8Value(self));
//                if (V8_CLASS_WRAPPER_DEBUG) printf("%s\n", stringify_value(isolate, self).c_str());
//                assert(!self.IsEmpty());

                // void* pointer = instance->GetAlignedPointerFromInternalField(0);
    			auto wrap = v8::Local<v8::External>::Cast(self->GetInternalField(0));

//                if (V8_CLASS_WRAPPER_DEBUG) printf("uncasted internal field: %p\n", wrap->Value());
                auto backing_object_pointer = V8ClassWrapper<T>::get_instance(isolate).cast(static_cast<AnyBase *>(wrap->Value()));
                
//			    assert(backing_object_pointer != nullptr);
    			// bind the object and method into a std::function then build the parameters for it and call it
//                if (V8_CLASS_WRAPPER_DEBUG) printf("binding with object %p\n", backing_object_pointer);
    			auto bound_method = v8toolkit::bind(*backing_object_pointer, method);


                using PB_TYPE = v8toolkit::ParameterBuilder<0, decltype(bound_method), decltype(get_typelist_for_function(bound_method))>;
            
                PB_TYPE pb;
                auto arity = PB_TYPE::ARITY;
                if (!check_parameter_builder_parameter_count<PB_TYPE, 0>(info)) {
                    std::stringstream ss;
                    ss << "Function '" << method_name << "' called from javascript with insufficient parameters.  Requires " << arity << " provided " << info.Length();
                    isolate->ThrowException(v8::String::NewFromUtf8(isolate, ss.str().c_str()));
                    return; // return now so the exception can be thrown inside the javascript
                }
            
                // V8 does not support C++ exceptions, so all exceptions must be caught before control
                //   is returned to V8 or the program will instantly terminate
                try {
                    // if (dynamic_cast< JSWrapper<T>* >(backing_object_pointer)) {
                    //     dynamic_cast< JSWrapper<T>* >(backing_object_pointer)->called_from_javascript = true;
                    // }
        			pb(bound_method, info);
                } catch(std::exception & e) {
                    isolate->ThrowException(v8::String::NewFromUtf8(isolate, e.what()));
                    return;
                }
                return;
    		});

			// create a function template, set the lambda created above to be the handler
    		auto function_template = v8::FunctionTemplate::New(this->isolate);
            function_template->SetCallHandler(callback_helper, v8::External::New(this->isolate, method_caller));
		
            // methods are put into the protype of the newly created javascript object
    		prototype_template->Set(v8::String::NewFromUtf8(isolate, method_name.c_str()), function_template);
    	});
        return *this;
    }
};

/**
* Stores the "singleton" per isolate
*/
template <class T> 
std::map<v8::Isolate *, V8ClassWrapper<T> *> V8ClassWrapper<T>::isolate_to_wrapper_map;

template<typename T>
struct CastToJS {

	v8::Local<v8::Value> operator()(v8::Isolate * isolate, T & cpp_object){
		if (V8_CLASS_WRAPPER_DEBUG) printf("In base cast to js struct with lvalue ref\n");
		return CastToJS<typename std::add_pointer<T>::type>()(isolate, &cpp_object);
	}

	/**
	* If an rvalue is passed in, a copy must be made.
	*/
	v8::Local<v8::Value> operator()(v8::Isolate * isolate, T && cpp_object){
		if (V8_CLASS_WRAPPER_DEBUG) printf("In base cast to js struct with rvalue ref");
		if (V8_CLASS_WRAPPER_DEBUG) printf("Asked to convert rvalue type, so copying it first\n");

		// this memory will be owned by the javascript object and cleaned up if/when the GC removes the object
		auto copy = new T(cpp_object);
		auto context = isolate->GetCurrentContext();
		V8ClassWrapper<T> & class_wrapper = V8ClassWrapper<T>::get_instance(isolate);
		auto result = class_wrapper.template wrap_existing_cpp_object<DestructorBehavior_Delete<T>>(context, copy);
        if (V8_CLASS_WRAPPER_DEBUG) printf("CastToJS<T> returning wrapped existing object: %s\n", *v8::String::Utf8Value(result));
        
        return result;
	}
};

/**
* Attempt to use V8ClassWrapper to wrap any remaining types not handled by the specializations in casts.hpp
* That type must have had its methods and members added beforehand in the same isolate
*/
template<typename T>
struct CastToJS<T*> {
	v8::Local<v8::Value> operator()(v8::Isolate * isolate, T * cpp_object){
		if (V8_CLASS_WRAPPER_DEBUG) printf("CastToJS from T*\n");
		auto context = isolate->GetCurrentContext();
		V8ClassWrapper<T> & class_wrapper = V8ClassWrapper<T>::get_instance(isolate);
        if (V8_CLASS_WRAPPER_DEBUG) printf("CastToJS<T*> returning wrapped existing object\n");
		return class_wrapper.template wrap_existing_cpp_object<DestructorBehavior_LeaveAlone<T>>(context, cpp_object);
	}
};

template<typename T>
struct CastToJS<T&> {
	v8::Local<v8::Value> operator()(v8::Isolate * isolate, T & cpp_object){
        using Pointer = typename std::add_pointer_t<T>;
		return CastToJS<Pointer>()(isolate, &cpp_object);
	}
};




template<typename T>
struct CastToNative<T*>
{
	T * operator()(v8::Isolate * isolate, v8::Local<v8::Value> value){
        return & CastToNative<typename std::remove_reference<T>::type>()(isolate, value);
    }
};

template<class T>
std::string type_details(){
    return fmt::format("const: {} pointer: {} reference: {} typeid: {}",
		       std::is_const<T>::value, std::is_pointer<T>::value,
		       std::is_reference<T>::value, typeid(T).name());
 }

/**
 * This can be used from CastToNative<UserType> calls to fall back to if other conversions aren't appropriate
 */
template<class T>
T & get_object_from_embedded_cpp_object(v8::Isolate * isolate, v8::Local<v8::Value> value) {
	if (V8_CLASS_WRAPPER_DEBUG) printf("cast to native\n");
	if(!value->IsObject()){
		printf("CastToNative failed for type: %s (%s)\n", type_details<T>().c_str(), *v8::String::Utf8Value(value));
		throw CastException("No specialized CastToNative found and value was not a Javascript Object");
	}
	auto object = v8::Object::Cast(*value);
	if (object->InternalFieldCount() <= 0) {
		throw CastException(fmt::format("No specialization CastToNative<{}> found and provided Object is not a wrapped C++ object.  It is a native Javascript Object", demangle<T>()));
	}
	v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(object->GetInternalField(0));

	// I don't know any way to determine if a type is
	auto any_base = (v8toolkit::AnyBase *)wrap->Value();
	T * t = nullptr;
	if ((t = V8ClassWrapper<T>::get_instance(isolate).cast(any_base)) == nullptr) {
		printf("Failed to convert types: want:  %d %s, got: %s\n", std::is_const<T>::value, typeid(T).name(), TYPE_DETAILS(*any_base));
		throw CastException(fmt::format("Cannot convert {} to {} {}",
										TYPE_DETAILS(*any_base), std::is_const<T>::value, typeid(T).name()));
	}
	return *t;

}


template<typename T>
struct CastToNative
{
    T & operator()(v8::Isolate * isolate, v8::Local<v8::Value> value) {
		return get_object_from_embedded_cpp_object<T>(isolate, value);
    }
};
    
    
}

