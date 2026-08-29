/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Intl.Collator implementation. */

#include "builtin/intl/Collator.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/Collator.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/UsingEnum.h"

#include "builtin/Array.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/LanguageTag.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/Packed.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "builtin/intl/SharedIntlData.h"
#include "gc/GCContext.h"
#include "js/PropertySpec.h"
#include "js/StableStringChars.h"
#include "js/TypeDecls.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"  // js::PlainObject
#include "vm/Runtime.h"
#include "vm/StringType.h"

#include "vm/GeckoProfiler-inl.h"
#include "vm/JSObject-inl.h"

using namespace js;
using namespace js::intl;

const JSClassOps CollatorObject::classOps_ = {
    .finalize = CollatorObject::finalize,
};

const JSClass CollatorObject::class_ = {
    "Intl.Collator",
    JSCLASS_HAS_RESERVED_SLOTS(CollatorObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_Collator) |
        JSCLASS_BACKGROUND_FINALIZE,
    &CollatorObject::classOps_,
    &CollatorObject::classSpec_,
};

const JSClass& CollatorObject::protoClass_ = PlainObject::class_;

static bool collator_supportedLocalesOf(JSContext* cx, unsigned argc,
                                        Value* vp);

static bool collator_compare(JSContext* cx, unsigned argc, Value* vp);

static bool collator_resolvedOptions(JSContext* cx, unsigned argc, Value* vp);

static bool collator_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().Collator);
  return true;
}

static const JSFunctionSpec collator_static_methods[] = {
    JS_FN("supportedLocalesOf", collator_supportedLocalesOf, 1, 0),
    JS_FS_END,
};

static const JSFunctionSpec collator_methods[] = {
    JS_FN("resolvedOptions", collator_resolvedOptions, 0, 0),
    JS_FN("toSource", collator_toSource, 0, 0),
    JS_FS_END,
};

static const JSPropertySpec collator_properties[] = {
    JS_PSG("compare", collator_compare, 0),
    JS_STRING_SYM_PS(toStringTag, "Intl.Collator", JSPROP_READONLY),
    JS_PS_END,
};

static bool Collator(JSContext* cx, unsigned argc, Value* vp);

