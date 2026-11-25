/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_CommonFunctions_h
#define builtin_intl_CommonFunctions_h

#include <stddef.h>
#include <stdint.h>

#include "js/GCVector.h"
#include "js/ProtoKey.h"
#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/Utility.h"
#include "util/LanguageId.h"

namespace mozilla::intl {
enum class ICUError : uint8_t;
}

namespace JS {
class CallArgs;
}

namespace js {
class ArrayObject;
}

namespace js::intl {

/**
 * ChainDateTimeFormat ( dateTimeFormat, newTarget, this )
 * ChainNumberFormat ( numberFormat, newTarget, this )
 */
extern bool ChainLegacyIntlFormat(JSContext* cx, JSProtoKey protoKey,
                                  const JS::CallArgs& args,
                                  JS::Handle<JSObject*> format);

/**
 * UnwrapDateTimeFormat ( dtf )
 * UnwrapNumberFormat ( nf )
 */
extern bool UnwrapLegacyIntlFormat(JSContext* cx, JSProtoKey protoKey,
                                   JS::Handle<JSObject*> format,
                                   JS::MutableHandle<JS::Value> result);

/** Report an Intl internal error not directly tied to a spec step. */
extern void ReportInternalError(JSContext* cx);

/** Report an Intl internal error not directly tied to a spec step. */
extern void ReportInternalError(JSContext* cx, mozilla::intl::ICUError error);

/**
 * The last-ditch locale is used if none of the available locales satisfies a
 * request. "en-GB" is used based on the assumptions that English is the most
 * common second language, that both en-GB and en-US are normally available in
 * an implementation, and that en-GB is more representative of the English used
 * in other locales.
 */
static constexpr LanguageId LastDitchLocale() {
  return LanguageId::fromValidBcp49("en-GB");
}

extern JS::UniqueChars EncodeLocale(JSContext* cx, JSString* locale);

// The inline capacity we use for a Vector<char16_t>.  Use this to ensure that
// our uses of ICU string functions, below and elsewhere, will try to fill the
// buffer's entire inline capacity before growing it and heap-allocating.
constexpr size_t INITIAL_CHAR_BUFFER_SIZE = 32;

using StringList = JS::GCVector<JSLinearString*>;

/**
 * Create a sorted array from a list of strings.
 */
ArrayObject* CreateSortedArrayFromList(JSContext* cx,
                                       JS::MutableHandle<StringList> list);

void AddICUCellMemory(JSObject* obj, size_t nbytes);

void RemoveICUCellMemory(JSObject* obj, size_t nbytes);

void RemoveICUCellMemory(JS::GCContext* gcx, JSObject* obj, size_t nbytes);

}  // namespace js::intl

#endif /* builtin_intl_CommonFunctions_h */
