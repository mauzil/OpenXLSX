/*
    pybind11/functional.h: std::function<> support

    Copyright (c) 2016 Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#pragma once

#include "pybind11.h"
#include <functional>

NAMESPACE_BEGIN(PYBIND11_NAMESPACE)
NAMESPACE_BEGIN(detail)

template <typename Return, typename... Args>
struct type_caster<std::function<Return(Args...)>> {
    using type = std::function<Return(Args...)>;
    using retval_type = conditional_t<std::is_same<Return, void>::value, void_type, Return>;
    using function_type = Return (*) (Args...);

public:
    bool load(handle src, bool convert) {
        if (src.is_none()) {
            // Defer accepting None to other overloads (if we aren't in convert mode):
            if (!convert) return false;
            return true;
        }

        if (!isinstance<function>(src))
            return false;

        auto func = reinterpret_borrow<function>(src);

        /*
           When passing a C++ function as an argument to another C++
           function via Python, every function call would normally involve
           a full C++ -> Python -> C++ roundtrip, which can be prohibitive.
           Here, we try to at least detect the case where the function is
           stateless (i.e. function pointer or lambda function without
           captured variables), in which case the roundtrip can be avoided.
         */
        if (auto cfunc = func.cpp_function()) {
            auto c = reinterpret_borrow<capsule>(PyCFunction_GET_SELF(cfunc.ptr()));
            auto rec = (function_record *) c;

            if (rec && rec->is_stateless &&
                    same_type(typeid(function_type), *reinterpret_cast<const std::type_info *>(rec->data[1]))) {
                struct capture { function_type f; };
                value = ((capture *) &rec->data)->f;
                return true;
            }
        }

        // ensure GIL is held during functor destruction
        struct func_handle {
            function f;
            func_handle(function&& f_) : f(std::move(f_)) {}
            func_handle(const func_handle&) = default;
            ~func_handle() {
                gil_scoped_acquire acq;
                function kill_f(std::move(f));
            }
        };

        // to emulate 'move initialization capture' in C++11
        struct func_wrapper {
            func_handle hfunc;
            func_wrapper(func_handle&& hf): hfunc(std::move(hf)) {}
            Return operator()(Args... args) const {
                gil_scoped_acquire acq;
                object retval(hfunc.f(std::forward<Args>(args)...));
                /* Visual studio 2015 parser issue: need parentheses around this expression */
                return (retval.cast<Return>());
            }
        };

        value = func_wrapper(func_handle(std::move(func)));
        return true;
    }

    template <typename Func>
    static handle cast(Func &&f_, return_value_policy policy, handle /* parent */) {
        if (!f_)
            return none().inc_ref();

        auto result = f_.target<function_type>();
        if (result)
            return cpp_function(*result, policy).release();
        else
            return cpp_function(std::forward<Func>(f_), policy).release();
    }

    PYBIND11_TYPE_CASTER(type, _("Callable[[") + concat(make_caster<Args>::name...) + _("], ")
                               + make_caster<retval_type>::name + _("]"));
};

NAMESPACE_END(detail)
NAMESPACE_END(PYBIND11_NAMESPACE)