const ClassSpec CollatorObject::classSpec_ = {
    GenericCreateConstructor<Collator, 0, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<CollatorObject>,
    collator_static_methods,
    nullptr,
    collator_methods,
    collator_properties,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

struct js::intl::CollatorOptions {
  enum class Usage : int8_t { Sort, Search };
  Usage usage = Usage::Sort;

  using Sensitivity = mozilla::intl::CollatorSensitivity;
  Sensitivity sensitivity = Sensitivity::Variant;

  using IgnorePunctuation = mozilla::intl::CollatorIgnorePunctuation;
  IgnorePunctuation ignorePunctuation = IgnorePunctuation::Locale;

  using Numeric = mozilla::intl::CollatorNumeric;
  Numeric numeric = Numeric::Locale;

  using CaseFirst = mozilla::intl::CollatorCaseFirst;
  CaseFirst caseFirst = CaseFirst::Locale;
};

struct PackedCollatorOptions {
  using RawValue = uint32_t;

  using UsageField = packed::EnumField<RawValue, CollatorOptions::Usage::Sort,
                                       CollatorOptions::Usage::Search>;

  using SensitivityField =
      packed::EnumField<UsageField, CollatorOptions::Sensitivity::Variant,
                        CollatorOptions::Sensitivity::Base>;

  using IgnorePunctuationField =
      packed::EnumField<SensitivityField,
                        CollatorOptions::IgnorePunctuation::Locale,
                        CollatorOptions::IgnorePunctuation::Off>;

  using NumericField = packed::EnumField<IgnorePunctuationField,
                                         CollatorOptions::Numeric::Locale,
                                         CollatorOptions::Numeric::Off>;

  using CaseFirstField =
      packed::EnumField<NumericField, CollatorOptions::CaseFirst::Locale,
                        CollatorOptions::CaseFirst::False>;

  using PackedValue = packed::PackedValue<CaseFirstField>;

  static auto pack(const CollatorOptions& options) {
    RawValue rawValue =
        UsageField::pack(options.usage) |
        SensitivityField::pack(options.sensitivity) |
        IgnorePunctuationField::pack(options.ignorePunctuation) |
        NumericField::pack(options.numeric) |
        CaseFirstField::pack(options.caseFirst);
    return PackedValue::toValue(rawValue);
  }

  static auto unpack(JS::Value value) {
    RawValue rawValue = PackedValue::fromValue(value);
    return CollatorOptions{
        .usage = UsageField::unpack(rawValue),
        .sensitivity = SensitivityField::unpack(rawValue),
        .ignorePunctuation = IgnorePunctuationField::unpack(rawValue),
        .numeric = NumericField::unpack(rawValue),
        .caseFirst = CaseFirstField::unpack(rawValue),
    };
  }
};

CollatorOptions js::intl::CollatorObject::getOptions() const {
  const auto& slot = getFixedSlot(OPTIONS_SLOT);
  if (slot.isUndefined()) {
    return {};
  }
  return PackedCollatorOptions::unpack(slot);
}

void js::intl::CollatorObject::setOptions(const CollatorOptions& options) {
  setFixedSlot(OPTIONS_SLOT, PackedCollatorOptions::pack(options));
}

static constexpr std::string_view UsageToString(CollatorOptions::Usage usage) {
  MOZ_USING_ENUM(CollatorOptions::Usage, Sort, Search);
  switch (usage) {
    case Sort:
      return "sort";
    case Search:
      return "search";
  }
  MOZ_CRASH("invalid collator usage");
}

static constexpr std::string_view SensitivityToString(
    CollatorOptions::Sensitivity sensitivity) {
  MOZ_USING_ENUM(CollatorOptions::Sensitivity, Base, Accent, Case, Variant);
  switch (sensitivity) {
    case Base:
      return "base";
    case Accent:
      return "accent";
    case Case:
      return "case";
    case Variant:
      return "variant";
  }
  MOZ_CRASH("invalid collator sensitivity");
}

static constexpr std::string_view CaseFirstToString(
    CollatorOptions::CaseFirst caseFirst) {
  MOZ_USING_ENUM(CollatorOptions::CaseFirst, False, Lower, Upper, Locale);
  switch (caseFirst) {
    case False:
      return "false";
    case Lower:
      return "lower";
    case Upper:
      return "upper";
    case Locale:
      MOZ_CRASH("expected to have resolved away this case");
  }
  MOZ_CRASH("invalid collator case first");
}

template <typename Option>
static auto MapBoolean(mozilla::Maybe<bool> option) {
  return option.isNothing() ? Option::Locale
         : *option          ? Option::On
                            : Option::Off;
}

/**
 * 10.1.2 Intl.Collator([ locales [, options]])
 *
 * ES2017 Intl draft rev 94045d234762ad107a3d09bb6f7381a65f1a2f9b
 */
static bool InitializeCollator(JSContext* cx, Handle<CollatorObject*> collator,
                               Handle<JS::Value> locales,
                               Handle<JS::Value> optionsValue) {
  // Steps 4-5.
  auto* requestedLocales = CanonicalizeLocaleList(cx, locales);
  if (!requestedLocales) {
    return false;
  }
  collator->setRequestedLocales(requestedLocales);

  CollatorOptions colOptions{};

  if (!optionsValue.isUndefined()) {
    // Step 6.
    Rooted<JSObject*> options(cx, JS::ToObject(cx, optionsValue));
    if (!options) {
      return false;
    }

    // Steps 7-8.
    static constexpr auto usages = MapOptions<UsageToString>(
        CollatorOptions::Usage::Sort, CollatorOptions::Usage::Search);
    if (!GetStringOption(cx, options, cx->names().usage, usages,
                         CollatorOptions::Usage::Sort, &colOptions.usage)) {
      return false;
    }

    // Step 9-10. (Performed in ResolveLocale)

    // Step 11. (Inlined ResolveOptions)

    // ResolveOptions, steps 1-3. (Already performed above)

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
    Rooted<JSLinearString*> collation(cx);
    if (!GetUnicodeExtensionOption(cx, options, UnicodeExtensionKey::Collation,
                                   &collation)) {
      return false;
    }
    if (collation) {
      collator->setCollation(collation);
    }

    // ResolveOptions, step 6.
    mozilla::Maybe<bool> numeric;
    if (!GetBooleanOption(cx, options, cx->names().numeric, &numeric)) {
      return false;
    }
    colOptions.numeric = MapBoolean<CollatorOptions::Numeric>(numeric);

    // ResolveOptions, step 6.
    mozilla::Maybe<CollatorOptions::CaseFirst> caseFirst;
    static constexpr auto caseFirsts = MapOptions<CaseFirstToString>(
        CollatorOptions::CaseFirst::Upper, CollatorOptions::CaseFirst::Lower,
        CollatorOptions::CaseFirst::False);
    if (!GetStringOption(cx, options, cx->names().caseFirst, caseFirsts,
                         &caseFirst)) {
      return false;
    }
    colOptions.caseFirst =
        caseFirst.valueOr(CollatorOptions::CaseFirst::Locale);

    // ResolveOptions, step 7. (Not applicable)

    // ResolveOptions, step 8. (Performed in ResolveLocale)

    // ResolveOptions, step 9. (Return)

    // Steps 12-19 and 21. (Performed in ResolveLocale)

    // Step 20.
    mozilla::Maybe<CollatorOptions::Sensitivity> sensitivity;
    static constexpr auto sensitivities =
        MapOptions<SensitivityToString>(CollatorOptions::Sensitivity::Base,
                                        CollatorOptions::Sensitivity::Accent,
                                        CollatorOptions::Sensitivity::Case,
                                        CollatorOptions::Sensitivity::Variant);
    if (!GetStringOption(cx, options, cx->names().sensitivity, sensitivities,
                         &sensitivity)) {
      return false;
    }

    // In theory the default sensitivity for the "search" collator is locale
    // dependent; in reality the CLDR/ICU default strength is always tertiary.
    // Therefore use "variant" as the default value for both collation modes.
    colOptions.sensitivity =
        sensitivity.valueOr(CollatorOptions::Sensitivity::Variant);

    // Step 22.
    mozilla::Maybe<bool> ignorePunctuation;
    if (!GetBooleanOption(cx, options, cx->names().ignorePunctuation,
                          &ignorePunctuation)) {
      return false;
    }
    colOptions.ignorePunctuation =
        MapBoolean<CollatorOptions::IgnorePunctuation>(ignorePunctuation);
  }
  collator->setOptions(colOptions);

  return true;
}

/**
 * 10.1.2 Intl.Collator([ locales [, options]])
 *
 * ES2017 Intl draft rev 94045d234762ad107a3d09bb6f7381a65f1a2f9b
 */
static bool Collator(JSContext* cx, unsigned argc, Value* vp) {
  AutoJSConstructorProfilerEntry pseudoFrame(cx, "Intl.Collator");
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1 (Handled by OrdinaryCreateFromConstructor fallback code).

  // Steps 2-3 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  Rooted<JSObject*> proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_Collator, &proto)) {
    return false;
  }

  Rooted<CollatorObject*> collator(
      cx, NewObjectWithClassProto<CollatorObject>(cx, proto));
  if (!collator) {
    return false;
  }

  // Steps 4-22.
  if (!InitializeCollator(cx, collator, args.get(0), args.get(1))) {
    return false;
  }

  // Step 23.
  args.rval().setObject(*collator);
  return true;
}

