/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Intl.PluralRules implementation. */

#include "builtin/intl/PluralRules.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/PluralRules.h"
#include "mozilla/UsingEnum.h"

#include "builtin/Array.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/IntlMathematicalValue.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/NumberFormatOptions.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "gc/GCContext.h"
#include "js/PropertySpec.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"
#include "vm/StringType.h"

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::intl;

const JSClassOps PluralRulesObject::classOps_ = {
    .finalize = PluralRulesObject::finalize,
};

const JSClass PluralRulesObject::class_ = {
    "Intl.PluralRules",
    JSCLASS_HAS_RESERVED_SLOTS(PluralRulesObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_PluralRules) |
        JSCLASS_BACKGROUND_FINALIZE,
    &PluralRulesObject::classOps_,
    &PluralRulesObject::classSpec_,
};

const JSClass& PluralRulesObject::protoClass_ = PlainObject::class_;

static bool pluralRules_supportedLocalesOf(JSContext* cx, unsigned argc,
                                           Value* vp);

static bool pluralRules_select(JSContext* cx, unsigned argc, Value* vp);

static bool pluralRules_selectRange(JSContext* cx, unsigned argc, Value* vp);

static bool pluralRules_resolvedOptions(JSContext* cx, unsigned argc,
                                        Value* vp);

static bool pluralRules_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().PluralRules);
  return true;
}

static const JSFunctionSpec pluralRules_static_methods[] = {
    JS_FN("supportedLocalesOf", pluralRules_supportedLocalesOf, 1, 0),
    JS_FS_END,
};

static const JSFunctionSpec pluralRules_methods[] = {
    JS_FN("resolvedOptions", pluralRules_resolvedOptions, 0, 0),
    JS_FN("select", pluralRules_select, 1, 0),
    JS_FN("selectRange", pluralRules_selectRange, 2, 0),
    JS_FN("toSource", pluralRules_toSource, 0, 0),
    JS_FS_END,
};

static const JSPropertySpec pluralRules_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "Intl.PluralRules", JSPROP_READONLY),
    JS_PS_END,
};

static bool PluralRules(JSContext* cx, unsigned argc, Value* vp);

