/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Operations used to implement multiple Intl.* classes. */

#include "builtin/intl/CommonFunctions.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/ICUError.h"
#include "mozilla/TextUtils.h"

#include <algorithm>

#include "builtin/Array.h"
#include "ds/Sort.h"
#include "gc/GCEnum.h"
#include "gc/ZoneAllocator.h"
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_INTERNAL_INTL_ERROR
#include "js/Value.h"
#include "vm/GlobalObject.h"
#include "vm/JSAtomState.h"
#include "vm/JSContext.h"
#include "vm/JSObject.h"
#include "vm/SelfHosting.h"
#include "vm/Stack.h"
#include "vm/StringType.h"

#include "gc/GCContext-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/ObjectOperations-inl.h"

/**
 * ChainDateTimeFormat ( dateTimeFormat, newTarget, this )
 * ChainNumberFormat ( numberFormat, newTarget, this )
 */
bool js::intl::ChainLegacyIntlFormat(JSContext* cx, JSProtoKey protoKey,
                                     const JS::CallArgs& args,
                                     JS::Handle<JSObject*> format) {
  // Step 1.
  if (!args.isConstructing() && args.thisv().isObject()) {
    Rooted<JSObject*> thisValue(cx, &args.thisv().toObject());

    Rooted<JSObject*> proto(cx,
                            GlobalObject::getOrCreatePrototype(cx, protoKey));
    if (!proto) {
      return false;
    }

    bool isPrototype;
    if (!IsPrototypeOf(cx, proto, thisValue, &isPrototype)) {
      return false;
    }

    if (isPrototype) {
      auto* fallback = cx->global()->globalIntlData().fallbackSymbol(cx);
      if (!fallback) {
        return false;
      }

      // Step 1.a.
      Rooted<PropertyKey> id(cx, JS::PropertyKey::Symbol(fallback));
      Rooted<Value> value(cx, ObjectValue(*format));
      if (!DefineDataProperty(cx, thisValue, id, value,
                              JSPROP_READONLY | JSPROP_PERMANENT)) {
        return false;
      }

      // Step 1.b.
      args.rval().set(args.thisv());
      return true;
    }
  }

  // Step 2.
  args.rval().setObject(*format);
  return true;
}

/**
 * UnwrapDateTimeFormat ( dtf )
 * UnwrapNumberFormat ( nf )
 */
bool js::intl::UnwrapLegacyIntlFormat(JSContext* cx, JSProtoKey protoKey,
                                      JS::Handle<JSObject*> format,
                                      JS::MutableHandle<JS::Value> result) {
  // Step 1. (Performed in caller)

  // Step 2. (Partial)
  Rooted<JSObject*> proto(cx, GlobalObject::getOrCreatePrototype(cx, protoKey));
  if (!proto) {
    return false;
  }

  bool isPrototype;
  if (!IsPrototypeOf(cx, proto, format, &isPrototype)) {
    return false;
  }

  if (isPrototype) {
    auto* fallback = cx->global()->globalIntlData().fallbackSymbol(cx);
    if (!fallback) {
      return false;
    }

    Rooted<PropertyKey> id(cx, JS::PropertyKey::Symbol(fallback));
    return GetProperty(cx, format, format, id, result);
  }

  // Step 3.
  result.setObject(*format);
  return true;
}

void js::intl::ReportInternalError(JSContext* cx) {
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_INTERNAL_INTL_ERROR);
}

void js::intl::ReportInternalError(JSContext* cx,
                                   mozilla::intl::ICUError error) {
  switch (error) {
    case mozilla::intl::ICUError::OutOfMemory:
      ReportOutOfMemory(cx);
      return;
    case mozilla::intl::ICUError::InternalError:
      ReportInternalError(cx);
      return;
    case mozilla::intl::ICUError::OverflowError:
      ReportAllocationOverflow(cx);
      return;
  }
  MOZ_CRASH("Unexpected ICU error");
}

js::UniqueChars js::intl::EncodeLocale(JSContext* cx, JSString* locale) {
  MOZ_ASSERT(locale->length() > 0);

  js::UniqueChars chars = EncodeAscii(cx, locale);
  if (!chars) {
    return nullptr;
  }

  // Ensure the returned value contains only valid BCP 47 characters.
  MOZ_ASSERT(mozilla::IsAsciiAlpha(chars[0]));
  MOZ_ASSERT(std::all_of(
      chars.get(), chars.get() + locale->length(),
      [](char c) { return mozilla::IsAsciiAlphanumeric(c) || c == '-'; }));

  return chars;
}

js::ArrayObject* js::intl::CreateSortedArrayFromList(
    JSContext* cx, JS::MutableHandle<StringList> list) {
  // Reserve scratch space for MergeSort().
  size_t initialLength = list.length();
  if (!list.growBy(initialLength)) {
    return nullptr;
  }

  // Sort all strings in alphabetical order.
  MOZ_ALWAYS_TRUE(
      MergeSort(list.begin(), initialLength, list.begin() + initialLength,
                [](const auto* a, const auto* b, bool* lessOrEqual) {
                  *lessOrEqual = js::CompareStrings(a, b) <= 0;
                  return true;
                }));

  // Ensure we don't add duplicate entries to the array.
  auto* end = std::unique(
      list.begin(), list.begin() + initialLength,
      [](const auto* a, const auto* b) { return EqualStrings(a, b); });

  // std::unique leaves the elements after |end| with an unspecified value, so
  // remove them first. And also delete the elements in the scratch space.
  list.shrinkBy(std::distance(end, list.end()));

  // And finally copy the strings into the result array.
  auto* array = NewDenseFullyAllocatedArray(cx, list.length());
  if (!array) {
    return nullptr;
  }
  array->setDenseInitializedLength(list.length());

  for (size_t i = 0; i < list.length(); ++i) {
    array->initDenseElement(i, StringValue(list[i]));
  }

  return array;
}

void js::intl::AddICUCellMemory(JSObject* obj, size_t nbytes) {
  // Account the (estimated) number of bytes allocated by an ICU object against
  // the JSObject's zone.
  AddCellMemory(obj, nbytes, MemoryUse::ICUObject);
}

void js::intl::RemoveICUCellMemory(JSObject* obj, size_t nbytes) {
  RemoveCellMemory(obj, nbytes, MemoryUse::ICUObject);
}

void js::intl::RemoveICUCellMemory(JS::GCContext* gcx, JSObject* obj,
                                   size_t nbytes) {
  gcx->removeCellMemory(obj, nbytes, MemoryUse::ICUObject);
}