CollatorObject* js::intl::CreateCollator(JSContext* cx, Handle<Value> locales,
                                         Handle<Value> options) {
  Rooted<CollatorObject*> collator(cx,
                                   NewBuiltinClassInstance<CollatorObject>(cx));
  if (!collator) {
    return nullptr;
  }

  if (!InitializeCollator(cx, collator, locales, options)) {
    return nullptr;
  }
  return collator;
}

CollatorObject* js::intl::GetOrCreateCollator(JSContext* cx,
                                              Handle<Value> locales,
                                              Handle<Value> options) {
  // Try to use a cached instance when |locales| is either undefined or a
  // string, and |options| is undefined.
  if ((locales.isUndefined() || locales.isString()) && options.isUndefined()) {
    Rooted<JSLinearString*> locale(cx);
    if (locales.isString()) {
      locale = locales.toString()->ensureLinear(cx);
      if (!locale) {
        return nullptr;
      }
    }
    return cx->global()->globalIntlData().getOrCreateCollator(cx, locale);
  }

  // Create a new Intl.Collator instance.
  return CreateCollator(cx, locales, options);
}

void js::intl::CollatorObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  auto* collator = &obj->as<CollatorObject>();

  if (auto* coll = collator->getCollator()) {
    RemoveICUCellMemory(gcx, obj, CollatorObject::EstimatedMemoryUse);
    delete coll;
  }
}