const ClassSpec PluralRulesObject::classSpec_ = {
    GenericCreateConstructor<PluralRules, 0, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<PluralRulesObject>,
    pluralRules_static_methods,
    nullptr,
    pluralRules_methods,
    pluralRules_properties,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

PluralRulesOptions js::intl::PluralRulesObject::getOptions() const {
  const auto& slot = getFixedSlot(OPTIONS_SLOT);
  if (slot.isUndefined()) {
    return {};
  }
  return PackedPluralRulesOptions::unpack(slot);
}

void js::intl::PluralRulesObject::setOptions(
    const PluralRulesOptions& options) {
  setFixedSlot(OPTIONS_SLOT, PackedPluralRulesOptions::pack(options));
}

static constexpr std::string_view PluralRulesTypeToString(
    PluralRulesOptions::Type type) {
  MOZ_USING_ENUM(PluralRulesOptions::Type, Cardinal, Ordinal);
  switch (type) {
    case Cardinal:
      return "cardinal";
    case Ordinal:
      return "ordinal";
  }
  MOZ_CRASH("invalid plural rules type");
}

static constexpr std::string_view PluralRulesNotationToString(
    PluralRulesOptions::Notation notation) {
  MOZ_USING_ENUM(PluralRulesOptions::Notation, Standard, Scientific,
                 Engineering, Compact);
  switch (notation) {
    case Standard:
      return "standard";
    case Scientific:
      return "scientific";
    case Engineering:
      return "engineering";
    case Compact:
      return "compact";
  }
  MOZ_CRASH("invalid plural rules notation");
}

static constexpr std::string_view PluralRulesCompactDisplayToString(
    PluralRulesOptions::CompactDisplay compactDisplay) {
  MOZ_USING_ENUM(PluralRulesOptions::CompactDisplay, Short, Long);
  switch (compactDisplay) {
    case Short:
      return "short";
    case Long:
      return "long";
  }
  MOZ_CRASH("invalid plural rules compact display");
}

/**
 * 16.1.1 Intl.PluralRules ( [ locales [ , options ] ] )
 *
 * ES2024 Intl draft rev 74ca7099f103d143431b2ea422ae640c6f43e3e6
 */
static bool PluralRules(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  if (!ThrowIfNotConstructing(cx, args, "Intl.PluralRules")) {
    return false;
  }

  // Step 2 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  Rooted<JSObject*> proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_PluralRules,
                                          &proto)) {
    return false;
  }

  Rooted<PluralRulesObject*> pluralRules(cx);
  pluralRules = NewObjectWithClassProto<PluralRulesObject>(cx, proto);
  if (!pluralRules) {
    return false;
  }

  // ResolveOptions, step 1.
  auto* requestedLocales = CanonicalizeLocaleList(cx, args.get(0));
  if (!requestedLocales) {
    return false;
  }
  pluralRules->setRequestedLocales(requestedLocales);

  PluralRulesOptions plOptions{};

  if (args.hasDefined(1)) {
    // ResolveOptions, steps 2-3.
    Rooted<JSObject*> options(cx, JS::ToObject(cx, args[1]));
    if (!options) {
      return false;
    }

    // ResolveOptions, step 4.
    LocaleMatcher matcher;
    if (!GetLocaleMatcherOption(cx, options, &matcher)) {
      return false;
    }

    // ResolveOptions, step 5.
    //
    // This implementation only supports the "lookup" locale matcher, therefore
    // the "localeMatcher" option doesn't need to be stored.

    // ResolveOptions, step 6.
    //
    // Intl.PluralRules doesn't support any Unicode extension keys.

    // ResolveOptions, step 7. (Not applicable)

    // ResolveOptions, step 8. (Performed in ResolveLocale)

    // ResolveOptions, step 9. (Return)

    // Step 4. (Not applicable when ResolveOptions is inlined.)

    // Step 5-6. (Performed in ResolveLocale)

    // Steps 7-8.
    static constexpr auto types = MapOptions<PluralRulesTypeToString>(
        PluralRulesOptions::Type::Cardinal, PluralRulesOptions::Type::Ordinal);
    if (!GetStringOption(cx, options, cx->names().type, types,
                         PluralRulesOptions::Type::Cardinal, &plOptions.type)) {
      return false;
    }

    // Steps 9-10.
    static constexpr auto notations = MapOptions<PluralRulesNotationToString>(
        PluralRulesOptions::Notation::Standard,
        PluralRulesOptions::Notation::Scientific,
        PluralRulesOptions::Notation::Engineering,
        PluralRulesOptions::Notation::Compact);
    if (!GetStringOption(cx, options, cx->names().notation, notations,
                         NumberFormatOptions::Notation::Standard,
                         &plOptions.notation)) {
      return false;
    }

    // Steps 11-12.
    static constexpr auto compactDisplays =
        MapOptions<PluralRulesCompactDisplayToString>(
            PluralRulesOptions::CompactDisplay::Short,
            PluralRulesOptions::CompactDisplay::Long);
    if (!GetStringOption(cx, options, cx->names().compactDisplay,
                         compactDisplays,
                         PluralRulesOptions::CompactDisplay::Short,
                         &plOptions.compactDisplay)) {
      return false;
    }

    // Step 13.
    if (!SetNumberFormatDigitOptions(cx, plOptions.digitOptions, options, 0, 3,
                                     plOptions.notation)) {
      return false;
    }
  } else {
    static constexpr PluralRulesOptions defaultOptions = {
        .digitOptions = NumberFormatDigitOptions::defaultOptions(),
        .type = PluralRulesOptions::Type::Cardinal,
        .notation = PluralRulesOptions::Notation::Standard,
    };

    // Initialize using the default plural rules options.
    plOptions = defaultOptions;
  }
  pluralRules->setOptions(plOptions);

  // Step 14.
  args.rval().setObject(*pluralRules);
  return true;
}

void js::intl::PluralRulesObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  auto* pluralRules = &obj->as<PluralRulesObject>();

  if (auto* pr = pluralRules->getPluralRules()) {
    RemoveICUCellMemory(gcx, obj,
                        PluralRulesObject::UPluralRulesEstimatedMemoryUse);
    delete pr;
  }
}

/**
 * Resolve the actual locale to finish initialization of the PluralRules.
 */
