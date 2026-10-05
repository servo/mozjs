/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GC Policy Mechanism

// GCPolicy controls how the GC interacts with a given type for functionality
// that is used by generic code. It is implemented for:
//
//  - direct pointers to GC things, e.g. JSObject* or JSString*
//
//  - tagged and/or optional pointers to GC things, e.g. JS::Value or jsid
//
//  - structures containing GC pointers, e.g. JS::PropertyDescriptor
//
//  - C++ container types, e.g. GCHashMap
//
// The GCPolicy for type |T| provides at a minimum:
//
//   static void trace(JSTracer, T* tp, const char* name)
//
//     Trace the edge |*tp|, calling the edge |name|. Generic containers
//     like GCHashMap and GCHashSet use this method to trace their children.
//
//   static bool traceWeak(T* tp)
//
//     Update any GC edges if their target has been moved. Return false if the
//     target of the edge should be be removed.
//
//     For edges to GC things, this will return false if the target will be
//     collected in the current GC. In general, for structures this should call
//     |traceWeak| on internal GC edges and return whether the result was true
//     for all of them.
//
//     Containers can use this to remove entries containing GC things that are
//     going to be collected (e.g. GCVector).
//
//     For non-GC thing edges (including nullptr edges) this will return true.
//
//   static bool isValid(const T& t)
//
//     Check that |t| is valid and is not corrupt in some way. The built-in GC
//     types do some memory layout checks. This is for assertions only; it is ok
//     to always return true.
//
// The default GCPolicy<T> assumes that T has a default constructor and |trace|
// and |traceWeak| methods, and forwards to them. GCPolicy has appropriate
// specializations for pointers to GC things and pointer-like types like
// JS::Heap<T> and mozilla::UniquePtr<T>.
//
// There are some stock structs your specializations can inherit from.
// IgnoreGCPolicy<T> does nothing. StructGCPolicy<T> forwards the methods to the
// referent type T.

#ifndef GCPolicyAPI_h
#define GCPolicyAPI_h

#include "mozilla/Maybe.h"
#include "mozilla/UniquePtr.h"

#include <type_traits>

#include "js/GCTypeMacros.h"  // JS_FOR_EACH_PUBLIC_GC_POINTER_TYPE
#include "js/TraceKind.h"
#include "js/TracingAPI.h"
#include "js/TypeDecls.h"

namespace JS {

// Base class for GCPolicy<T> that provides some defaults.
template <typename T>
struct GCPolicyBase {
  static bool isValid(const T& tp) { return true; /* Unchecked. */ }
  static constexpr bool mightBeInNursery() { return true; /* Unknown. */ }
};

// Defines a policy for container types with non-GC, i.e. C storage. This
// policy dispatches to the underlying struct for GC interactions. Note that
// currently a type can define only the subset of the methods (trace and/or
// traceWeak) if it is never used in a context that requires the other.
template <typename T>
struct StructGCPolicy : public GCPolicyBase<T> {
  static_assert(
      !std::is_pointer_v<T>,
      "Pointer types must have a GCPolicy<> specialization.\n"
      "  - Public GC pointer types (eg JSObject*, JSString*) use "
      "GCPointerPolicy.\n"
      "  - Internal GC pointer types are handled by InternalGCPointerPolicy "
      "from gc/Policy.h; make sure you are including that file.\n"
      "  - Other pointer types (eg Realm*) must define their own "
      "specialization.\n"
      "The most likely cause of this error is attempting to root a non-GC "
      "pointer that should not be rooted. Only pointers on the stack that "
      "point to GC pointers should be or need to be rooted.");

  static void trace(JSTracer* trc, T* tp, const char* name) { tp->trace(trc); }