/**
 * Resolve the actual locale to finish initialization of the Collator.
 */
static bool ResolveLocale(JSContext* cx, Handle<CollatorObject*> collator) {
  // Return if the locale was already resolved.
  if (collator->isLocaleResolved()) {
    return true;
  }
  auto colOptions = collator->getOptions();

  Rooted<ArrayObject*> requestedLocales(
      cx, &collator->getRequestedLocales()->as<ArrayObject>());

  // %Intl.Collator%.[[RelevantExtensionKeys]] is « "co", "kf", "kn" ».
  mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys{
      UnicodeExtensionKey::Collation,
      UnicodeExtensionKey::CollationCaseFirst,
      UnicodeExtensionKey::CollationNumeric,
  };

  // Initialize locale options from constructor arguments.
  Rooted<LocaleOptions> localeOptions(cx);
  if (auto* co = collator->getCollation()) {
    localeOptions.setUnicodeExtension(UnicodeExtensionKey::Collation, co);
  }
  if (colOptions.caseFirst != CollatorOptions::CaseFirst::Locale) {
    MOZ_USING_ENUM(CollatorOptions::CaseFirst, False, Lower, Upper, Locale);

    JSLinearString* kf;
    switch (colOptions.caseFirst) {
      case Upper:
        kf = cx->names().upper;
        break;
      case Lower:
        kf = cx->names().lower;
        break;
      case False:
        kf = cx->names().false_;
        break;
      case Locale:
        MOZ_CRASH("Unreachable switch-case");
    }
    localeOptions.setUnicodeExtension(UnicodeExtensionKey::CollationCaseFirst,
                                      kf);
  }
  if (colOptions.numeric != CollatorOptions::Numeric::Locale) {
    JSLinearString* kn = (colOptions.numeric == CollatorOptions::Numeric::On)
                             ? cx->names().true_
                             : cx->names().false_;
    localeOptions.setUnicodeExtension(UnicodeExtensionKey::CollationNumeric,
                                      kn);
  }

  // Use the default locale data for "sort" usage.
  // Use the collator search locale data for "search" usage.
  LocaleData localeData;
  if (colOptions.usage == CollatorOptions::Usage::Sort) {
    localeData = LocaleData::Default;
  } else {
    localeData = LocaleData::CollatorSearch;
  }

  // Resolve the actual locale.
  Rooted<ResolvedLocale> resolved(cx);
  if (!ResolveLocale(cx, AvailableLocaleKind::Collator, requestedLocales,
                     localeOptions, relevantExtensionKeys, localeData,
                     &resolved)) {
    return false;
  }

  // In theory the default sensitivity for the "search" collator is locale
  // dependent; in reality the CLDR/ICU default strength is always tertiary.
  // Therefore use "variant" as the default value for both collation modes,
  // which is why there is not a sensitivity handling step here.

  // Finish initialization by setting the actual locale and collation.
  auto* locale = resolved.toLocale(cx);
  if (!locale) {
    return false;
  }
  collator->setLocale(locale);

  if (auto co = resolved.extension(UnicodeExtensionKey::Collation)) {
    collator->setCollation(co);
  } else {
    // The first element of the collations array must be |null| per ES2026 Intl,
    // 10.2.3 Internal Slots. The |Intl.Collator| constructor maps |null| to
    // "default".
    collator->setCollation(cx->names().default_);
  }

  if (auto kf = resolved.extension(UnicodeExtensionKey::CollationCaseFirst)) {
    if (StringEqualsLiteral(kf, "upper")) {
      colOptions.caseFirst = CollatorOptions::CaseFirst::Upper;
    } else if (StringEqualsLiteral(kf, "lower")) {
      colOptions.caseFirst = CollatorOptions::CaseFirst::Lower;
    } else {
      MOZ_ASSERT(StringEqualsLiteral(kf, "false"));
      colOptions.caseFirst = CollatorOptions::CaseFirst::False;
    }
  } else {
    MOZ_ASSERT(colOptions.caseFirst == CollatorOptions::CaseFirst::Locale);
  }

  if (auto kn = resolved.extension(UnicodeExtensionKey::CollationNumeric)) {
    if (StringEqualsLiteral(kn, "true")) {
      colOptions.numeric = CollatorOptions::Numeric::On;
    } else {
      MOZ_ASSERT(StringEqualsLiteral(kn, "false"));
      colOptions.numeric = CollatorOptions::Numeric::Off;
    }
  } else {
    MOZ_ASSERT(colOptions.numeric == CollatorOptions::Numeric::Locale);
  }

  // Set the resolved options.
  collator->setOptions(colOptions);

  MOZ_ASSERT(collator->isLocaleResolved(), "locale successfully resolved");
  return true;
}