static bool ResolveLocale(JSContext* cx,
                          Handle<PluralRulesObject*> pluralRules) {
  // Return if the locale was already resolved.
  if (pluralRules->isLocaleResolved()) {
    return true;
  }

  Rooted<ArrayObject*> requestedLocales(
      cx, &pluralRules->getRequestedLocales()->as<ArrayObject>());

  // %Intl.PluralRules%.[[RelevantExtensionKeys]] is « ».
  mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys{};

  // Initialize locale options from constructor arguments.
  Rooted<LocaleOptions> localeOptions(cx);

  // Use the default locale data.
  auto localeData = LocaleData::Default;

  // Resolve the actual locale.
  Rooted<ResolvedLocale> resolved(cx);
  if (!ResolveLocale(cx, AvailableLocaleKind::PluralRules, requestedLocales,
                     localeOptions, relevantExtensionKeys, localeData,
                     &resolved)) {
    return false;
  }

  // Finish initialization by setting the actual locale.
  auto* locale = resolved.toLocale(cx);
  if (!locale) {
    return false;
  }
  pluralRules->setLocale(locale);

  MOZ_ASSERT(pluralRules->isLocaleResolved(), "locale successfully resolved");
  return true;
}

static JSString* KeywordToString(mozilla::intl::PluralRules::Keyword keyword,
                                 JSContext* cx) {
  MOZ_USING_ENUM(mozilla::intl::PluralRules::Keyword, Zero, One, Two, Few, Many,
                 Other);
  switch (keyword) {
    case Zero:
      return cx->names().zero;
    case One:
      return cx->names().one;
    case Two:
      return cx->names().two;
    case Few:
      return cx->names().few;
    case Many:
      return cx->names().many;
    case Other:
      return cx->names().other;
  }
  MOZ_CRASH("Unexpected PluralRules keyword");
}

/**
 * Returns a new intl::PluralRules with the locale and type options of the given
 * PluralRules.
 */
static mozilla::intl::PluralRules* NewPluralRules(
    JSContext* cx, Handle<PluralRulesObject*> pluralRules) {
  if (!ResolveLocale(cx, pluralRules)) {
    return nullptr;
  }
  auto plOptions = pluralRules->getOptions();

  auto locale = EncodeLocale(cx, pluralRules->getLocale());
  if (!locale) {
    return nullptr;
  }

  mozilla::intl::PluralRulesOptions options = {
      .mPluralType = plOptions.type,
  };
  SetPluralRulesOptions(plOptions, options);

  auto result = mozilla::intl::PluralRules::TryCreate(locale.get(), options);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  return result.unwrap().release();
}

static mozilla::intl::PluralRules* GetOrCreatePluralRules(
    JSContext* cx, Handle<PluralRulesObject*> pluralRules) {
  // Obtain a cached PluralRules object.
  if (auto* pr = pluralRules->getPluralRules()) {
    return pr;
  }

  auto* pr = NewPluralRules(cx, pluralRules);
  if (!pr) {
    return nullptr;
  }
  pluralRules->setPluralRules(pr);

  AddICUCellMemory(pluralRules,
                   PluralRulesObject::UPluralRulesEstimatedMemoryUse);
  return pr;
}

/**
 * ResolvePlural ( pluralRules, n )
 * PluralRuleSelect ( locale, type, notation, compactDisplay, s )
 */
static auto ResolvePlural(JSContext* cx,
                          const mozilla::intl::PluralRules* pluralRules,
                          Handle<IntlMathematicalValue> value)
    -> decltype(pluralRules->Select(0)) {
  double x;
  if (value.isRepresentableAsDouble(&x)) {
    return pluralRules->Select(x);
  }

  auto str = value.toString(cx);
  if (!str) {
    return mozilla::Err(mozilla::intl::ICUError::OutOfMemory);
  }

  JS::AutoCheckCannotGC nogc;

  auto view = str.asView(cx, nogc);
  if (!view) {
    return mozilla::Err(mozilla::intl::ICUError::OutOfMemory);
  }
  return pluralRules->Select(view);
}

/**
 * ResolvePlural ( pluralRules, n )
 * PluralRuleSelect ( locale, type, notation, compactDisplay, s )
 *
 * Returns a plural rule for the Intl mathematical value `n` according to the
 * effective locale and formatting options of the given PluralRules.
 *
 * A plural rule is a grammatical category that expresses count distinctions
 * (such as "one", "two", "few" etc.).
 */
static JSString* ResolvePlural(JSContext* cx,
                               Handle<PluralRulesObject*> pluralRules,
                               Handle<IntlMathematicalValue> n) {
  // Steps 1-11.
  auto* pr = GetOrCreatePluralRules(cx, pluralRules);
  if (!pr) {
    return nullptr;
  }

  auto keywordResult = ResolvePlural(cx, pr, n);
  if (keywordResult.isErr()) {
    ReportInternalError(cx, keywordResult.unwrapErr());
    return nullptr;
  }

  return KeywordToString(keywordResult.unwrap(), cx);
}

