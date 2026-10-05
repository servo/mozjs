/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_IntlMathematicalValue_h
#define builtin_intl_IntlMathematicalValue_h

#include <stddef.h>
#include <string_view>

#include "js/GCAPI.h"
#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/Utility.h"
#include "js/Value.h"

class JSLinearString;

namespace js::intl {

class IntlMathematicalValueString;
class IntlMathematicalValueStringView;

/**
 * An Intl mathematical value is either a numeric value or a string which can
 * be parsed as `StringNumericLiteral`.
 */
class IntlMathematicalValue final {
  JS::Value value_{};

  JSLinearString* toLinearString(JSContext* cx) const;

 public:
  IntlMathematicalValue() = default;

  explicit IntlMathematicalValue(JS::Value value) : value_(value) {
    MOZ_ASSERT(value.isNumeric() || value.isString());
  }

  explicit IntlMathematicalValue(JS::BigInt* bigInt)
      : value_(JS::BigIntValue(bigInt)) {}

  bool isNumber() const { return value_.isNumber(); }
  bool isBigInt() const { return value_.isBigInt(); }
  bool isNaN() const { return value_.isNaN(); }

  double toNumber() const { return value_.toNumber(); }
  JS::BigInt* toBigInt() const { return value_.toBigInt(); }

  bool isRepresentableAsDouble(double* result) const;

  [[nodiscard]] IntlMathematicalValueString toString(JSContext* cx) const;

  void trace(JSTracer* trc);
};

/**
 * Wrapper around a string produced from `IntlMathematicalValue`.
 */
class IntlMathematicalValueString final {
  JSLinearString* string_ = nullptr;

  explicit IntlMathematicalValueString(JSLinearString* string)
      : string_(string) {}

  friend class IntlMathematicalValue;

 public:
  IntlMathematicalValueString() = default;

  explicit operator bool() const { return !!string_; }

  [[nodiscard]] IntlMathematicalValueStringView asView(
      JSContext* cx, const JS::AutoCheckCannotGC& nogc) const;

  void trace(JSTracer* trc);
};

/**
 * View to the string characters of `IntlMathematicalValueString`.
 */
class IntlMathematicalValueStringView final {
  std::string_view view_{};

  // Two-byte strings have to be copied into a separate buffer.
  JS::UniqueChars latin1_{};

 public:
  IntlMathematicalValueStringView() = default;

  explicit IntlMathematicalValueStringView(std::string_view view,
                                           JS::UniqueChars latin1 = nullptr)
      : view_(view), latin1_(std::move(latin1)) {}

  explicit operator bool() const { return !view_.empty(); }

  operator std::string_view() const { return view_; }
};

inline IntlMathematicalValueString IntlMathematicalValue::toString(
    JSContext* cx) const {
  return IntlMathematicalValueString{toLinearString(cx)};
}

/**
 * 15.5.16 ToIntlMathematicalValue ( value )
 *
 * ES2024 Intl draft rev 74ca7099f103d143431b2ea422ae640c6f43e3e6
 */
bool ToIntlMathematicalValue(JSContext* cx, JS::Handle<JS::Value> value,
                             JS::MutableHandle<IntlMathematicalValue> result);
}  // namespace js::intl

namespace js {

template <typename Wrapper>
class WrappedPtrOperations<intl::IntlMathematicalValue, Wrapper> {
  const intl::IntlMathematicalValue& container() const {
    return static_cast<const Wrapper*>(this)->get();
  }

 public:
  bool isNumber() const { return container().isNumber(); }
  bool isBigInt() const { return container().isBigInt(); }
  bool isNaN() const { return container().isNaN(); }

  double toNumber() const { return container().toNumber(); }
  JS::BigInt* toBigInt() const { return container().toBigInt(); }

  bool isRepresentableAsDouble(double* result) const {
    return container().isRepresentableAsDouble(result);
  }

  [[nodiscard]] auto toString(JSContext* cx) const {
    return container().toString(cx);
  }
};

template <typename Wrapper>
class WrappedPtrOperations<intl::IntlMathematicalValueString, Wrapper> {
  const intl::IntlMathematicalValueString& container() const {
    return static_cast<const Wrapper*>(this)->get();
  }

 public:
  explicit operator bool() const { return bool(container()); }

  auto asView(JSContext* cx, const JS::AutoCheckCannotGC& nogc) const {
    return container().asView(cx, nogc);
  }
};

}  // namespace js

#endif /* builtin_intl_IntlMathematicalValue_h */