/**
 * Returns a new mozilla::intl::Collator with the locale and collation options
 * of the given Collator.
 */
static mozilla::intl::Collator* NewIntlCollator(
    JSContext* cx, Handle<CollatorObject*> collator) {
  if (!ResolveLocale(cx, collator)) {
    return nullptr;
  }
  auto colOptions = collator->getOptions();

  JS::RootedVector<UnicodeExtensionKeyword> keywords(cx);

  // ICU expects collation as Unicode locale extensions on locale.
  if (colOptions.usage == CollatorOptions::Usage::Search) {
    if (!keywords.emplaceBack("co", cx->names().search)) {
      return nullptr;
    }

    // Search collations can't select a different collation, so the collation
    // property is guaranteed to be "default".
    MOZ_ASSERT(collator->getCollation() == cx->names().default_);
  } else {
    auto* collation = collator->getCollation();

    // Set collation as a Unicode locale extension when it was specified.
    if (collation != cx->names().default_) {
      if (!keywords.emplaceBack("co", collation)) {
        return nullptr;
      }
    }
  }

  Rooted<JSLinearString*> localeStr(cx, collator->getLocale());
  auto locale = FormatLocale(cx, localeStr, keywords);
  if (!locale) {
    return nullptr;
  }

  mozilla::intl::CollatorOptions options = {
      .sensitivity = colOptions.sensitivity,
      .caseFirst = colOptions.caseFirst,
      .ignorePunctuation = colOptions.ignorePunctuation,
      .numeric = colOptions.numeric,
  };

  auto collResult = mozilla::intl::Collator::TryCreate(
      mozilla::MakeStringSpan(locale.get()), options);
  if (collResult.isErr()) {
    ReportInternalError(cx, collResult.unwrapErr());
    return nullptr;
  }
  auto coll = collResult.unwrap();

  return coll.release();
}

static mozilla::intl::Collator* GetOrCreateCollator(
    JSContext* cx, Handle<CollatorObject*> collator) {
  // Obtain a cached mozilla::intl::Collator object.
  if (auto* coll = collator->getCollator()) {
    return coll;
  }

  auto* coll = NewIntlCollator(cx, collator);
  if (!coll) {
    return nullptr;
  }
  collator->setCollator(coll);

  AddICUCellMemory(collator, CollatorObject::EstimatedMemoryUse);
  return coll;
}