/**
 * ResolvePluralRange ( pluralRules, x, y )
 * PluralRuleSelectRange ( locale, type, notation, compactDisplay, xp, yp )
 */
static auto ResolvePluralRange(JSContext* cx,
                               const mozilla::intl::PluralRules* pluralRules,
                               Handle<IntlMathematicalValue> start,
                               Handle<IntlMathematicalValue> end)
    -> decltype(pluralRules->SelectRange(0, 0)) {
  double x, y;
  if (start.isRepresentableAsDouble(&x) && end.isRepresentableAsDouble(&y)) {
    return pluralRules->SelectRange(x, y);
  }

  Rooted<IntlMathematicalValueString> strStart(cx, start.toString(cx));
  if (!strStart) {
    return mozilla::Err(mozilla::intl::ICUError::OutOfMemory);
  }

  Rooted<IntlMathematicalValueString> strEnd(cx, end.toString(cx));
  if (!strEnd) {
    return mozilla::Err(mozilla::intl::ICUError::OutOfMemory);
  }

  JS::AutoCheckCannotGC nogc;

  auto viewStart = strStart.asView(cx, nogc);
  if (!viewStart) {
    return mozilla::Err(mozilla::intl::ICUError::OutOfMemory);
  }

  auto viewEnd = strEnd.asView(cx, nogc);
  if (!viewEnd) {
    return mozilla::Err(mozilla::intl::ICUError::OutOfMemory);
  }

  return pluralRules->SelectRange(viewStart, viewEnd);
}

/**
 * ResolvePluralRange ( pluralRules, x, y )
 * PluralRuleSelectRange ( locale, type, notation, compactDisplay, xp, yp )
 *
 * Returns a plural rule for the number range «x - y» according to the effective
 * locale and formatting options of the given PluralRules.
 *
 * A plural rule is a grammatical category that expresses count distinctions
 * (such as "one", "two", "few" etc.).
 */
static JSString* ResolvePluralRange(JSContext* cx,
                                    Handle<PluralRulesObject*> pluralRules,
                                    Handle<IntlMathematicalValue> start,
                                    Handle<IntlMathematicalValue> end) {
  // Step 1.
  if (start.isNaN() || end.isNaN()) {
    const char* which = start.isNaN() ? "start" : "end";
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_NAN_NUMBER_RANGE, which, "PluralRules",
                              "selectRange");
    return nullptr;
  }

  // Steps 2-9.
  auto* pr = GetOrCreatePluralRules(cx, pluralRules);
  if (!pr) {
    return nullptr;
  }

  auto keywordResult = ResolvePluralRange(cx, pr, start, end);
  if (keywordResult.isErr()) {
    ReportInternalError(cx, keywordResult.unwrapErr());
    return nullptr;
  }

  return KeywordToString(keywordResult.unwrap(), cx);
}

/**
 * Returns an array of plural rules categories for a given pluralRules object.
 *
 * Usage: categories = intl_GetPluralCategories(pluralRules)
 *
 * Example:
 *
 * pluralRules = new Intl.PluralRules('pl', {type: 'cardinal'});
 * intl_getPluralCategories(pluralRules); // ['one', 'few', 'many', 'other']
 */
static ArrayObject* GetPluralCategories(
    JSContext* cx, Handle<PluralRulesObject*> pluralRules) {
  auto* pr = GetOrCreatePluralRules(cx, pluralRules);
  if (!pr) {
    return nullptr;
  }

  auto categoriesResult = pr->Categories();
  if (categoriesResult.isErr()) {
    ReportInternalError(cx, categoriesResult.unwrapErr());
    return nullptr;
  }
  auto categories = categoriesResult.unwrap();

  auto* res = NewDenseFullyAllocatedArray(cx, categories.size());
  if (!res) {
    return nullptr;
  }
  res->setDenseInitializedLength(categories.size());

  using PluralRules = mozilla::intl::PluralRules;

  size_t index = 0;
  for (auto keyword : {
           PluralRules::Keyword::Zero,
           PluralRules::Keyword::One,
           PluralRules::Keyword::Two,
           PluralRules::Keyword::Few,
           PluralRules::Keyword::Many,
           PluralRules::Keyword::Other,
       }) {
    if (categories.contains(keyword)) {
      auto* str = KeywordToString(keyword, cx);
      MOZ_ASSERT(str);

      res->initDenseElement(index++, StringValue(str));
    }
  }
  MOZ_ASSERT(index == categories.size());

  return res;
}

