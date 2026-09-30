/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Intl.Locale implementation. */

#include "builtin/intl/Locale.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/intl/Calendar.h"
#include "mozilla/intl/Collator.h"
#include "mozilla/intl/DateTimeFormat.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/intl/Region.h"
#include "mozilla/intl/TimeZone.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/TextUtils.h"
#include "mozilla/UsingEnum.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "builtin/Array.h"
#include "builtin/Boolean.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/glue/Locale.h"
#include "builtin/intl/LanguageTag.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "builtin/intl/StringAsciiChars.h"
#include "builtin/String.h"
#include "js/Conversions.h"
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/Prefs.h"
#include "js/Printer.h"
#include "js/TypeDecls.h"
#include "js/Wrapper.h"
#include "vm/Compartment.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"  // js::PlainObject
#include "vm/StringType.h"

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::intl;
using namespace mozilla::intl::LanguageTagLimits;

const JSClass LocaleObject::class_ = {
    "Intl.Locale",
    JSCLASS_HAS_RESERVED_SLOTS(LocaleObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_Locale),
    JS_NULL_CLASS_OPS,
    &LocaleObject::classSpec_,
};

const JSClass LocaleObject::protoClass_ = {
    "Intl.Locale.prototype",
    JSCLASS_HAS_CACHED_PROTO(JSProto_Locale),
    JS_NULL_CLASS_OPS,
    &LocaleObject::classSpec_,
};

static inline bool IsLocale(Handle<JS::Value> v) {
  return v.isObject() && v.toObject().is<LocaleObject>();
}

// Return the length of the base-name subtags.
static size_t BaseNameLength(const mozilla::intl::Locale& tag) {
  size_t baseNameLength = tag.Language().Length();
  if (tag.Script().Present()) {
    baseNameLength += 1 + tag.Script().Length();
  }
  if (tag.Region().Present()) {
    baseNameLength += 1 + tag.Region().Length();
  }
  for (const auto& variant : tag.Variants()) {
    baseNameLength += 1 + variant.size();
  }
  return baseNameLength;
}

struct IndexAndLength {
  size_t index = 0;
  size_t length = 0;
};

// Compute the Unicode extension's index and length in the extension subtag.
static mozilla::Maybe<IndexAndLength> UnicodeExtensionPosition(
    const mozilla::intl::Locale& tag) {
  size_t index = 0;
  for (const auto& extension : tag.Extensions()) {
    MOZ_ASSERT(!mozilla::IsAsciiUppercaseAlpha(extension[0]),
               "extensions are case normalized to lowercase");

    size_t extensionLength = extension.size();
    if (extension[0] == 'u') {
      return mozilla::Some(IndexAndLength{index, extensionLength});
    }

    // Add +1 to skip over the preceding separator.
    index += 1 + extensionLength;
  }
  return mozilla::Nothing();
}

static LocaleObject* CreateLocaleObject(JSContext* cx,
                                        Handle<JSObject*> prototype,
                                        const mozilla::intl::Locale& tag) {
  FormatBuffer<char, INITIAL_CHAR_BUFFER_SIZE> buffer(cx);
  if (auto result = tag.ToString(buffer); result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }

  Rooted<JSLinearString*> tagStr(cx, buffer.toAsciiString(cx));
  if (!tagStr) {
    return nullptr;
  }

  size_t baseNameLength = BaseNameLength(tag);

  Rooted<JSLinearString*> baseName(
      cx, NewDependentString(cx, tagStr, 0, baseNameLength));
  if (!baseName) {
    return nullptr;
  }

  Rooted<JSLinearString*> unicodeExtension(cx);
  if (auto result = UnicodeExtensionPosition(tag)) {
    unicodeExtension = NewDependentString(
        cx, tagStr, baseNameLength + 1 + result->index, result->length);
    if (!unicodeExtension) {
      return nullptr;
    }
  }

  auto* locale = NewObjectWithClassProto<LocaleObject>(cx, prototype);
  if (!locale) {
    return nullptr;
  }

  // Initialize all fixed slots.
  locale->initialize(tagStr, baseName, unicodeExtension);

  return locale;
}

/**
 * Iterate through (sep keyword) in a valid Unicode extension.
 *
 * The Unicode extension value is not required to be in canonical case.
 */
template <typename CharT>
class SepKeywordIterator {
  const CharT* iter_;
  const CharT* const end_;

 public:
  SepKeywordIterator(const CharT* unicodeExtensionBegin,
                     const CharT* unicodeExtensionEnd)
      : iter_(unicodeExtensionBegin), end_(unicodeExtensionEnd) {}

  /**
   * Return (sep keyword) in the Unicode locale extension from begin to end.
   * The first call after all (sep keyword) are consumed returns |nullptr|; no
   * further calls are allowed.
   */
  const CharT* next() {
    MOZ_ASSERT(iter_ != nullptr,
               "can't call next() once it's returned nullptr");

    constexpr size_t SepKeyLength = 1 + UnicodeKeyLength;  // "-co"/"-nu"/etc.

    MOZ_ASSERT(iter_ + SepKeyLength <= end_,
               "overall Unicode locale extension or non-leading subtags must "
               "be at least key-sized");

    MOZ_ASSERT(((iter_[0] == 'u' || iter_[0] == 'U') && iter_[1] == '-') ||
               iter_[0] == '-');

    while (true) {
      // Skip past '-' so |std::char_traits::find| makes progress. Skipping
      // 'u' is harmless -- skip or not, |find| returns the first '-'.
      iter_++;

      // Find the next separator.
      iter_ = std::char_traits<CharT>::find(
          iter_, mozilla::PointerRangeSize(iter_, end_), CharT('-'));
      if (!iter_) {
        return nullptr;
      }

      MOZ_ASSERT(iter_ + SepKeyLength <= end_,
                 "non-leading subtags in a Unicode locale extension are all "
                 "at least as long as a key");

      if (iter_ + SepKeyLength == end_ ||  // key is terminal subtag
          iter_[SepKeyLength] == '-') {    // key is followed by more subtags
        break;
      }
    }

    MOZ_ASSERT(iter_[0] == '-');
    MOZ_ASSERT(mozilla::IsAsciiAlphanumeric(iter_[1]));
    MOZ_ASSERT(mozilla::IsAsciiAlpha(iter_[2]));
    MOZ_ASSERT_IF(iter_ + SepKeyLength < end_, iter_[SepKeyLength] == '-');
    return iter_;
  }
};

enum class LocaleHourCycle { H11, H12, H23, H24 };

static constexpr std::string_view LocaleHourCycleToString(
    LocaleHourCycle hourCycle) {
  MOZ_USING_ENUM(LocaleHourCycle, H11, H12, H23, H24);
  switch (hourCycle) {
    case H11:
      return "h11";
    case H12:
      return "h12";
    case H23:
      return "h23";
    case H24:
      return "h24";
  }
  MOZ_CRASH("invalid locale hour cycle");
}

static JSLinearString* ToUnicodeValue(JSContext* cx,
                                      LocaleHourCycle hourCycle) {
  MOZ_USING_ENUM(LocaleHourCycle, H11, H12, H23, H24);
  switch (hourCycle) {
    case H11:
      return cx->names().h11;
    case H12:
      return cx->names().h12;
    case H23:
      return cx->names().h23;
    case H24:
      return cx->names().h24;
  }
  MOZ_CRASH("invalid locale hour cycle");
}

enum class LocaleCaseFirst { Upper, Lower, False };

static constexpr std::string_view LocaleCaseFirstToString(
    LocaleCaseFirst caseFirst) {
  MOZ_USING_ENUM(LocaleCaseFirst, False, Lower, Upper);
  switch (caseFirst) {
    case False:
      return "false";
    case Lower:
      return "lower";
    case Upper:
      return "upper";
  }
  MOZ_CRASH("invalid locale case first");
}

static JSLinearString* ToUnicodeValue(JSContext* cx,
                                      LocaleCaseFirst caseFirst) {
  MOZ_USING_ENUM(LocaleCaseFirst, False, Lower, Upper);
  switch (caseFirst) {
    case False:
      return cx->names().false_;
    case Lower:
      return cx->names().lower;
    case Upper:
      return cx->names().upper;
  }
  MOZ_CRASH("invalid locale case first");
}

/**
 * WeekdayToUValue ( fw )
 */
static JSLinearString* WeekdayToUValue(JSContext* cx, JSLinearString* fw) {
  static constexpr std::array weekdays = {
      "sun", "mon", "tue", "wed", "thu", "fri", "sat", "sun",
  };

  if (fw->length() == 1) {
    char16_t ch = fw->latin1OrTwoByteChar(0);
    if ('0' <= ch && ch <= '7') {
      size_t index = ch - '0';
      return NewStringCopyN<CanGC>(cx, weekdays[index], 3);
    }
  }
  return fw;
}

/**
 * ApplyOptionsToTag ( tag, options )
 */