  static bool traceWeak(JSTracer* trc, T* tp) { return tp->traceWeak(trc); }
};

// The default GC policy attempts to defer to methods on the underlying type.
// Most C++ structures that contain a default constructor, a trace function and
// a sweep function will work out of the box with Rooted, Handle, GCVector,
// and GCHash{Set,Map}.
template <typename T>
struct GCPolicy : public StructGCPolicy<T> {};

// This policy ignores any GC interaction, e.g. for non-GC types.
template <typename T>
struct IgnoreGCPolicy : public GCPolicyBase<T> {
  static void trace(JSTracer* trc, T* t, const char* name) {}
  static bool traceWeak(JSTracer*, T* v) { return true; }
};
template <>
struct GCPolicy<uint8_t> : public IgnoreGCPolicy<uint8_t> {};
template <>
struct GCPolicy<uint32_t> : public IgnoreGCPolicy<uint32_t> {};
template <>
struct GCPolicy<uint64_t> : public IgnoreGCPolicy<uint64_t> {};
template <>
struct GCPolicy<bool> : public IgnoreGCPolicy<bool> {};

template <typename T>
struct GCPointerPolicy : public GCPolicyBase<T> {
  static_assert(std::is_pointer_v<T>,
                "Non-pointer type not allowed for GCPointerPolicy");

