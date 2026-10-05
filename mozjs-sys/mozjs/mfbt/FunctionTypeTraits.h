/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_FunctionTypeTraits_h
#define mozilla_FunctionTypeTraits_h

#include <cstddef> /* for size_t */

namespace mozilla {

// Main FunctionTypeTraits declaration, taking one template argument.
//
// Given a function type, FunctionTypeTraits will expose the following members:
// - ReturnType: Return type.
// - arity: Number of parameters (size_t).
// - ParameterType<N>: Type of the Nth** parameter, 0-indexed.
//
// ** `ParameterType<N>` with `N` >= `arity` is allowed and gives `void`.
// This prevents compilation errors when trying to access a type outside of the
// function's parameters, which is useful for parameters checks, e.g.:
//   template<typename F>
//   auto foo(F&&)
//    -> enable_if(FunctionTypeTraits<F>::arity == 1 &&
//                 is_same<FunctionTypeTraits<F>::template ParameterType<0>,
//                         int>::value,
//                 void)
//   {
//     // This function will only be enabled if `F` takes one `int`.
//     // Without the permissive ParameterType<any N>, it wouldn't even compile.
//
// Note: FunctionTypeTraits does not work with generic lambdas `[](auto&) {}`,
// because parameter types cannot be known until an actual invocation when types
// are inferred from the given arguments.
template <typename T>
struct FunctionTypeTraits;

// Remove reference and pointer wrappers, if any.
template <typename T>
struct FunctionTypeTraits<T&> : FunctionTypeTraits<T> {};
template <typename T>
struct FunctionTypeTraits<T&&> : FunctionTypeTraits<T> {};
template <typename T>
struct FunctionTypeTraits<T*> : FunctionTypeTraits<T> {};

// Extract `operator()` function from callables (e.g. lambdas, std::function).
template <typename T>
struct FunctionTypeTraits : FunctionTypeTraits<decltype(&T::operator())> {};

namespace detail {
template <size_t N, typename... As>
struct SafePackElement;

template <size_t N>
struct SafePackElement<N> {
  using type = void;
};

template <typename A, typename... As>
struct SafePackElement<0, A, As...> {
  using type = A;
};

template <size_t N, typename A, typename... As>
struct SafePackElement<N, A, As...> : SafePackElement<N - 1, As...> {};

template <size_t N, typename... As>
using SafePackElementType = typename SafePackElement<N, As...>::type;

}  // namespace detail

// Specialization for free functions.
template <typename R, typename... As>
struct FunctionTypeTraits<R(As...)> {
  using ReturnType = R;
  static constexpr size_t arity = sizeof...(As);
  template <size_t N>
  using ParameterType = detail::SafePackElementType<N, As...>;
};

// Specialization for non-const member functions.
template <typename C, typename R, typename... As>
struct FunctionTypeTraits<R (C::*)(As...)> : FunctionTypeTraits<R(As...)> {};

// Specialization for const member functions.
template <typename C, typename R, typename... As>
struct FunctionTypeTraits<R (C::*)(As...) const>
    : FunctionTypeTraits<R(As...)> {};

#ifdef NS_HAVE_STDCALL
// Specialization for __stdcall free functions.
template <typename R, typename... As>
struct FunctionTypeTraits<R NS_STDCALL(As...)> : FunctionTypeTraits<R(As...)> {
};

// Specialization for __stdcall non-const member functions.
template <typename C, typename R, typename... As>
struct FunctionTypeTraits<R (NS_STDCALL C::*)(As...)>
    : FunctionTypeTraits<R(As...)> {};

// Specialization for __stdcall const member functions.
template <typename C, typename R, typename... As>
struct FunctionTypeTraits<R (NS_STDCALL C::*)(As...) const>
    : FunctionTypeTraits<R(As...)> {};
#endif  // NS_HAVE_STDCALL

}  // namespace mozilla

#endif  // mozilla_FunctionTypeTraits_h