static bool ApplyOptionsToTag(JSContext* cx, mozilla::intl::Locale& tag,
                              Handle<JSObject*> options) {
  // Step 1. (Not applicable in our implementation.)

  Rooted<JSLinearString*> option(cx);

  // Step 2.
  if (!GetStringOption(cx, options, cx->names().language, &option)) {
    return false;
  }

  // Step 3.
  mozilla::intl::LanguageSubtag language;
  if (option && !ParseStandaloneLanguageTag(option, language)) {
    if (UniqueChars str = QuoteString(cx, option, '"')) {
      JS_ReportErrorNumberASCII(cx, js::GetErrorMessage, nullptr,
                                JSMSG_INVALID_OPTION_VALUE, "language",
                                str.get());
    }
    return false;
  }

  // Step 4.
  if (!GetStringOption(cx, options, cx->names().script, &option)) {
    return false;
  }

  // Step 5.
  mozilla::intl::ScriptSubtag script;
  if (option && !ParseStandaloneScriptTag(option, script)) {
    if (UniqueChars str = QuoteString(cx, option, '"')) {
      JS_ReportErrorNumberASCII(cx, js::GetErrorMessage, nullptr,
                                JSMSG_INVALID_OPTION_VALUE, "script",
                                str.get());
    }
    return false;
  }

  // Step 6.
  if (!GetStringOption(cx, options, cx->names().region, &option)) {
    return false;
  }

  // Step 7.
  mozilla::intl::RegionSubtag region;
  if (option && !ParseStandaloneRegionTag(option, region)) {
    if (UniqueChars str = QuoteString(cx, option, '"')) {
      JS_ReportErrorNumberASCII(cx, js::GetErrorMessage, nullptr,
                                JSMSG_INVALID_OPTION_VALUE, "region",
                                str.get());
    }
    return false;
  }

  // Step 8.
  if (!GetStringOption(cx, options, cx->names().variants, &option)) {
    return false;
  }

  // Step 9.
  mozilla::intl::Locale::VariantsVector variants;
  if (option) {
    bool ok;
    if (!ParseStandaloneVariantTag(option, variants, &ok)) {
      ReportOutOfMemory(cx);
      return false;
    }
    if (!ok) {
      if (UniqueChars str = QuoteString(cx, option, '"')) {
        JS_ReportErrorNumberASCII(cx, js::GetErrorMessage, nullptr,
                                  JSMSG_INVALID_OPTION_VALUE, "variants",
                                  str.get());
      }
      return false;
    }
  }

  // Skip steps 10-15 when no subtags were modified.
  if (language.Present() || script.Present() || region.Present() ||
      !variants.empty()) {
    // Step 10. (Not applicable in our implementation.)

    // Step 11.
    if (language.Present()) {
      tag.SetLanguage(language);
    }

    // Step 12.
    if (script.Present()) {
      tag.SetScript(script);
    }

    // Step 13.
    if (region.Present()) {
      tag.SetRegion(region);
    }

    // Step 14.
    if (!variants.empty()) {
      tag.SetVariants(std::move(variants));
    }

    // Step 15.
    //
    // Optimization to perform base-name canonicalization early. This avoids
    // extra work later on.
    auto result = tag.CanonicalizeBaseName();
    if (result.isErr()) {
      if (result.unwrapErr() ==
          mozilla::intl::Locale::CanonicalizationError::DuplicateVariant) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_DUPLICATE_VARIANT_SUBTAG);
      } else {
        ReportInternalError(cx);
      }
      return false;
    }
  }

  // Step 16.
  return true;
}

/**
 * ApplyUnicodeExtensionToTag( tag, options, relevantExtensionKeys )
 */
bool js::intl::ApplyUnicodeExtensionToTag(
    JSContext* cx, mozilla::intl::Locale& tag,
    JS::HandleVector<UnicodeExtensionKeyword> keywords) {
  // If no Unicode extensions were present in the options object, we can skip
  // everything below and directly return.
  if (keywords.empty()) {
    return true;
  }

  Vector<char, 32> newExtension(cx);
  if (!newExtension.append('u')) {
    return false;
  }

  // Check if there's an existing Unicode extension subtag.

  const char* unicodeExtensionEnd = nullptr;
  const char* unicodeExtensionKeywords = nullptr;
  if (auto unicodeExtension = tag.GetUnicodeExtension()) {
    const char* unicodeExtensionBegin = unicodeExtension->data();
    unicodeExtensionEnd = unicodeExtensionBegin + unicodeExtension->size();

    SepKeywordIterator<char> iter(unicodeExtensionBegin, unicodeExtensionEnd);

    // Find the start of the first keyword.
    unicodeExtensionKeywords = iter.next();

    // Copy any attributes present before the first keyword.
    const char* attributesEnd = unicodeExtensionKeywords
                                    ? unicodeExtensionKeywords
                                    : unicodeExtensionEnd;
    if (!newExtension.append(unicodeExtensionBegin + 1, attributesEnd)) {
      return false;
    }
  }

  // Append the new keywords before any existing keywords. That way any previous
  // keyword with the same key is detected as a duplicate when canonicalizing
  // the Unicode extension subtag and gets discarded.

  for (const auto& keyword : keywords) {
    UnicodeExtensionKeyword::UnicodeKeySpan key = keyword.key();
    if (!newExtension.append('-')) {
      return false;
    }
    if (!newExtension.append(key.data(), key.size())) {
      return false;
    }
    if (!newExtension.append('-')) {
      return false;
    }

    JS::AutoCheckCannotGC nogc;
    JSLinearString* type = keyword.type();
    if (type->hasLatin1Chars()) {
      if (!newExtension.append(type->latin1Chars(nogc), type->length())) {
        return false;
      }
    } else {
      if (!newExtension.append(type->twoByteChars(nogc), type->length())) {
        return false;
      }
    }
  }

  // Append the remaining keywords from the previous Unicode extension subtag.
  if (unicodeExtensionKeywords) {
    if (!newExtension.append(unicodeExtensionKeywords, unicodeExtensionEnd)) {
      return false;
    }
  }

  if (auto res = tag.SetUnicodeExtension(newExtension); res.isErr()) {
    ReportInternalError(cx, res.unwrapErr());
    return false;
  }

  return true;
}

static JS::Result<JSLinearString*> LanguageTagFromMaybeWrappedLocale(
    JSContext* cx, JSObject* obj) {
  if (obj->is<LocaleObject>()) {
    return obj->as<LocaleObject>().getLanguageTag();
  }

  JSObject* unwrapped = CheckedUnwrapStatic(obj);
  if (!unwrapped) {
    ReportAccessDenied(cx);
    return cx->alreadyReportedError();
  }

  if (!unwrapped->is<LocaleObject>()) {
    return nullptr;
  }

  Rooted<JSString*> tagStr(cx, unwrapped->as<LocaleObject>().getLanguageTag());
  if (!cx->compartment()->wrap(cx, &tagStr)) {
    return cx->alreadyReportedError();
  }

  auto* linear = tagStr->ensureLinear(cx);
  if (!linear) {
    return cx->alreadyReportedError();
  }
  return linear;
}

/**
 * Intl.Locale( tag[, options] )
 */