static bool IsPluralRules(Handle<JS::Value> v) {
  return v.isObject() && v.toObject().is<PluralRulesObject>();
}

/**
 * Intl.PluralRules.prototype.select ( value )
 */
static bool pluralRules_select(JSContext* cx, const CallArgs& args) {
  Rooted<PluralRulesObject*> pluralRules(
      cx, &args.thisv().toObject().as<PluralRulesObject>());

  // Step 3.
  Rooted<IntlMathematicalValue> n(cx);
  if (!ToIntlMathematicalValue(cx, args.get(0), &n)) {
    return false;
  }

  // Step 4.
  auto* result = ResolvePlural(cx, pluralRules, n);
  if (!result) {
    return false;
  }
  args.rval().setString(result);
  return true;
}

/**
 * Intl.PluralRules.prototype.select ( value )
 */
static bool pluralRules_select(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsPluralRules, pluralRules_select>(cx, args);
}

/**
 * Intl.PluralRules.prototype.selectRange ( start, end )
 */
static bool pluralRules_selectRange(JSContext* cx, const CallArgs& args) {
  Rooted<PluralRulesObject*> pluralRules(
      cx, &args.thisv().toObject().as<PluralRulesObject>());

  // Step 3.
  if (!args.hasDefined(0) || !args.hasDefined(1)) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_UNDEFINED_NUMBER,
        !args.hasDefined(0) ? "start" : "end", "PluralRules", "selectRange");
    return false;
  }

  // Step 4.
  Rooted<IntlMathematicalValue> x(cx);
  if (!ToIntlMathematicalValue(cx, args[0], &x)) {
    return false;
  }

  // Step 5.
  Rooted<IntlMathematicalValue> y(cx);
  if (!ToIntlMathematicalValue(cx, args[1], &y)) {
    return false;
  }

  // Step 6.
  auto* result = ResolvePluralRange(cx, pluralRules, x, y);
  if (!result) {
    return false;
  }
  args.rval().setString(result);
  return true;
}

/**
 * Intl.PluralRules.prototype.selectRange ( start, end )
 */
static bool pluralRules_selectRange(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsPluralRules, pluralRules_selectRange>(cx, args);
}

/**
 * Intl.PluralRules.prototype.resolvedOptions ( )
 */
static bool pluralRules_resolvedOptions(JSContext* cx, const CallArgs& args) {
  Rooted<PluralRulesObject*> pluralRules(
      cx, &args.thisv().toObject().as<PluralRulesObject>());

  if (!ResolveLocale(cx, pluralRules)) {
    return false;
  }
  auto plOptions = pluralRules->getOptions();

  // Step 4. (Reordered)
  Rooted<ArrayObject*> pluralCategories(cx,
                                        GetPluralCategories(cx, pluralRules));
  if (!pluralCategories) {
    return false;
  }

  // Step 3.
  Rooted<IdValueVector> options(cx, cx);

  // Step 5.
  if (!options.emplaceBack(NameToId(cx->names().locale),
                           StringValue(pluralRules->getLocale()))) {
    return false;
  }

  auto* type =
      NewStringCopy<CanGC>(cx, PluralRulesTypeToString(plOptions.type));
  if (!type) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().type), StringValue(type))) {
    return false;
  }

  if (!ResolvePluralRulesOptions(cx, plOptions, pluralCategories, &options)) {
    return false;
  }

  // Step 6.
  auto* result = NewPlainObjectWithUniqueNames(cx, options);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

/**
 * Intl.PluralRules.prototype.resolvedOptions ( )
 */
static bool pluralRules_resolvedOptions(JSContext* cx, unsigned argc,
                                        Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsPluralRules, pluralRules_resolvedOptions>(cx,
                                                                          args);
}

/**
 * Intl.PluralRules.supportedLocalesOf ( locales [ , options ] )
 */
static bool pluralRules_supportedLocalesOf(JSContext* cx, unsigned argc,
                                           Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-3.
  auto* array = SupportedLocalesOf(cx, AvailableLocaleKind::PluralRules,
                                   args.get(0), args.get(1));
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}
