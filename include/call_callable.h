
#pragma once

#include <tuple>


#include <v8.h>

#include "v8helpers.h"
#include "casts.hpp"
#include "parameter_builder.h"
#include "stdfunctionreplacement.h"


namespace v8toolkit {



/**
 * Calls a C++ function/callable object with the provided JavaScript objects and takes the value returned by
 * C++ and returns it to JavaScript.  Delegates to ParameterBuilder to generate the correct data for each individual
 * parameter.
 * @tparam Function Callable type
 * @tparam Args These have a different meaning per specialization
 */
template<class Function, class... Args>
struct CallCallable;


/**
 * Call a function where the first argument is specified differently for "fake methods" - functions
 * that aren't proper member instance functions but act like it by taking a pointer to "this" as their first
 * explicit parameter
 */
template<class ReturnType, class InitialArg, class... Args>
struct CallCallable<func::function<ReturnType(InitialArg, Args...)>, InitialArg> {
using NonConstReturnType = std::remove_const_t<ReturnType>;

template<class DefaultArgsTuple = std::tuple<>, std::size_t... ArgIndexes>
void operator()(func::function<ReturnType(InitialArg, Args...)> & function,
                const v8::FunctionCallbackInfo<v8::Value> & info,
                InitialArg initial_arg,
                std::index_sequence<ArgIndexes...>,
                DefaultArgsTuple const & default_args_tuple = DefaultArgsTuple()) {

    int i = 0;

    constexpr int user_parameter_count = sizeof...(Args);
    constexpr int default_arg_count = std::tuple_size<DefaultArgsTuple>::value;

    // while this is negative, no default argument is available
    constexpr int default_parameter_position = default_arg_count - user_parameter_count - 1;

    std::vector<std::unique_ptr<StuffBase>> stuff;
    info.GetReturnValue().
        Set(v8toolkit::CastToJS<ReturnType>()(info.GetIsolate(),
                                              run_function(function, info, std::forward<InitialArg>(initial_arg),
                                                           std::forward<Args>(
                                                               ParameterBuilder<
                                                                   Args>().template operator()<ArgIndexes - default_arg_count>(info, i, stuff, default_args_tuple))...)));
}
};


template<class InitialArg, class... Args>
struct CallCallable<func::function<void(InitialArg, Args...)>, InitialArg> {

template<int default_arg_position, class DefaultArgsTuple = std::tuple<>, std::size_t... ArgIndexes>
void operator()(func::function<void(InitialArg, Args...)> & function,
                const v8::FunctionCallbackInfo<v8::Value> & info,
                InitialArg initial_arg,
                std::index_sequence<ArgIndexes...>,
                DefaultArgsTuple const & default_args_tuple = DefaultArgsTuple()) {

    int i = 0;
    constexpr auto default_arg_count = std::tuple_size<DefaultArgsTuple>::value;

    std::vector<std::unique_ptr<StuffBase>> stuff;
    run_function(function, info, std::forward<InitialArg>(initial_arg),
                 std::forward<Args>(ParameterBuilder<Args>().template operator()<ArgIndexes - default_arg_count - 1>(info, i, stuff,
                                                                                                                     default_args_tuple))...);

}
};


/**
 * CallCallable for a normal function which has a non-void return type
 */
template<class ReturnType, class... Args>
struct CallCallable<func::function<ReturnType(Args...)>> {
using NonConstReturnType = std::remove_const_t<ReturnType>;

template<class... Ts, class DefaultArgsTuple = std::tuple<>, std::size_t... ArgIndexes>
void operator()(func::function<ReturnType(Args...)> & function,
                const v8::FunctionCallbackInfo<v8::Value> & info,
                std::index_sequence<ArgIndexes...>,
                DefaultArgsTuple && default_args_tuple = DefaultArgsTuple(),
                bool return_most_derived = false) {

    int i = 0;

    constexpr int user_parameter_count = sizeof...(Args);
    constexpr int default_arg_count = std::tuple_size<std::remove_reference_t<DefaultArgsTuple>>::value;

    // while this is negative, no default argument is available

    // if there are 3 parameters, 1 default parameter (3-1=2), calls to ParameterBuilder will have positions:
    // 0 - 2 = -2 (no default available)
    // 1 - 2 = -1 (no default available)
    // 2 - 2 = 0 (lookup at std::get<0>(default_args_tuple)
    constexpr int minimum_user_parameters_required = user_parameter_count - default_arg_count;



    std::vector<std::unique_ptr<StuffBase>> stuff;

    info.GetReturnValue().Set(v8toolkit::CastToJS<std::remove_reference_t<ReturnType>>()(info.GetIsolate(),
                                                                                         run_function(function, info, std::forward<Args>(
                                                                                             ParameterBuilder<Args>().template operator()
                                                                                                 <(((int)ArgIndexes) - minimum_user_parameters_required), DefaultArgsTuple> (info, i,
                                                                                                                                                                             stuff, std::move(default_args_tuple)
                                                                                             ))...)));
}
};


/**
 * call callable for a normal function with a void return
 */
template<class... Args>
struct CallCallable<func::function<void(Args...)>> {

template<class DefaultArgsTuple = std::tuple<>, std::size_t... ArgIndexes>
void operator()(func::function<void(Args...)> & function,
                const v8::FunctionCallbackInfo<v8::Value> & info,
                std::index_sequence<ArgIndexes...>,
                DefaultArgsTuple && default_args_tuple = DefaultArgsTuple()) {

    int i = 0;

    constexpr int user_parameter_count = sizeof...(Args);
    constexpr int default_arg_count = std::tuple_size<DefaultArgsTuple>::value;

    // while this is negative, no default argument is available

    // if there are 3 parameters, 1 default parameter (3-1=2), calls to ParameterBuilder will have positions:
    // 0 - 2 = -2 (no default available)
    // 1 - 2 = -1 (no default available)
    // 2 - 2 = 0 (lookup at std::get<0>(default_args_tuple)
    constexpr int minimum_user_parameters_required = user_parameter_count - default_arg_count;

    std::vector<std::unique_ptr<StuffBase>> stuff;
    run_function(function, info,
                 std::forward<Args>(ParameterBuilder<Args>().
                     template operator()<(((int)ArgIndexes) - minimum_user_parameters_required), DefaultArgsTuple>(info, i, stuff, std::move(default_args_tuple)))...);
}
};


/**
 * CallCallable for a function directly taking a v8::FunctionCallbackInfo
 * This requires the function to do everything itself in terms of parsing parameters
 */
template<>
struct CallCallable<func::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>> {

    template<class DefaultArgsTuple = std::tuple<>, std::size_t... ArgIndexes>
    void operator()(func::function<void(const v8::FunctionCallbackInfo<v8::Value> &)> & function,
                    const v8::FunctionCallbackInfo<v8::Value> & info,
                    std::index_sequence<ArgIndexes...>,
                    DefaultArgsTuple const & default_args_tuple = DefaultArgsTuple()) {
        static_assert(std::is_same<DefaultArgsTuple, std::tuple<>>::value,
                      "function taking a v8::FunctionCallbackInfo object cannot have default parameters");
        function(info);
    }
};


} // v8toolkit