static bool Locale(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  if (!ThrowIfNotConstructing(cx, args, "Intl.Locale")) {
    return false;
  }

  // Steps 2-6 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  Rooted<JSObject*> proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_Locale, &proto)) {
    return false;
  }

  // Steps 7-9.
  Handle<JS::Value> tagValue = args.get(0);
  JSString* tagStr;
  if (tagValue.isObject()) {
    JS_TRY_VAR_OR_RETURN_FALSE(
        cx, tagStr,
        LanguageTagFromMaybeWrappedLocale(cx, &tagValue.toObject()));
    if (!tagStr) {
      tagStr = ToString(cx, tagValue);
      if (!tagStr) {
        return false;
      }
    }
  } else if (tagValue.isString()) {
    tagStr = tagValue.toString();
  } else {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_INVALID_LOCALES_ELEMENT);
    return false;
  }

  Rooted<JSLinearString*> tagLinearStr(cx, tagStr->ensureLinear(cx));
  if (!tagLinearStr) {
    return false;
  }

  // Step 10.
  Rooted<JSObject*> options(cx);
  if (args.hasDefined(1)) {
    options = ToObject(cx, args[1]);
    if (!options) {
      return false;
    }
  }

  // Step 11.
  mozilla::intl::Locale tag;
  if (!ParseLocale(cx, tagLinearStr, tag)) {
    return false;
  }

  if (tag.Language().Length() > 4) {
    MOZ_ASSERT(cx->global());
    cx->runtime()->setUseCounter(cx->global(),
                                 JSUseCounter::LEGACY_LANG_SUBTAG);
  }

  // Steps 12-13. (Optimized to only perform base-name canonicalization.)
  if (auto result = tag.CanonicalizeBaseName(); result.isErr()) {
    if (result.unwrapErr() ==
        mozilla::intl::Locale::CanonicalizationError::DuplicateVariant) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DUPLICATE_VARIANT_SUBTAG);
    } else {
      ReportInternalError(cx);
    }
    return false;
  }

  if (options) {
    // Step 14.
    if (!ApplyOptionsToTag(cx, tag, options)) {
      return false;
    }

    // Step 15.
    JS::RootedVector<UnicodeExtensionKeyword> keywords(cx);

    // Steps 16-18.
    Rooted<JSLinearString*> calendar(cx);
    if (!GetUnicodeExtensionOption(cx, options, UnicodeExtensionKey::Calendar,
                                   &calendar)) {
      return false;
    }
    if (calendar) {
      if (!keywords.emplaceBack("ca", calendar)) {
        return false;
      }
    }

    // Steps 19-21.
    Rooted<JSLinearString*> collation(cx);
    if (!GetUnicodeExtensionOption(cx, options, UnicodeExtensionKey::Collation,
                                   &collation)) {
      return false;
    }
    if (collation) {
      if (!keywords.emplaceBack("co", collation)) {
        return false;
      }
    }

    if (JS::Prefs::experimental_intl_locale_info()) {
      Rooted<JSLinearString*> firstDayOfWeek(cx);

      // Step 22.
      if (!GetStringOption(cx, options, cx->names().firstDayOfWeek,
                           &firstDayOfWeek)) {
        return false;
      }

      // Steps 23-24.
      if (firstDayOfWeek) {
        // Step 23.a.
        firstDayOfWeek = WeekdayToUValue(cx, firstDayOfWeek);
        if (!firstDayOfWeek) {
          return false;
        }

        // Step 23.b.
        firstDayOfWeek = GetUnicodeExtensionOption(
            cx, UnicodeExtensionKey::FirstDayOfWeek, firstDayOfWeek);
        if (!firstDayOfWeek) {
          return false;
        }

        // Step 24.
        if (!keywords.emplaceBack("fw", firstDayOfWeek)) {
          return false;
        }
      }
    }

    // Step 25.
    static constexpr auto hourCycles = MapOptions<LocaleHourCycleToString>(
        LocaleHourCycle::H11, LocaleHourCycle::H12, LocaleHourCycle::H23,
        LocaleHourCycle::H24);
    mozilla::Maybe<LocaleHourCycle> hourCycle{};
    if (!GetStringOption(cx, options, cx->names().hourCycle, hourCycles,
                         &hourCycle)) {
      return false;
    }

    // Step 26.
    if (hourCycle) {
      if (!keywords.emplaceBack("hc", ToUnicodeValue(cx, *hourCycle))) {
        return false;
      }
    }

    // Step 27.
    static constexpr auto caseFirsts = MapOptions<LocaleCaseFirstToString>(
        LocaleCaseFirst::Upper, LocaleCaseFirst::Lower, LocaleCaseFirst::False);
    mozilla::Maybe<LocaleCaseFirst> caseFirst{};
    if (!GetStringOption(cx, options, cx->names().caseFirst, caseFirsts,
                         &caseFirst)) {
      return false;
    }

    // Step 28.
    if (caseFirst) {
      if (!keywords.emplaceBack("kf", ToUnicodeValue(cx, *caseFirst))) {
        return false;
      }
    }

    // Step 29.
    mozilla::Maybe<bool> numeric{};
    if (!GetBooleanOption(cx, options, cx->names().numeric, &numeric)) {
      return false;
    }

    // Step 30-31.
    if (numeric) {
      if (!keywords.emplaceBack("kn", BooleanToString(cx, *numeric))) {
        return false;
      }
    }

    // Steps 32-34.
    Rooted<JSLinearString*> numberingSystem(cx);
    if (!GetUnicodeExtensionOption(cx, options,
                                   UnicodeExtensionKey::NumberingSystem,
                                   &numberingSystem)) {
      return false;
    }
    if (numberingSystem) {
      if (!keywords.emplaceBack("nu", numberingSystem)) {
        return false;
      }
    }

    // Step 35.
    if (!ApplyUnicodeExtensionToTag(cx, tag, keywords)) {
      return false;
    }
  }

  // ApplyUnicodeExtensionToTag, steps 6-7.
  if (auto result = tag.CanonicalizeExtensions(); result.isErr()) {
    if (result.unwrapErr() ==
        mozilla::intl::Locale::CanonicalizationError::DuplicateVariant) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DUPLICATE_VARIANT_SUBTAG);
    } else {
      ReportInternalError(cx);
    }
    return false;
  }

  // Steps 6, 36-43.
  JSObject* obj = CreateLocaleObject(cx, proto, tag);
  if (!obj) {
    return false;
  }

  // Step 44.
  args.rval().setObject(*obj);
  return true;
}

using UnicodeKey = const char (&)[UnicodeKeyLength + 1];

// Returns the tuple [index, length] of the `type` in the `keyword` in Unicode
// locale extension |extension| that has |key| as its `key`. If `keyword` lacks
// a type, the returned |index| will be where `type` would have been, and
// |length| will be set to zero.
template <typename CharT>
static mozilla::Maybe<IndexAndLength> FindUnicodeExtensionType(
    const CharT* extension, size_t length, UnicodeKey key) {
  MOZ_ASSERT(extension[0] == 'u');
  MOZ_ASSERT(extension[1] == '-');

  const CharT* end = extension + length;

  SepKeywordIterator<CharT> iter(extension, end);

  // Search all keywords until a match was found.
  const CharT* beginKey;
  while (true) {
    beginKey = iter.next();
    if (!beginKey) {
      return mozilla::Nothing();
    }

    // Add +1 to skip over the separator preceding the keyword.
    MOZ_ASSERT(beginKey[0] == '-');
    beginKey++;

    // Exit the loop on the first match.
    if (std::equal(beginKey, beginKey + UnicodeKeyLength, key)) {
      break;
    }
  }

  // Skip over the key.
  const CharT* beginType = beginKey + UnicodeKeyLength;

  // Find the start of the next keyword.
  const CharT* endType = iter.next();

  // No further keyword present, the current keyword ends the Unicode extension.
  if (!endType) {
    endType = end;
  }

  // If the keyword has a type, skip over the separator preceding the type.
  if (beginType != endType) {
    MOZ_ASSERT(beginType[0] == '-');
    beginType++;
  }
  return mozilla::Some(IndexAndLength{size_t(beginType - extension),
                                      size_t(endType - beginType)});
}

static inline auto FindUnicodeExtensionType(
    const JSLinearString* unicodeExtension, UnicodeKey key) {
  JS::AutoCheckCannotGC nogc;
  return unicodeExtension->hasLatin1Chars()
             ? FindUnicodeExtensionType(
                   reinterpret_cast<const char*>(
                       unicodeExtension->latin1Chars(nogc)),
                   unicodeExtension->length(), key)
             : FindUnicodeExtensionType(unicodeExtension->twoByteChars(nogc),
                                        unicodeExtension->length(), key);
}

// Return the sequence of `uvalue` subtags for the Unicode extension keyword
// specified by key or undefined when the keyword isn't present.
static bool GetUnicodeExtension(JSContext* cx, LocaleObject* locale,
                                UnicodeKey key,
                                MutableHandle<JS::Value> result) {
  // Return undefined when no Unicode extension subtag is present.
  auto* unicodeExtension = locale->getUnicodeExtension();
  if (!unicodeExtension) {
    result.setUndefined();
    return true;
  }

  // Find the `uvalue of the requested key in the Unicode extension subtag.
  auto indexAndLength = FindUnicodeExtensionType(unicodeExtension, key);

  // Return undefined if the requested key isn't present in the extension.
  if (!indexAndLength) {
    result.setUndefined();
    return true;
  }
  auto [index, length] = *indexAndLength;

  // Otherwise return the `uvalue` of the found keyword.
  auto* str = NewDependentString(cx, unicodeExtension, index, length);
  if (!str) {
    return false;
  }
  result.setString(str);
  return true;
}

struct UnicodeValue {
  // Length of a single `uvalue` subtag.
  static constexpr size_t UValueLength = 8;

  size_t length_;
  char value_[UValueLength] = {};

  template <typename CharT>
  explicit UnicodeValue(mozilla::Span<const CharT> span)
      : length_(span.size()) {
    MOZ_RELEASE_ASSERT(span.size() >= 3 && span.size() <= UValueLength);
    MOZ_ASSERT(std::all_of(span.begin(), span.end(),
                           mozilla::IsAsciiAlphanumeric<CharT>));
    std::copy_n(span.data(), span.size(), value_);
  }

 public:
  auto asSpan() const { return mozilla::Span<const char>{value_, length_}; }

  template <typename CharT>
  static mozilla::Maybe<UnicodeValue> from(mozilla::Span<const CharT> uvalue) {
    // Tell the analysis this function can't GC. (bug 1588528)
    JS::AutoSuppressGCAnalysis nogc;

    MOZ_ASSERT(mozilla::IsAscii(uvalue));

    size_t length = uvalue.size();

    // Definitely too small or too long for a single `uvalue` subtag.
    if (length == 0 || length > UValueLength) {
      return mozilla::Nothing();
    }
    MOZ_ASSERT(length >= 3);

    // Search for "-" separator. This case can only happen when `uvalue` has at
    // least 7 characters, because each `uvalue` subtag has 3-8 characters.
    //
    // The "-" separator can only occur at the 4th or 5th position:
    // 1. uuu-uuu
    // 2. uuu-uuuu
    // 3. uuuu-uuu
    if (length >= 7 && (uvalue[3] == '-' || uvalue[4] == '-')) {
      return mozilla::Nothing();
    }
    return mozilla::Some(UnicodeValue{uvalue});
  }
};