static bool ResolvedOptions(JSContext* cx, Handle<CollatorObject*> collator,
                            CollatorOptions* result) {
  if (!ResolveLocale(cx, collator)) {
    return false;
  }
  auto options = collator->getOptions();

  // If any option is set to |Locale|, we need to construct a Collator to
  // determine the resolved options.
  if (options.ignorePunctuation == CollatorOptions::IgnorePunctuation::Locale ||
      options.numeric == CollatorOptions::Numeric::Locale ||
      options.caseFirst == CollatorOptions::CaseFirst::Locale) {
    auto* coll = GetOrCreateCollator(cx, collator);
    if (!coll) {
      return false;
    }
    auto resolvedOptions = coll->ResolvedOptions();

    // Assert all fixed input options match the resolved options.
    MOZ_ASSERT(options.sensitivity == resolvedOptions.sensitivity);
    MOZ_ASSERT_IF(
        options.ignorePunctuation != CollatorOptions::IgnorePunctuation::Locale,
        options.ignorePunctuation == resolvedOptions.ignorePunctuation);
    MOZ_ASSERT_IF(options.numeric != CollatorOptions::Numeric::Locale,
                  options.numeric == resolvedOptions.numeric);
    MOZ_ASSERT_IF(options.caseFirst != CollatorOptions::CaseFirst::Locale,
                  options.caseFirst == resolvedOptions.caseFirst);

    // Assert all options are resolved to a locale-specific value.
    MOZ_ASSERT(resolvedOptions.ignorePunctuation !=
               CollatorOptions::IgnorePunctuation::Locale);
    MOZ_ASSERT(resolvedOptions.numeric != CollatorOptions::Numeric::Locale);
    MOZ_ASSERT(resolvedOptions.caseFirst != CollatorOptions::CaseFirst::Locale);

    // Case first defaults to "false" for all search collations.
    MOZ_ASSERT_IF(
        options.usage == CollatorOptions::Usage::Search &&
            options.caseFirst == CollatorOptions::CaseFirst::Locale,
        resolvedOptions.caseFirst == CollatorOptions::CaseFirst::False);

    // Numeric defaults to "false" for all locales.
    MOZ_ASSERT_IF(options.numeric == CollatorOptions::Numeric::Locale,
                  resolvedOptions.numeric == CollatorOptions::Numeric::Off);

    options.ignorePunctuation = resolvedOptions.ignorePunctuation;
    options.numeric = resolvedOptions.numeric;
    options.caseFirst = resolvedOptions.caseFirst;

    // Set the resolved options.
    collator->setOptions(options);
  }

  *result = options;
  return true;
}

bool js::intl::CompareStrings(JSContext* cx, Handle<CollatorObject*> collator,
                              Handle<JSString*> str1, Handle<JSString*> str2,
                              MutableHandle<Value> result) {
  MOZ_ASSERT(str1);
  MOZ_ASSERT(str2);

  if (str1 == str2) {
    result.setInt32(0);
    return true;
  }

  auto* coll = GetOrCreateCollator(cx, collator);
  if (!coll) {
    return false;
  }

  JSLinearString* linear1 = str1->ensureLinear(cx);
  if (!linear1) {
    return false;
  }
  JSLinearString* linear2 = str2->ensureLinear(cx);
  if (!linear2) {
    return false;
  }

  int32_t ret;
  JS::AutoCheckCannotGC nogc;
  if (linear1->hasLatin1Chars()) {
    if (linear2->hasLatin1Chars()) {
      ret = coll->CompareLatin1(linear1->latin1Range(nogc),
                                linear2->latin1Range(nogc));
    } else {
      ret = coll->CompareLatin1UTF16(linear1->latin1Range(nogc),
                                     linear2->twoByteRange(nogc));
    }
  } else {
    if (linear2->hasTwoByteChars()) {
      ret = coll->CompareUTF16(linear1->twoByteRange(nogc),
                               linear2->twoByteRange(nogc));
    } else {
      // We don't have `CompareUTF16Latin1`, so we're responsible for flipping
      // the result here.
      ret = -(coll->CompareLatin1UTF16(linear2->latin1Range(nogc),
                                       linear1->twoByteRange(nogc)));
    }
  }

  result.setInt32(ret);
  return true;
}

static bool IsCollator(Handle<JS::Value> v) {
  return v.isObject() && v.toObject().is<CollatorObject>();
}

static constexpr uint32_t CollatorCompareFunction_Collator = 0;

/**
 * Collator Compare Functions
 */
