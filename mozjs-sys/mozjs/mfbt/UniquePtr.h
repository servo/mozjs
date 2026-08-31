/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Smart pointer managing sole ownership of a resource. */

#ifndef mozilla_UniquePtr_h
#define mozilla_UniquePtr_h

#include <memory>
#include <utility>

#include "mozilla/Attributes.h"

namespace mozilla {

template <typename T>
using DefaultDelete = std::default_delete<T>;

template <typename T, class D = DefaultDelete<T>>
using UniquePtr = std::unique_ptr<T, D>;

}  // namespace mozilla

namespace mozilla {

namespace detail {

template <typename T>
struct UniqueSelector {
  typedef UniquePtr<T> SingleObject;
};

template <typename T>
struct UniqueSelector<T[]> {
  typedef UniquePtr<T[]> UnknownBound;
};

template <typename T, decltype(sizeof(int)) N>
struct UniqueSelector<T[N]> {
  typedef UniquePtr<T[N]> KnownBound;
};

}  // namespace detail

/**
 * MakeUnique is a helper function for allocating new'd objects and arrays,
 * returning a UniquePtr containing the resulting pointer.  The semantics of
 * MakeUnique<Type>(...) are as follows.
 *
 *   If Type is an array T[n]:
 *     Disallowed, deleted, no overload for you!
 *   If Type is an array T[]:
 *     MakeUnique<T[]>(size_t) is the only valid overload.  The pointer returned
 *     is as if by |new T[n]()|, which value-initializes each element.  (If T
 *     isn't a class type, this will zero each element.  If T is a class type,
 *     then roughly speaking, each element will be constructed using its default
 *     constructor.  See C++11 [dcl.init]p7 for the full gory details.)
 *   If Type is non-array T:
 *     The arguments passed to MakeUnique<T>(...) are forwarded into a
 *     |new T(...)| call, initializing the T as would happen if executing
 *     |T(...)|.
 *
 * There are various benefits to using MakeUnique instead of |new| expressions.
 *
 * First, MakeUnique eliminates use of |new| from code entirely.  If objects are
 * only created through UniquePtr, then (assuming all explicit release() calls
 * are safe, including transitively, and no type-safety casting funniness)
 * correctly maintained ownership of the UniquePtr guarantees no leaks are
 * possible.  (This pays off best if a class is only ever created through a
 * factory method on the class, using a private constructor.)
 *
 * Second, initializing a UniquePtr using a |new| expression requires repeating
 * the name of the new'd type, whereas MakeUnique in concert with the |auto|
 * keyword names it only once:
 *
 *   UniquePtr<char> ptr1(new char()); // repetitive
 *   auto ptr2 = MakeUnique<char>();   // shorter
 *
 * Of course this assumes the reader understands the operation MakeUnique
 * performs.  In the long run this is probably a reasonable assumption.  In the
 * short run you'll have to use your judgment about what readers can be expected
 * to know, or to quickly look up.
 *
 * Third, a call to MakeUnique can be assigned directly to a UniquePtr.  In
 * contrast you can't assign a pointer into a UniquePtr without using the
 * cumbersome reset().
 *
 *   UniquePtr<char> p;
 *   p = new char;           // ERROR
 *   p.reset(new char);      // works, but fugly
 *   p = MakeUnique<char>(); // preferred
 *
 * (And third, although not relevant to Mozilla: MakeUnique is exception-safe.
 * An exception thrown after |new T| succeeds will leak that memory, unless the
 * pointer is assigned to an object that will manage its ownership.  UniquePtr
 * ably serves this function.)
 */

template <typename T, typename... Args>
auto MakeUnique(Args&&... aArgs) {
  return std::make_unique<T>(std::forward<Args>(aArgs)...);
}

/**
 * WrapUnique is a helper function to transfer ownership from a raw pointer
 * into a UniquePtr<T>. It can only be used with a single non-array type.
 *
 * It is generally used this way:
 *
 *   auto p = WrapUnique(new char);
 *
 * It can be used when MakeUnique is not usable, for example, when the
 * constructor you are using is private, or you want to use aggregate
 * initialization.
 */

template <typename T>
typename detail::UniqueSelector<T>::SingleObject WrapUnique(T* aPtr) {
  return UniquePtr<T>(aPtr);
}

}  // namespace mozilla

/**
TempPtrToSetter(UniquePtr<T>*) -> T**-ish
TempPtrToSetter(std::unique_ptr<T>*) -> T**-ish

Make a temporary class to support assigning to UniquePtr/unique_ptr via passing
a pointer to the callee.

Often, APIs will be shaped like this trivial example:
```
nsresult Foo::NewChildBar(Bar** out) {
  if (!IsOk()) return NS_ERROR_FAILURE;
  *out = new Bar(this);
  return NS_OK;
}
```

In order to make this work with unique ptrs, it's often either risky or
overwrought:
```
Bar* bar = nullptr;
const auto cleanup = MakeScopeExit([&]() {
  if (bar) {
    delete bar;
  }
});
if (FAILED(foo->NewChildBar(&bar)) {
  // handle it
}
```

```
UniquePtr<Bar> bar;
{
  Bar* raw = nullptr;
  const auto res = foo->NewChildBar(&bar);
  bar.reset(raw);
  if (FAILED(res) {
    // handle it
  }
}
```
TempPtrToSettable is a shorthand for the latter approach, allowing something
cleaner but also safe:

```
UniquePtr<Bar> bar;
if (FAILED(foo->NewChildBar(TempPtrToSetter(&bar))) {
  // handle it
}
```
*/

namespace mozilla {
namespace detail {

template <class T, class UniquePtrT>
class MOZ_TEMPORARY_CLASS TempPtrToSetterT final {
 private:
  UniquePtrT* const mDest;
  T* mNewVal;

 public:
  explicit TempPtrToSetterT(UniquePtrT* dest)
      : mDest(dest), mNewVal(mDest->get()) {}

  operator T**() { return &mNewVal; }

  ~TempPtrToSetterT() {
    if (mDest->get() != mNewVal) {
      mDest->reset(mNewVal);
    }
  }
};

}  // namespace detail

template <class T, class Deleter>
auto TempPtrToSetter(UniquePtr<T, Deleter>* const p) {
  return detail::TempPtrToSetterT<T, UniquePtr<T, Deleter>>{p};
}

}  // namespace mozilla

namespace std {

// No operator<, operator>, operator<=, operator>= for now because simplicity.

template <typename T, class D>
bool operator==(const mozilla::UniquePtr<T, D>& aX, const T* aY) {
  return aX.get() == aY;
}

template <typename T, class D>
bool operator==(const T* aY, const mozilla::UniquePtr<T, D>& aX) {
  return aY == aX.get();
}

template <typename T, class D>
bool operator!=(const mozilla::UniquePtr<T, D>& aX, const T* aY) {
  return aX.get() != aY;
}

template <typename T, class D>
bool operator!=(const T* aY, const mozilla::UniquePtr<T, D>& aX) {
  return aY != aX.get();
}
}  // namespace std

#endif /* mozilla_UniquePtr_h */