// Return the single `uvalue` subtag for the Unicode extension keyword specified
// by key or undefined when the keyword isn't present or has multiple `uvalue`
// subtags.
static mozilla::Maybe<UnicodeValue> GetUnicodeExtension(LocaleObject* locale,
                                                        UnicodeKey key) {
  // Return when no Unicode extension subtag is present.
  const auto* unicodeExtension = locale->getUnicodeExtension();
  if (!unicodeExtension) {
    return mozilla::Nothing();
  }

  // Find the `uvalue` of the requested key in the Unicode extension subtag.
  auto indexAndLength = FindUnicodeExtensionType(unicodeExtension, key);

  // Return if the requested key isn't present in the extension.
  if (!indexAndLength) {
    return mozilla::Nothing();
  }
  auto [index, length] = *indexAndLength;

  // Otherwise return the `uvalue` of the found keyword.
  JS::AutoCheckCannotGC nogc;
  if (unicodeExtension->hasLatin1Chars()) {
    auto uext = mozilla::AsChars(unicodeExtension->latin1Range(nogc));
    return UnicodeValue::from(uext.subspan(index, length));
  }
  auto uext = mozilla::Span{unicodeExtension->twoByteRange(nogc)};
  return UnicodeValue::from(uext.subspan(index, length));
}

struct BaseNamePartsResult {
  IndexAndLength language;
  mozilla::Maybe<IndexAndLength> script;
  mozilla::Maybe<IndexAndLength> region;
};

// Returns [language-length, script-index, region-index, region-length].
template <typename CharT>
static BaseNamePartsResult BaseNameParts(mozilla::Span<const CharT> baseName) {
  size_t languageLength = baseName.size();
  size_t scriptIndex = 0;
  size_t regionIndex = 0;
  size_t regionLength = 0;

  // Search the first separator to find the end of the language subtag.
  if (baseName.size() > 3) {
    if (baseName[2] == '-' || baseName[3] == '-') [[likely]] {
      languageLength = baseName[2] == '-' ? 2 : 3;
    } else {
      // Language subtag with more than three letters.
      if (const CharT* sep = std::char_traits<CharT>::find(
              baseName.data(), baseName.size(), '-')) {
        languageLength = sep - baseName.data();
      }
    }

    if (languageLength < baseName.size()) {
      // Add +1 to skip over the separator character.
      size_t nextSubtag = languageLength + 1;

      // Script subtags are always four characters long, but take care for a
      // four character long variant subtag. These start with a digit.
      if ((nextSubtag + ScriptLength == baseName.size() ||
           (nextSubtag + ScriptLength < baseName.size() &&
            baseName[nextSubtag + ScriptLength] == '-')) &&
          mozilla::IsAsciiAlpha(baseName[nextSubtag])) {
        scriptIndex = nextSubtag;
        nextSubtag = scriptIndex + ScriptLength + 1;
      }

      // Region subtags can be either two or three characters long.
      if (nextSubtag < baseName.size()) {
        for (size_t rlen : {AlphaRegionLength, DigitRegionLength}) {
          MOZ_ASSERT(nextSubtag + rlen <= baseName.size());
          if (nextSubtag + rlen == baseName.size() ||
              baseName[nextSubtag + rlen] == '-') {
            regionIndex = nextSubtag;
            regionLength = rlen;
            break;
          }
        }
      }
    }
  }

  // Tell the analysis the |IsStructurallyValid*Tag| functions can't GC.
  JS::AutoSuppressGCAnalysis nogc;

  IndexAndLength language{0, languageLength};
  MOZ_ASSERT(mozilla::intl::IsStructurallyValidLanguageTag(
      baseName.subspan(language.index, language.length)));

  mozilla::Maybe<IndexAndLength> script{};
  if (scriptIndex) {
    script.emplace(scriptIndex, ScriptLength);
    MOZ_ASSERT(mozilla::intl::IsStructurallyValidScriptTag(
        baseName.subspan(script->index, script->length)));
  }

  mozilla::Maybe<IndexAndLength> region{};
  if (regionIndex) {
    region.emplace(regionIndex, regionLength);
    MOZ_ASSERT(mozilla::intl::IsStructurallyValidRegionTag(
        baseName.subspan(region->index, region->length)));
  }

  return {language, script, region};
}

static inline auto BaseNameParts(const JSLinearString* baseName) {
  JS::AutoCheckCannotGC nogc;
  return baseName->hasLatin1Chars()
             ? BaseNameParts(mozilla::AsChars(baseName->latin1Range(nogc)))
             : BaseNameParts(mozilla::Span{baseName->twoByteRange(nogc)});
}

/**
 * GetLocaleLanguage ( locale )
 */
static auto GetLocaleLanguage(const LocaleObject* locale) {
  const auto* baseName = locale->getBaseName();

  auto language = [](auto baseName) {
    mozilla::intl::LanguageSubtag language{};

    auto parts = BaseNameParts(baseName);
    language.Set(baseName.subspan(parts.language.index, parts.language.length));
    return language;
  };

  JS::AutoCheckCannotGC nogc;
  return baseName->hasLatin1Chars()
             ? language(mozilla::AsChars(baseName->latin1Range(nogc)))
             : language(mozilla::Span{baseName->twoByteRange(nogc)});
}

/**
 * GetLocaleScript ( locale )
 */
static auto GetLocaleScript(const LocaleObject* locale) {
  const auto* baseName = locale->getBaseName();

  auto script = [](auto baseName) {
    mozilla::intl::ScriptSubtag script{};

    auto parts = BaseNameParts(baseName);
    if (parts.script) {
      script.Set(baseName.subspan(parts.script->index, parts.script->length));
    }
    return script;
  };

  JS::AutoCheckCannotGC nogc;
  return baseName->hasLatin1Chars()
             ? script(mozilla::AsChars(baseName->latin1Range(nogc)))
             : script(mozilla::Span{baseName->twoByteRange(nogc)});
}

/**
 * GetLocaleRegion ( locale )
 */
static auto GetLocaleRegion(const LocaleObject* locale) {
  const auto* baseName = locale->getBaseName();

  auto region = [](auto baseName) {
    mozilla::intl::RegionSubtag region{};

    auto parts = BaseNameParts(baseName);
    if (parts.region) {
      region.Set(baseName.subspan(parts.region->index, parts.region->length));
    }
    return region;
  };

  JS::AutoCheckCannotGC nogc;
  return baseName->hasLatin1Chars()
             ? region(mozilla::AsChars(baseName->latin1Range(nogc)))
             : region(mozilla::Span{baseName->twoByteRange(nogc)});
}

/**
 * GetLocaleVariants ( locale )
 */
static mozilla::Maybe<IndexAndLength> GetLocaleVariants(
    const LocaleObject* locale) {
  const auto* baseName = locale->getBaseName();
  auto parts = BaseNameParts(baseName);

  // Variants are the trailing subtags in the base-name. Find which subtag
  // precedes the variants.
  auto precedingSubtag = parts.region   ? *parts.region
                         : parts.script ? *parts.script
                                        : parts.language;

  // Index of the next subtag, including the leading '-' character.
  size_t index = precedingSubtag.index + precedingSubtag.length;

  if (index == baseName->length()) {
    return mozilla::Nothing();
  }
  MOZ_ASSERT(baseName->latin1OrTwoByteChar(index) == '-',
             "missing '-' separator after precedingSubtag");

  // Skip over the '-' separator.
  index += 1;

  // Length of the variant subtags.
  size_t length = baseName->length() - index;

  return mozilla::Some(IndexAndLength{index, length});
}

/**
 * Create an array which holds a single item.
 */
static ArrayObject* CreateArrayFromValue(JSContext* cx,
                                         Handle<JS::Value> item) {
  auto* array = NewDenseFullyAllocatedArray(cx, 1);
  if (!array) {
    return nullptr;
  }
  array->setDenseInitializedLength(1);
  array->initDenseElement(0, item);
  return array;
}

/**
 * CanonicalUnicodeSubdivision ( locale, key )
 */