static bool CollatorCompareFunction(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-2.
  auto* compare = &args.callee().as<JSFunction>();
  auto collValue = compare->getExtendedSlot(CollatorCompareFunction_Collator);
  Rooted<CollatorObject*> collator(cx,
                                   &collValue.toObject().as<CollatorObject>());

  // Step 3.
  Rooted<JSString*> x(cx, JS::ToString(cx, args.get(0)));
  if (!x) {
    return false;
  }

  // Step 4.
  Rooted<JSString*> y(cx, JS::ToString(cx, args.get(1)));
  if (!y) {
    return false;
  }

  // Step 5.
  return CompareStrings(cx, collator, x, y, args.rval());
}

/**
 * get Intl.Collator.prototype.compare
 */
static bool collator_compare(JSContext* cx, const CallArgs& args) {
  Rooted<CollatorObject*> collator(
      cx, &args.thisv().toObject().as<CollatorObject>());

  // Step 3.
  auto* boundCompare = collator->getBoundCompare();
  if (!boundCompare) {
    Handle<PropertyName*> funName = cx->names().empty_;
    auto* fn =
        NewNativeFunction(cx, CollatorCompareFunction, 2, funName,
                          gc::AllocKind::FUNCTION_EXTENDED, GenericObject);
    if (!fn) {
      return false;
    }
    fn->initExtendedSlot(CollatorCompareFunction_Collator,
                         ObjectValue(*collator));

    collator->setBoundCompare(fn);
    boundCompare = fn;
  }

  // Step 4.
  args.rval().setObject(*boundCompare);
  return true;
}

/**
 * get Intl.Collator.prototype.compare
 */
static bool collator_compare(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsCollator, collator_compare>(cx, args);
}

/**
 * Intl.Collator.prototype.resolvedOptions ( )
 */
static bool collator_resolvedOptions(JSContext* cx, const CallArgs& args) {
  Rooted<CollatorObject*> collator(
      cx, &args.thisv().toObject().as<CollatorObject>());

  CollatorOptions colOptions;
  if (!ResolvedOptions(cx, collator, &colOptions)) {
    return false;
  }

  // Step 3.
  Rooted<IdValueVector> options(cx, cx);

  // Step 4.
  if (!options.emplaceBack(NameToId(cx->names().locale),
                           StringValue(collator->getLocale()))) {
    return false;
  }

  auto* usage = NewStringCopy<CanGC>(cx, UsageToString(colOptions.usage));
  if (!usage) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().usage), StringValue(usage))) {
    return false;
  }

  auto* sensitivity =
      NewStringCopy<CanGC>(cx, SensitivityToString(colOptions.sensitivity));
  if (!sensitivity) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().sensitivity),
                           StringValue(sensitivity))) {
    return false;
  }

  if (!options.emplaceBack(
          NameToId(cx->names().ignorePunctuation),
          BooleanValue(colOptions.ignorePunctuation ==
                       CollatorOptions::IgnorePunctuation::On))) {
    return false;
  }

  if (!options.emplaceBack(NameToId(cx->names().collation),
                           StringValue(collator->getCollation()))) {
    return false;
  }

  if (!options.emplaceBack(
          NameToId(cx->names().numeric),
          BooleanValue(colOptions.numeric == CollatorOptions::Numeric::On))) {
    return false;
  }

  auto* caseFirst =
      NewStringCopy<CanGC>(cx, CaseFirstToString(colOptions.caseFirst));
  if (!caseFirst) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().caseFirst),
                           StringValue(caseFirst))) {
    return false;
  }

  // Step 5.
  auto* result = NewPlainObjectWithUniqueNames(cx, options);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

/**
 * Intl.Collator.prototype.resolvedOptions ( )
 */
static bool collator_resolvedOptions(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsCollator, collator_resolvedOptions>(cx, args);
}

/**
 * Intl.Collator.supportedLocalesOf ( locales [ , options ] )
 */
static bool collator_supportedLocalesOf(JSContext* cx, unsigned argc,
                                        Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-3.
  auto* array = SupportedLocalesOf(cx, AvailableLocaleKind::Collator,
                                   args.get(0), args.get(1));
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}
