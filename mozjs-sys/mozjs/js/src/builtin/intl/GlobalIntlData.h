/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_GlobalIntlData_h
#define builtin_intl_GlobalIntlData_h

#include "gc/Barrier.h"
#include "js/TypeDecls.h"
#include "util/LanguageId.h"
#include "vm/JSObject.h"
#include "vm/StringType.h"
#include "vm/SymbolType.h"

class JS_PUBLIC_API JSTracer;

namespace js::temporal {
class TimeZoneObject;
}

namespace js::intl {

enum class DateTimeFormatKind;

class CollatorObject;
class DateTimeFormatObject;
class NumberFormatObject;

/**
 * Cached per-global Intl data. In contrast to SharedIntlData, which is
 * a per-runtime shared Intl cache, this cache is per-global.
 */
class GlobalIntlData {
  /**
   * The locale information provided by the embedding, guiding SpiderMonkey's
   * selection of a default locale. See intl::ComputeDefaultLocale(), whose
   * value controls the value returned by defaultLocale() that's what's
   * *actually* used.
   */
  LanguageId realmLocale_ = LanguageId::und();

  /**
   * The actual default locale.
   */
  LanguageId defaultLocale_ = LanguageId::und();

  /**
   * Time zone information provided by ICU or the embedding. See
   * temporal::ComputeSystemTimeZoneIdentifier(), whose value controls the value
   * returned by defaultTimeZone() that's what's *actually* used.
   */
  GCPtr<JSLinearString*> realmTimeZone_;

  /**
   * The actual default time zone.
   */
  GCPtr<JSLinearString*> defaultTimeZone_;

  /**
   * Cached temporal::TimeZoneObject for the default time zone.
   */
  GCPtr<JSObject*> defaultTimeZoneObject_;

  /**
   * Cached temporal::TimeZoneObject of the last request to create a named
   * time zone.
   */
  GCPtr<JSObject*> timeZoneObject_;

  /**
   * Locale string passed to the last call to localeCompare String method. Not
   * necessarily the actual locale when the string can't be resolved to a
   * supported Collator locale.
   */
  GCPtr<JSLinearString*> collatorLocale_;

  /**
   * Cached Intl.Collator when String.prototype.localeCompare is called with
   * |locales| either a |undefined| or a string, and |options| having the value
   * |undefined|.
   */
  GCPtr<JSObject*> collator_;

  /**
   * Locale string passed to the last call to toLocaleString Number method. Not
   * necessarily the actual locale when the string can't be resolved to a
   * supported NumberFormat locale.
   */
  GCPtr<JSLinearString*> numberFormatLocale_;

  /**
   * Cached Intl.NumberFormat when Number.prototype.toLocaleString is called
   * with |locales| either a |undefined| or a string, and |options| having the
   * value |undefined|.
   */
  GCPtr<JSObject*> numberFormat_;

  /**
   * Locale string passed to the last call to toLocale*String Date method. Not
   * necessarily the actual locale when the string can't be resolved to a
   * supported DateTimeFormat locale.
   */
  GCPtr<JSLinearString*> dateTimeFormatLocale_;

  /**
   * Cached Intl.DateTimeFormat when Date.prototype.toLocaleString is called
   * with |locales| either a |undefined| or a string, and |options| having the
   * value |undefined|.
   */
  GCPtr<JSObject*> dateTimeFormatToLocaleAll_;

  /**
   * Cached Intl.DateTimeFormat when Date.prototype.toLocaleDateString is called
   * with |locales| either a |undefined| or a string, and |options| having the
   * value |undefined|.
   */
  GCPtr<JSObject*> dateTimeFormatToLocaleDate_;

  /**
   * Cached Intl.DateTimeFormat when Date.prototype.toLocaleTimeString is called
   * with |locales| either a |undefined| or a string, and |options| having the
   * value |undefined|.
   */
  GCPtr<JSObject*> dateTimeFormatToLocaleTime_;

  /**
   * The [[FallbackSymbol]] symbol of the %Intl% intrinsic object.
   *
   * This symbol is used to implement the legacy constructor semantics for
   * Intl.DateTimeFormat and Intl.NumberFormat.
   */
  GCPtr<JS::Symbol*> fallbackSymbol_;

 public:
  /**
   * Returns the BCP 47 language tag for the global's current locale.
   */
  bool defaultLocale(JSContext* cx, LanguageId* result);

  /**
   * Returns the IANA time zone name for the global's current time zone.
   */
  JSLinearString* defaultTimeZone(JSContext* cx);

  /**
   * Get or create the time zone object for the global's current time zone.
   */
  temporal::TimeZoneObject* getOrCreateDefaultTimeZone(JSContext* cx);

  /**
   * Get or create the time zone for the IANA time zone name |identifier|.
   * |primaryIdentifier| must be the primary identifier for |identifier|, i.e.
   * if |identifier| is a time zone link name, |primaryIdentifier| must be the
   * link's target time zone.
   */
  temporal::TimeZoneObject* getOrCreateTimeZone(
      JSContext* cx, JS::Handle<JSLinearString*> identifier,
      JS::Handle<JSLinearString*> primaryIdentifier);

  /**
   * Get or create the Intl.Collator instance for |locale|. The default locale
   * is used when |locale| is null.
   */
  CollatorObject* getOrCreateCollator(JSContext* cx,
                                      JS::Handle<JSLinearString*> locale);

  /**
   * Get or create the Intl.NumberFormat instance for |locale|. The default
   * locale is used when |locale| is null.
   */
  NumberFormatObject* getOrCreateNumberFormat(
      JSContext* cx, JS::Handle<JSLinearString*> locale);

  /**
   * Get or create the Intl.DateTimeFormat instance for |locale|. The default
   * locale is used when |locale| is null.
   */
  DateTimeFormatObject* getOrCreateDateTimeFormat(
      JSContext* cx, DateTimeFormatKind kind,
      JS::Handle<JSLinearString*> locale);

  /**
   * Returns the %Intl%.[[FallbackSymbol]] for legacy constructor semantics of
   * Intl.DateTimeFormat and Intl.NumberFormat.
   */
  JS::Symbol* fallbackSymbol(JSContext* cx);

  void trace(JSTracer* trc);

 private:
  bool ensureRealmLocale(JSContext* cx);
  bool ensureRealmTimeZone(JSContext* cx);

  void resetCollator();
  void resetNumberFormat();
  void resetDateTimeFormat();
};

}  // namespace js::intl

#endif /* builtin_intl_GlobalIntlData_h */