static bool CanonicalUnicodeSubdivision(JSContext* cx,
                                        Handle<LocaleObject*> locale,
                                        UnicodeKey key,
                                        mozilla::intl::RegionSubtag* result) {
  // FIXME: spec bug - align with UTS 35
  //
  // Implementation follows spec PR: https://github.com/tc39/ecma402/pull/1059

  // Step 1.
  auto subdivision = GetUnicodeExtension(locale, key);

  // Step 2.
  if (subdivision.isNothing()) {
    *result = {};
    return true;
  }

  // Step 3.
  //
  // Try to match the `unicode_subdivision_id` production from UTS 35.
  //
  // unicode_subdivision_id = unicode_region_subtag unicode_subdivision_suffix ;
  // unicode_region_subtag = (alpha{2} | digit{3}) ;
  // unicode_subdivision_suffix = alphanum{1,4} ;
  //
  // See <https://unicode.org/reports/tr35/#Unicode_locale_identifier>.

  auto uvalue = subdivision->asSpan();

  size_t regionLength;
  if (mozilla::IsAsciiAlpha(uvalue[0]) && mozilla::IsAsciiAlpha(uvalue[1])) {
    // Two letter region subtag.
    regionLength = 2;
  } else if (mozilla::IsAsciiDigit(uvalue[0]) &&
             mozilla::IsAsciiDigit(uvalue[1]) &&
             mozilla::IsAsciiDigit(uvalue[2])) {
    // Three digit region subtag.
    regionLength = 3;
  } else {
    *result = {};
    return true;
  }

  size_t suffixLength = uvalue.size() - regionLength;
  if (suffixLength < 1 || suffixLength > 4) {
    *result = {};
    return true;
  }

  // Step 4.
  auto region = mozilla::intl::RegionSubtag{uvalue.to(regionLength)};

  // Step 5.
  region.ToUpperCase();

  // Step 6.
  auto regionResult = mozilla::intl::Region::From(region);
  if (regionResult.isErr()) {
    ReportInternalError(cx, regionResult.unwrapErr());
    return false;
  }

  auto regionMaybe = regionResult.unwrap();
  if (!regionMaybe || !regionMaybe->IsRegular()) {
    *result = {};
    return true;
  }

  // Step 7.
  *result = region;
  return true;
}

struct RegionPref {
  mozilla::intl::RegionSubtag region;

  enum class RegionOverride : bool { No, Yes };
  RegionOverride regionOverride = RegionOverride::No;
};

/**
 * RegionPreference ( locale )
 */
static bool RegionPreference(JSContext* cx, Handle<LocaleObject*> locale,
                             RegionPref* result) {
  // FIXME: spec bug - align with UTS 35
  //
  // Implementation follows spec PR: https://github.com/tc39/ecma402/pull/1059

  // Step 1.
  mozilla::intl::RegionSubtag regionOverride;
  if (!CanonicalUnicodeSubdivision(cx, locale, "rg", &regionOverride)) {
    return false;
  }

  // Step 2.
  if (regionOverride.Present()) {
    *result = {regionOverride, RegionPref::RegionOverride::Yes};
    return true;
  }

  // Step 3.
  auto region = GetLocaleRegion(locale);

  // Step 4.
  if (region.Present()) {
    *result = {region};
    return true;
  }

  // Step 5.
  mozilla::intl::RegionSubtag regionSubdiv;
  if (!CanonicalUnicodeSubdivision(cx, locale, "sd", &regionSubdiv)) {
    return false;
  }

  // Step 6.
  if (regionSubdiv.Present()) {
    *result = {regionSubdiv};
    return true;
  }

  // Steps 7-8.
  //
  // NB: `mozilla::intl::Locale::AddLikelySubtags` canonicalizes its result.
  mozilla::intl::Locale loc;
  loc.SetLanguage(GetLocaleLanguage(locale));
  loc.SetScript(GetLocaleScript(locale));

  if (auto result = loc.AddLikelySubtags(); result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return false;
  }

  // Step 9.
  auto likelyRegion = loc.Region();

  // Step 10.
  if (likelyRegion.Present()) {
    *result = {likelyRegion};
    return true;
  }

  // Step 11.
  *result = {mozilla::intl::RegionSubtag(mozilla::MakeStringSpan("001"))};
  return true;
}

/**
 * CalendarsOfLocale ( loc )
 *
 * Return the commonly used calendars of |locale| in preference order.
 */
static ArrayObject* CalendarsOfLocale(JSContext* cx,
                                      Handle<LocaleObject*> locale) {
  // Step 1.
  Rooted<JS::Value> preferred(cx);
  if (!GetUnicodeExtension(cx, locale, "ca", &preferred)) {
    return nullptr;
  }
  MOZ_ASSERT(preferred.isString() || preferred.isUndefined());

  if (preferred.isString()) {
    return CreateArrayFromValue(cx, preferred);
  }

  // Steps 2-6.
  RegionPref preference;
  if (!RegionPreference(cx, locale, &preference)) {
    return nullptr;
  }
  auto region = preference.region;

  Rooted<ArrayObject*> array(cx, NewDenseEmptyArray(cx));
  if (!array) {
    return nullptr;
  }

  // Steps 7-8.
  //
  // Get the calendars that are commonly used in the lookup region.
  {
    // Hazard analysis complains that the mozilla::Result destructor calls a GC
    // function, which is unsound when returning an unrooted value. Work around
    // this issue by restricting the lifetime of |keywords| to a separate block.

    auto keywords = mozilla::intl::Calendar::GetBcp47KeywordValuesForRegion(
        region, mozilla::intl::Calendar::CommonlyUsed::Yes);
    if (keywords.isErr()) {
      ReportInternalError(cx, keywords.unwrapErr());
      return nullptr;
    }

    // Step 9.
    for (auto keyword : keywords.unwrap()) {
      if (keyword.isErr()) {
        ReportInternalError(cx);
        return nullptr;
      }
      auto calendar = keyword.unwrap();

      // Don't return deprecated calendar variants.
      if (calendar == mozilla::MakeStringSpan("islamic") ||
          calendar == mozilla::MakeStringSpan("islamic-rgsa")) {
        continue;
      }

      auto* string = NewStringCopy<CanGC>(cx, calendar);
      if (!string) {
        return nullptr;
      }
      if (!NewbornArrayPush(cx, array, JS::StringValue(string))) {
        return nullptr;
      }
    }
  }

  return array;
}

/**
 * CollationsOfLocale ( loc )
 *
 * Return the commonly used collations of |locale| in sorted order.
 */
static ArrayObject* CollationsOfLocale(JSContext* cx,
                                       Handle<LocaleObject*> locale) {
  // Step 1.
  Rooted<JS::Value> preferred(cx);
  if (!GetUnicodeExtension(cx, locale, "co", &preferred)) {
    return nullptr;
  }
  MOZ_ASSERT(preferred.isString() || preferred.isUndefined());

  if (preferred.isString()) {
    return CreateArrayFromValue(cx, preferred);
  }

  // Steps 2-4.
  auto langId = LanguageId::und();
  if (auto parsedLangId = ToLanguageId(cx, locale->getBaseName())) {
    if (parsedLangId->language() != "und") {
      mozilla::Maybe<LanguageId> foundLocale;
      if (!LookupMatcher(cx, AvailableLocaleKind::Collator, *parsedLangId,
                         &foundLocale)) {
        return nullptr;
      }

      if (foundLocale) {
        langId = *foundLocale;
      } else {
        // Fallback to the default locale to match ICU4C:
        //
        // See also <https://github.com/tc39/ecma402/issues/1053>.
        if (!DefaultLocale(cx, &langId)) {
          return nullptr;
        }
      }
    }
  } else {
    MOZ_ASSERT(GetLocaleLanguage(locale).Length() > 3);

    if (!DefaultLocale(cx, &langId)) {
      return nullptr;
    }
  }

  Rooted<StringList> list(cx, StringList(cx));

  auto langIdStr = langId.toString();
  auto collations = mozilla::intl::Collator::GetBcp47KeywordValues();
  for (auto collation : collations) {
    if (mozilla::intl::Collator::IsSupportedCollation(langIdStr, collation)) {
      auto* string = NewStringCopy<CanGC>(cx, collation);
      if (!string) {
        return nullptr;
      }
      if (!list.append(string)) {
        return nullptr;
      }
    }
  }

  // Steps 5-6.
  return CreateSortedArrayFromList(cx, &list);
}

/**
 * HourCyclesOfLocale ( loc )
 *
 * Return the commonly used hour-cycles of |locale| in preference order.
 */
static ArrayObject* HourCyclesOfLocale(JSContext* cx,
                                       Handle<LocaleObject*> locale) {
  // Step 1.
  Rooted<JS::Value> preferred(cx);
  if (!GetUnicodeExtension(cx, locale, "hc", &preferred)) {
    return nullptr;
  }
  MOZ_ASSERT(preferred.isString() || preferred.isUndefined());

  if (preferred.isString()) {
    return CreateArrayFromValue(cx, preferred);
  }

  // Steps 2-7.
  //
  // Include `language` in lookup: <https://github.com/tc39/ecma402/issues/1056>
  auto language = GetLocaleLanguage(locale);

  RegionPref preference;
  if (!RegionPreference(cx, locale, &preference)) {
    return nullptr;
  }
  auto region = preference.region;

  auto result =
      mozilla::intl::DateTimeFormat::GetAllowedHourCycles(language, region);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  auto hourCycles = result.unwrap();

  using HourCycle = mozilla::intl::DateTimeFormat::HourCycle;

  // Step 8.
  if (hourCycles.empty()) {
    static_assert(decltype(hourCycles)::InlineLength > 0);
    MOZ_ALWAYS_TRUE(hourCycles.reserve(1));

    hourCycles.infallibleAppend(HourCycle::H23);
  }

  // Step 9.
  auto* array = NewDenseFullyAllocatedArray(cx, hourCycles.length());
  if (!array) {
    return nullptr;
  }
  array->setDenseInitializedLength(hourCycles.length());

  uint32_t index = 0;
  for (auto hourCycle : hourCycles) {
    JSString* hcname;
    switch (hourCycle) {
      case HourCycle::H11:
        hcname = cx->names().h11;
        break;
      case HourCycle::H12:
        hcname = cx->names().h12;
        break;
      case HourCycle::H23:
        hcname = cx->names().h23;
        break;
      case HourCycle::H24:
        hcname = cx->names().h24;
        break;
    }

    array->initDenseElement(index++, JS::StringValue(hcname));
  }
  MOZ_ASSERT(index == hourCycles.length());

  return array;
}