  static void trace(JSTracer* trc, T* vp, const char* name) {
    // This should only be called as part of root marking since that's the only
    // time we should trace unbarriered GC thing pointers. This will assert if
    // called at other times.
    TraceRoot(trc, vp, name);
  }
  static bool isTenured(T v) { return !v || !js::gc::IsInsideNursery(v); }
  static bool isValid(T v) { return js::gc::IsCellPointerValidOrNull(v); }
};
#define EXPAND_SPECIALIZE_GCPOLICY(Type)                   \
  template <>                                              \
  struct GCPolicy<Type> : public GCPointerPolicy<Type> {}; \
  template <>                                              \
  struct GCPolicy<Type const> : public GCPointerPolicy<Type const> {};
JS_FOR_EACH_PUBLIC_GC_POINTER_TYPE(EXPAND_SPECIALIZE_GCPOLICY)
#undef EXPAND_SPECIALIZE_GCPOLICY

template <typename T>
struct NonGCPointerPolicy : public GCPolicyBase<T> {
  static void trace(JSTracer* trc, T* vp, const char* name) {
    if (*vp) {
      (*vp)->trace(trc);
    }
  }
  static bool traceWeak(JSTracer* trc, T* vp) {
    return !*vp || (*vp)->traceWeak(trc);
  }
};

template <typename T>
struct GCPolicy<JS::Heap<T>> : public GCPolicyBase<JS::Heap<T>> {
  static void trace(JSTracer* trc, JS::Heap<T>* thingp, const char* name) {
    TraceEdge(trc, thingp, name);
  }
  static bool traceWeak(JSTracer* trc, JS::Heap<T>* thingp) {
    return !*thingp || js::gc::TraceWeakEdge(trc, thingp);
  }
};

// GCPolicy<UniquePtr<T>> forwards the contained pointer to GCPolicy<T>.
template <typename T, typename D>
struct GCPolicy<mozilla::UniquePtr<T, D>>
    : public GCPolicyBase<mozilla::UniquePtr<T, D>> {
  static void trace(JSTracer* trc, mozilla::UniquePtr<T, D>* tp,
                    const char* name) {
    if (tp->get()) {
      GCPolicy<T>::trace(trc, tp->get(), name);
    }
  }
  static bool traceWeak(JSTracer* trc, mozilla::UniquePtr<T, D>* tp) {
    return !tp->get() || GCPolicy<T>::traceWeak(trc, tp->get());
  }
  static bool isValid(const mozilla::UniquePtr<T, D>& t) {
    return !t.get() || GCPolicy<T>::isValid(*t.get());
  }
};

template <>
struct GCPolicy<UniqueChars> : public IgnoreGCPolicy<UniqueChars> {};

template <>
struct GCPolicy<mozilla::Nothing> : public IgnoreGCPolicy<mozilla::Nothing> {};

// GCPolicy<Maybe<T>> forwards tracing/sweeping to GCPolicy<T*> if
// the Maybe<T> is filled and T* can be traced via GCPolicy<T*>.
template <typename T>
struct GCPolicy<mozilla::Maybe<T>> : public GCPolicyBase<mozilla::Maybe<T>> {
  static void trace(JSTracer* trc, mozilla::Maybe<T>* tp, const char* name) {
    if (tp->isSome()) {
      GCPolicy<T>::trace(trc, tp->ptr(), name);
    }
  }
  static bool traceWeak(JSTracer* trc, mozilla::Maybe<T>* tp) {
    return tp->isNothing() || GCPolicy<T>::traceWeak(trc, tp->ptr());
  }
  static bool isValid(const mozilla::Maybe<T>& t) {
    return t.isNothing() || GCPolicy<T>::isValid(t.ref());
  }
};

template <typename T1, typename T2>
struct GCPolicy<std::pair<T1, T2>> : public GCPolicyBase<std::pair<T1, T2>> {
  static void trace(JSTracer* trc, std::pair<T1, T2>* tp, const char* name) {
    GCPolicy<T1>::trace(trc, &tp->first, name);
    GCPolicy<T2>::trace(trc, &tp->second, name);
  }
  static bool traceWeak(JSTracer* trc, std::pair<T1, T2>* tp) {
    return GCPolicy<T1>::traceWeak(trc, &tp->first) &&
           GCPolicy<T2>::traceWeak(trc, &tp->second);
  }
  static bool isValid(const std::pair<T1, T2>& t) {
    return GCPolicy<T1>::isValid(t.first) && GCPolicy<T2>::isValid(t.second);
  }
};

template <>
struct GCPolicy<JS::Realm*>;  // see Realm.h

template <>
struct GCPolicy<mozilla::Ok> : public IgnoreGCPolicy<mozilla::Ok> {};

template <typename V, typename E>
struct GCPolicy<mozilla::Result<V, E>>
    : public GCPolicyBase<mozilla::Result<V, E>> {
  static void trace(JSTracer* trc, mozilla::Result<V, E>* tp,
                    const char* name) {
    if (tp->isOk()) {
      V tmp = tp->unwrap();
      JS::GCPolicy<V>::trace(trc, &tmp, "Result value");
      tp->updateAfterTracing(std::move(tmp));
    }

    if (tp->isErr()) {
      E tmp = tp->unwrapErr();
      JS::GCPolicy<E>::trace(trc, &tmp, "Result error");
      tp->updateErrorAfterTracing(std::move(tmp));
    }
  }

  static bool isValid(const mozilla::Result<V, E>& t) { return true; }
};

template <typename... Fs>
struct GCPolicy<std::tuple<Fs...>> : public GCPolicyBase<std::tuple<Fs...>> {
  using T = std::tuple<Fs...>;
  static void trace(JSTracer* trc, T* tp, const char* name) {
    traceFieldsFrom<0>(trc, *tp, name);
  }
  static bool isValid(const T& t) { return areFieldsValidFrom<0>(t); }

 private:
  template <size_t N>
  static void traceFieldsFrom(JSTracer* trc, T& tuple, const char* name) {
    if constexpr (N != std::tuple_size_v<T>) {
      using F = std::tuple_element_t<N, T>;
      GCPolicy<F>::trace(trc, &std::get<N>(tuple), name);
      traceFieldsFrom<N + 1>(trc, tuple, name);
    }
  }

  template <size_t N>
  static bool areFieldsValidFrom(const T& tuple) {
    if constexpr (N != std::tuple_size_v<T>) {
      using F = std::tuple_element_t<N, T>;
      return GCPolicy<F>::isValid(std::get<N>(tuple)) &&
             areFieldsValidFrom<N + 1>(tuple);
    }

    return true;
  }
};

}  // namespace JS

#endif  // GCPolicyAPI_h