/**
 * NumberingSystemsOfLocale ( loc )
 *
 * Return the commonly used numbering systems of |locale| in preference order.
 */
static ArrayObject* NumberingSystemsOfLocale(JSContext* cx,
                                             Handle<LocaleObject*> locale) {
  // Step 1.
  Rooted<JS::Value> preferred(cx);
  if (!GetUnicodeExtension(cx, locale, "nu", &preferred)) {
    return nullptr;
  }
  MOZ_ASSERT(preferred.isString() || preferred.isUndefined());

  if (preferred.isString()) {
    return CreateArrayFromValue(cx, preferred);
  }

  // Step 2.
  mozilla::Maybe<LanguageId> foundLocale;
  if (auto langId = ToLanguageId(cx, locale->getBaseName())) {
    if (!LookupMatcher(cx, AvailableLocaleKind::NumberFormat, *langId,
                       &foundLocale)) {
      return nullptr;
    }
  } else {
    MOZ_ASSERT(GetLocaleLanguage(locale).Length() > 3);
  }

  // Steps 3-4.
  JSString* numberingSystem;
  if (foundLocale) {
    numberingSystem = DefaultNumberingSystem(cx, *foundLocale);
  } else {
    numberingSystem = NewStringCopyZ<CanGC>(cx, "latn");
  }
  if (!numberingSystem) {
    return nullptr;
  }

  // Step 5.
  Rooted<JS::Value> value(cx, JS::StringValue(numberingSystem));
  return CreateArrayFromValue(cx, value);
}

/**
 * TextDirectionOfLocale ( loc )
 *
 * Return the text direction of |locale|.
 */
static JS::Value TextDirectionOfLocale(JSContext* cx, LocaleObject* locale) {
  auto langId = LanguageId::und();
  if (auto parsedLangId = ToLanguageId(cx, locale->getBaseName())) {
    langId = *parsedLangId;
  } else {
    // ICU4X doesn't support language subtags with more than three letters.
    //
    // If |baseName| contains a script subtag, then replace the language subtag
    // with "und". Otherwise return `undefined` because the script subtag of
    // non-registered languages can't be inferred.
    MOZ_ASSERT(GetLocaleLanguage(locale).Length() > 3);

    auto script = GetLocaleScript(locale);
    if (script.Missing()) {
      return JS::UndefinedValue();
    }
    auto span = script.Span();

    langId = LanguageId::fromParts("und", {span.data(), span.size()}, "");
  }

  auto langIdStr = langId.toString();
  switch (locale_text_direction_of(langIdStr.data(), langIdStr.length())) {
    case TextDirection::Unknown:
      return JS::UndefinedValue();
    case TextDirection::LeftToRight:
      return JS::StringValue(cx->names().ltr);
    case TextDirection::RightToLeft:
      return JS::StringValue(cx->names().rtl);
  }
  MOZ_CRASH("invalid text direction");
}

static bool AddTimeZonesToList(JSContext* cx,
                               const mozilla::intl::RegionSubtag& region,
                               MutableHandle<StringList> list) {
  // Get the time zones that are commonly used in the given region.
  auto values = mozilla::intl::TimeZone::GetAvailableTimeZones(region);
  if (values.isErr()) {
    ReportInternalError(cx, values.unwrapErr());
    return false;
  }

  auto& sharedIntlData = cx->runtime()->sharedIntlData.ref();

  Rooted<JSAtom*> availableTimeZone(cx);
  Rooted<JSAtom*> primaryTimeZone(cx);
  for (auto value : values.unwrap()) {
    if (value.isErr()) {
      ReportInternalError(cx);
      return false;
    }

    // Reset
    availableTimeZone.set(nullptr);
    primaryTimeZone.set(nullptr);

    // Validate and canonicalize the time zone returned from ICU.
    if (!sharedIntlData.validateAndCanonicalizeTimeZone(
            cx, value.unwrap(), &availableTimeZone, &primaryTimeZone)) {
      return false;
    }

    // Append to list if successfully validated and canonicalized.
    if (primaryTimeZone) {
      if (!list.append(primaryTimeZone)) {
        return false;
      }
    }
  }
  return true;
}

/**
 * TimeZonesOfLocale ( loc )
 *
 * Return the commonly used time zones of |region| in sorted order.
 */
static bool TimeZonesOfLocale(JSContext* cx, LocaleObject* locale,
                              JS::MutableHandle<JS::Value> result) {
  // Step 1.
  auto region = GetLocaleRegion(locale);

  // Step 2.
  if (region.Missing()) {
    result.setUndefined();
    return true;
  }

  // Step 3.
  //
  // Unsorted list of canonical time zone names, possibly containing duplicates.
  Rooted<StringList> list(cx, StringList(cx));

  // Reject non-regular regions, like for example "001".
  auto regionResult = mozilla::intl::Region::From(region);
  if (regionResult.isErr()) {
    ReportInternalError(cx, regionResult.unwrapErr());
    return false;
  }

  auto regionMaybe = regionResult.unwrap();
  if (regionMaybe && regionMaybe->IsRegular()) {
    if (!AddTimeZonesToList(cx, region, &list)) {
      return false;
    }
  }

  // Step 4.
  auto* array = CreateSortedArrayFromList(cx, &list);
  if (!array) {
    return false;
  }

  result.setObject(*array);
  return true;
}

struct WeekInfo {
  /**
   * [[FirstDay]]
   *
   * Which day of the week is considered “first” for calendar purposes.
   */
  mozilla::intl::Weekday firstDay;

  /**
   * [[Weekend]]
   *
   * The list of weekday values indicating which days of the week are considered
   * as part of the “weekend” for calendar purposes.
   */
  mozilla::EnumSet<mozilla::intl::Weekday> weekend;
};

/**
 * WeekdayUValueToNumber ( fw )
 */
static mozilla::Maybe<mozilla::intl::Weekday> WeekdayUValueToNumber(
    const UnicodeValue& fw) {
  using Weekday = mozilla::intl::Weekday;

  static constexpr struct NameToWeekday {
    std::string_view name;
    Weekday weekday;
  } options[] = {
      // Likely cases first, based on existing supplemental week data.
      {"mon", Weekday::Monday},
      {"sun", Weekday::Sunday},
      {"sat", Weekday::Saturday},
      {"fri", Weekday::Friday},

      // Unlikely cases last.
      {"tue", Weekday::Tuesday},
      {"wed", Weekday::Wednesday},
      {"thu", Weekday::Thursday},
  };

  for (const auto& [name, weekday] : options) {
    if (fw.asSpan() == mozilla::Span{name}) {
      return mozilla::Some(weekday);
    }
  }
  return mozilla::Nothing();
}

/**
 * WeekInfoOfLocale ( loc )
 *
 * Return the week info of |locale|.
 */
static bool WeekInfoOfLocale(JSContext* cx, Handle<LocaleObject*> locale,
                             WeekInfo* result) {
  // Modified to include <https://github.com/tc39/ecma402/pull/1051>.

  // Steps 1-5.
  RegionPref preference;
  if (!RegionPreference(cx, locale, &preference)) {
    return false;
  }
  auto [region, regionOverride] = preference;

  // Step 6.
  auto calendarResult = mozilla::intl::Calendar::TryCreate(region);
  if (calendarResult.isErr()) {
    ReportInternalError(cx, calendarResult.unwrapErr());
    return false;
  }
  auto calendar = calendarResult.unwrap();

  auto weekend = calendar->GetWeekend();
  if (weekend.isErr()) {
    ReportInternalError(cx, weekend.unwrapErr());
    return false;
  }

  auto info = WeekInfo{
      .firstDay = calendar->GetFirstDayOfWeek(),
      .weekend = weekend.unwrap(),
  };

  // Steps 7-9.
  auto preferred = GetUnicodeExtension(locale, "fw");

  // FIXME: spec bug - [[FirstDayOfWeek]] can be undefined.
  //
  // https://github.com/tc39/ecma402/pull/1057
  mozilla::Maybe<mozilla::intl::Weekday> fw{};
  if (preferred.isSome()) {
    // Steps 8-9.
    fw = WeekdayUValueToNumber(*preferred);
  }

  // FIXME: spec bug - "ca" Unicode extension should apply.
  //
  // https://github.com/tc39/ecma402/issues/1055
  if (fw.isNothing() && regionOverride == RegionPref::RegionOverride::No) {
    auto calendar = GetUnicodeExtension(locale, "ca");
    if (calendar.isSome()) {
      // "iso8601" is the only calendar which specifies the first day.
      if (calendar->asSpan() == mozilla::MakeStringSpan("iso8601")) {
        fw = mozilla::Some(mozilla::intl::Weekday::Monday);
      }
    }
  }

  if (fw.isSome()) {
    info.firstDay = fw.value();
  }

  // Step 10.
  *result = info;
  return true;
}

// Intl.Locale.prototype.maximize ()
static bool Locale_maximize(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  Rooted<JSLinearString*> tagStr(cx, locale->getLanguageTag());

  mozilla::intl::Locale tag;
  if (!ParseLocale(cx, tagStr, tag)) {
    return false;
  }

  if (auto result = tag.AddLikelySubtags(); result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return false;
  }

  // Step 4.
  auto* result = CreateLocaleObject(cx, nullptr, tag);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

// Intl.Locale.prototype.maximize ()
static bool Locale_maximize(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_maximize>(cx, args);
}

// Intl.Locale.prototype.minimize ()
static bool Locale_minimize(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  Rooted<JSLinearString*> tagStr(cx, locale->getLanguageTag());

  mozilla::intl::Locale tag;
  if (!ParseLocale(cx, tagStr, tag)) {
    return false;
  }

  if (auto result = tag.RemoveLikelySubtags(); result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return false;
  }

  // Step 4.
  auto* result = CreateLocaleObject(cx, nullptr, tag);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

// Intl.Locale.prototype.minimize ()
static bool Locale_minimize(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_minimize>(cx, args);
}

// Intl.Locale.prototype.toString ()
static bool Locale_toString(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  args.rval().setString(locale->getLanguageTag());
  return true;
}

// Intl.Locale.prototype.toString ()
static bool Locale_toString(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_toString>(cx, args);
}

// get Intl.Locale.prototype.baseName
static bool Locale_baseName(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Steps 3-4.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  args.rval().setString(locale->getBaseName());
  return true;
}

// get Intl.Locale.prototype.baseName
static bool Locale_baseName(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_baseName>(cx, args);
}

// get Intl.Locale.prototype.calendar
static bool Locale_calendar(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  return GetUnicodeExtension(cx, locale, "ca", args.rval());
}

// get Intl.Locale.prototype.calendar
static bool Locale_calendar(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_calendar>(cx, args);
}

// get Intl.Locale.prototype.caseFirst
static bool Locale_caseFirst(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  return GetUnicodeExtension(cx, locale, "kf", args.rval());
}

// get Intl.Locale.prototype.caseFirst
static bool Locale_caseFirst(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_caseFirst>(cx, args);
}

// get Intl.Locale.prototype.collation
static bool Locale_collation(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  return GetUnicodeExtension(cx, locale, "co", args.rval());
}

// get Intl.Locale.prototype.collation
static bool Locale_collation(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_collation>(cx, args);
}

// get Intl.Locale.prototype.firstDayOfWeek
static bool Locale_firstDayOfWeek(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  return GetUnicodeExtension(cx, locale, "fw", args.rval());
}

// get Intl.Locale.prototype.firstDayOfWeek
static bool Locale_firstDayOfWeek(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_firstDayOfWeek>(cx, args);
}

// get Intl.Locale.prototype.hourCycle
static bool Locale_hourCycle(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  return GetUnicodeExtension(cx, locale, "hc", args.rval());
}

// get Intl.Locale.prototype.hourCycle
static bool Locale_hourCycle(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_hourCycle>(cx, args);
}

// get Intl.Locale.prototype.numeric
static bool Locale_numeric(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  Rooted<JS::Value> value(cx);
  if (!GetUnicodeExtension(cx, locale, "kn", &value)) {
    return false;
  }

  // Compare against the empty string per Intl.Locale, step 36.a. The Unicode
  // extension is already canonicalized, so we don't need to compare against
  // "true" at this point.
  MOZ_ASSERT(value.isUndefined() || value.isString());
  MOZ_ASSERT_IF(value.isString(),
                !StringEqualsLiteral(&value.toString()->asLinear(), "true"));

  args.rval().setBoolean(value.isString() && value.toString()->empty());
  return true;
}

// get Intl.Locale.prototype.numeric
static bool Locale_numeric(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_numeric>(cx, args);
}

// get Intl.Locale.prototype.numberingSystem
static bool Locale_numberingSystem(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  return GetUnicodeExtension(cx, locale, "nu", args.rval());
}

// get Intl.Locale.prototype.numberingSystem
static bool Locale_numberingSystem(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_numberingSystem>(cx, args);
}

// get Intl.Locale.prototype.language
static bool Locale_language(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  auto* baseName = locale->getBaseName();

  JSString* str;
  if (baseName->length() > 3) {
    str = NewStringCopy<CanGC>(cx, GetLocaleLanguage(locale).Span());
    if (!str) {
      return false;
    }
  } else {
    // Optimization for 2-3 letter language subtag-only locales.
    str = baseName;
  }

  args.rval().setString(str);
  return true;
}

// get Intl.Locale.prototype.language
static bool Locale_language(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_language>(cx, args);
}

// get Intl.Locale.prototype.script
static bool Locale_script(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  auto script = GetLocaleScript(locale);

  // No script subtag present.
  if (script.Missing()) {
    args.rval().setUndefined();
    return true;
  }

  auto* str = NewStringCopy<CanGC>(cx, script.Span());
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

// get Intl.Locale.prototype.script
static bool Locale_script(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_script>(cx, args);
}

// get Intl.Locale.prototype.region
static bool Locale_region(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  auto region = GetLocaleRegion(locale);

  // No region subtag present.
  if (region.Missing()) {
    args.rval().setUndefined();
    return true;
  }

  auto* str = NewStringCopy<CanGC>(cx, region.Span());
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

// get Intl.Locale.prototype.region
static bool Locale_region(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_region>(cx, args);
}

// get Intl.Locale.prototype.variants
static bool Locale_variants(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  // Step 3.
  auto* locale = &args.thisv().toObject().as<LocaleObject>();
  auto variants = GetLocaleVariants(locale);

  // No variant subtags present.
  if (!variants) {
    args.rval().setUndefined();
    return true;
  }

  auto [index, length] = *variants;
  MOZ_ASSERT(length >= 4, "variant subtag is at least four characters long");

  auto* baseName = locale->getBaseName();
  MOZ_ASSERT(index + length == baseName->length(),
             "unexpected base name subtags after variant subtags");

  auto* str = NewDependentString(cx, baseName, index, length);
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

// get Intl.Locale.prototype.variants
static bool Locale_variants(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_variants>(cx, args);
}

static bool Locale_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().Locale);
  return true;
}

// Intl.Locale.prototype.getCalendars ( )
static bool Locale_getCalendars(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  Rooted<LocaleObject*> locale(cx, &args.thisv().toObject().as<LocaleObject>());

  // Step 3.
  auto* result = CalendarsOfLocale(cx, locale);
  if (!result) {
    return false;
  }

  args.rval().setObject(*result);
  return true;
}

// Intl.Locale.prototype.getCalendars ( )
static bool Locale_getCalendars(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_getCalendars>(cx, args);
}

// Intl.Locale.prototype.getCollations ( )
static bool Locale_getCollations(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  Rooted<LocaleObject*> locale(cx, &args.thisv().toObject().as<LocaleObject>());

  // Step 3.
  auto* result = CollationsOfLocale(cx, locale);
  if (!result) {
    return false;
  }

  args.rval().setObject(*result);
  return true;
}

// Intl.Locale.prototype.getCollations ( )
static bool Locale_getCollations(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_getCollations>(cx, args);
}

// Intl.Locale.prototype.getHourCycles ( )
static bool Locale_getHourCycles(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  Rooted<LocaleObject*> locale(cx, &args.thisv().toObject().as<LocaleObject>());

  // Step 3.
  auto* result = HourCyclesOfLocale(cx, locale);
  if (!result) {
    return false;
  }

  args.rval().setObject(*result);
  return true;
}

// Intl.Locale.prototype.getHourCycles ( )
static bool Locale_getHourCycles(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_getHourCycles>(cx, args);
}

// Intl.Locale.prototype.getNumberingSystems ( )
static bool Locale_getNumberingSystems(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  Rooted<LocaleObject*> locale(cx, &args.thisv().toObject().as<LocaleObject>());

  // Step 3.
  auto* result = NumberingSystemsOfLocale(cx, locale);
  if (!result) {
    return false;
  }

  args.rval().setObject(*result);
  return true;
}

// Intl.Locale.prototype.getNumberingSystems ( )
static bool Locale_getNumberingSystems(JSContext* cx, unsigned argc,
                                       Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_getNumberingSystems>(cx, args);
}

// Intl.Locale.prototype.getTextInfo ( )
static bool Locale_getTextInfo(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  auto* locale = &args.thisv().toObject().as<LocaleObject>();

  // Step 3.
  Rooted<IdValueVector> info(cx, cx);

  // Step 4.
  auto dir = TextDirectionOfLocale(cx, locale);

  // Step 5.
  if (!info.emplaceBack(NameToId(cx->names().direction), dir)) {
    return false;
  }

  // Step 6.
  auto* result = NewPlainObjectWithUniqueNames(cx, info);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

// Intl.Locale.prototype.getTextInfo ( )
static bool Locale_getTextInfo(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_getTextInfo>(cx, args);
}

// Intl.Locale.prototype.getTimeZones ( )
static bool Locale_getTimeZones(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  auto* locale = &args.thisv().toObject().as<LocaleObject>();

  // Step 3.
  return TimeZonesOfLocale(cx, locale, args.rval());
}

// Intl.Locale.prototype.getTimeZones ( )
static bool Locale_getTimeZones(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_getTimeZones>(cx, args);
}

// Intl.Locale.prototype.getWeekInfo ( )
static bool Locale_getWeekInfo(JSContext* cx, const CallArgs& args) {
  MOZ_ASSERT(IsLocale(args.thisv()));

  Rooted<LocaleObject*> locale(cx, &args.thisv().toObject().as<LocaleObject>());

  // Step 3.
  Rooted<IdValueVector> info(cx, cx);

  // Step 4.
  WeekInfo wi;
  if (!WeekInfoOfLocale(cx, locale, &wi)) {
    return false;
  }

  // Step 5.
  if (!info.emplaceBack(NameToId(cx->names().firstDay),
                        JS::Int32Value(static_cast<int32_t>(wi.firstDay)))) {
    return false;
  }

  // Step 6.
  auto* weekend = NewDenseFullyAllocatedArray(cx, wi.weekend.size());
  if (!weekend) {
    return false;
  }
  weekend->setDenseInitializedLength(wi.weekend.size());

  size_t index = 0;
  for (auto day : wi.weekend) {
    weekend->initDenseElement(index++,
                              JS::Int32Value(static_cast<int32_t>(day)));
  }
  MOZ_ASSERT(index == wi.weekend.size());

  if (!info.emplaceBack(NameToId(cx->names().weekend), ObjectValue(*weekend))) {
    return false;
  }

  // Step 7.
  auto* result = NewPlainObjectWithUniqueNames(cx, info);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

// Intl.Locale.prototype.getWeekInfo ( )
static bool Locale_getWeekInfo(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsLocale, Locale_getWeekInfo>(cx, args);
}

static const JSFunctionSpec locale_methods[] = {
    JS_FN("maximize", Locale_maximize, 0, 0),
    JS_FN("minimize", Locale_minimize, 0, 0),
    JS_FN("toString", Locale_toString, 0, 0),
    JS_FN("toSource", Locale_toSource, 0, 0),
    JS_FN("getCalendars", Locale_getCalendars, 0, 0),
    JS_FN("getCollations", Locale_getCollations, 0, 0),
    JS_FN("getHourCycles", Locale_getHourCycles, 0, 0),
    JS_FN("getNumberingSystems", Locale_getNumberingSystems, 0, 0),
    JS_FN("getTextInfo", Locale_getTextInfo, 0, 0),
    JS_FN("getTimeZones", Locale_getTimeZones, 0, 0),
    JS_FN("getWeekInfo", Locale_getWeekInfo, 0, 0),
    JS_FS_END,
};

static const JSPropertySpec locale_properties[] = {
    JS_PSG("baseName", Locale_baseName, 0),
    JS_PSG("calendar", Locale_calendar, 0),
    JS_PSG("caseFirst", Locale_caseFirst, 0),
    JS_PSG("collation", Locale_collation, 0),
    JS_PSG("firstDayOfWeek", Locale_firstDayOfWeek, 0),
    JS_PSG("hourCycle", Locale_hourCycle, 0),
    JS_PSG("numeric", Locale_numeric, 0),
    JS_PSG("numberingSystem", Locale_numberingSystem, 0),
    JS_PSG("language", Locale_language, 0),
    JS_PSG("script", Locale_script, 0),
    JS_PSG("region", Locale_region, 0),
    JS_PSG("variants", Locale_variants, 0),
    JS_STRING_SYM_PS(toStringTag, "Intl.Locale", JSPROP_READONLY),
    JS_PS_END,
};

const ClassSpec LocaleObject::classSpec_ = {
    GenericCreateConstructor<Locale, 1, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<LocaleObject>,
    nullptr,
    nullptr,
    locale_methods,
    locale_properties,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

static JSLinearString* ValidateAndCanonicalizeLanguageTag(
    JSContext* cx, Handle<JSLinearString*> string) {
  // Handle the common case (a standalone language) first.
  // Only the following Unicode BCP 47 locale identifier subset is accepted:
  //   unicode_locale_id = unicode_language_id
  //   unicode_language_id = unicode_language_subtag
  //   unicode_language_subtag = alpha{2,3}
  JSLinearString* language;
  JS_TRY_VAR_OR_RETURN_NULL(cx, language,
                            ParseStandaloneISO639LanguageTag(cx, string));
  if (language) {
    return language;
  }

  mozilla::intl::Locale tag;
  if (!ParseLocale(cx, string, tag)) {
    return nullptr;
  }

  auto result = tag.Canonicalize();
  if (result.isErr()) {
    if (result.unwrapErr() ==
        mozilla::intl::Locale::CanonicalizationError::DuplicateVariant) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DUPLICATE_VARIANT_SUBTAG);
    } else {
      ReportInternalError(cx);
    }
    return nullptr;
  }

  FormatBuffer<char, INITIAL_CHAR_BUFFER_SIZE> buffer(cx);
  if (auto result = tag.ToString(buffer); result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }

  return buffer.toAsciiString(cx);
}

static JSLinearString* ValidateAndCanonicalizeLanguageTag(
    JSContext* cx, Handle<Value> tagValue) {
  if (tagValue.isObject()) {
    JSLinearString* tagStr;
    JS_TRY_VAR_OR_RETURN_NULL(
        cx, tagStr,
        LanguageTagFromMaybeWrappedLocale(cx, &tagValue.toObject()));
    if (tagStr) {
      return tagStr;
    }
  }

  JSString* tagStr = ToString(cx, tagValue);
  if (!tagStr) {
    return nullptr;
  }

  Rooted<JSLinearString*> tagLinearStr(cx, tagStr->ensureLinear(cx));
  if (!tagLinearStr) {
    return nullptr;
  }
  return ValidateAndCanonicalizeLanguageTag(cx, tagLinearStr);
}

/**
 * Canonicalizes a locale list.
 *
 * Spec: ECMAScript Internationalization API Specification, 9.2.1.
 */
bool js::intl::CanonicalizeLocaleList(JSContext* cx, Handle<Value> locales,
                                      MutableHandle<LocalesList> result) {
  MOZ_ASSERT(result.empty());

  // Step 1.
  if (locales.isUndefined()) {
    return true;
  }

  // Step 3 (and the remaining steps).
  if (locales.isString()) {
    Rooted<JSLinearString*> linear(cx, locales.toString()->ensureLinear(cx));
    if (!linear) {
      return false;
    }

    auto* languageTag = ValidateAndCanonicalizeLanguageTag(cx, linear);
    if (!languageTag) {
      return false;
    }
    return result.append(languageTag);
  }

  if (locales.isObject()) {
    JSLinearString* languageTag;
    JS_TRY_VAR_OR_RETURN_FALSE(
        cx, languageTag,
        LanguageTagFromMaybeWrappedLocale(cx, &locales.toObject()));
    if (languageTag) {
      return result.append(languageTag);
    }
  }

  // Step 2. (Implicit)

  // Step 4.
  Rooted<JSObject*> obj(cx, ToObject(cx, locales));
  if (!obj) {
    return false;
  }

  // Step 5.
  uint64_t length;
  if (!GetLengthProperty(cx, obj, &length)) {
    return false;
  }

  // Steps 6-7.
  Rooted<Value> value(cx);
  for (uint64_t k = 0; k < length; k++) {
    // Step 7.a-c.
    bool hole;
    if (!CheckForInterrupt(cx) ||
        !HasAndGetElement(cx, obj, k, &hole, &value)) {
      return false;
    }

    if (!hole) {
      // Step 7.c.ii.
      if (!value.isString() && !value.isObject()) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_INVALID_LOCALES_ELEMENT);
        return false;
      }

      // Step 7.c.iii-iv.
      JSLinearString* tag = ValidateAndCanonicalizeLanguageTag(cx, value);
      if (!tag) {
        return false;
      }

      // Step 7.c.v.
      bool addToResult =
          std::none_of(result.begin(), result.end(),
                       [tag](auto* other) { return EqualStrings(tag, other); });
      if (addToResult && !result.append(tag)) {
        return false;
      }
    }
  }

  // Step 8.
  return true;
}
