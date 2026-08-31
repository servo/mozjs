/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/intl/ListFormat.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/ListFormat.h"
#include "mozilla/UsingEnum.h"

#include <stddef.h>

#include "builtin/Array.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/Packed.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "gc/GCContext.h"
#include "js/ForOfIterator.h"
#include "js/Utility.h"
#include "js/Vector.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"
#include "vm/StringType.h"

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"
#include "vm/ObjectOperations-inl.h"

using namespace js;
using namespace js::intl;

const JSClassOps ListFormatObject::classOps_ = {
    .finalize = ListFormatObject::finalize,
};
const JSClass ListFormatObject::class_ = {
    "Intl.ListFormat",
    JSCLASS_HAS_RESERVED_SLOTS(ListFormatObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_ListFormat) |
        JSCLASS_BACKGROUND_FINALIZE,
    &ListFormatObject::classOps_,
    &ListFormatObject::classSpec_,
};

const JSClass& ListFormatObject::protoClass_ = PlainObject::class_;

static bool listFormat_supportedLocalesOf(JSContext* cx, unsigned argc,
                                          Value* vp);

static bool listFormat_format(JSContext* cx, unsigned argc, Value* vp);

static bool listFormat_formatToParts(JSContext* cx, unsigned argc, Value* vp);

static bool listFormat_resolvedOptions(JSContext* cx, unsigned argc, Value* vp);

static bool listFormat_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().ListFormat);
  return true;
}

static const JSFunctionSpec listFormat_static_methods[] = {
    JS_FN("supportedLocalesOf", listFormat_supportedLocalesOf, 1, 0),
    JS_FS_END,
};

static const JSFunctionSpec listFormat_methods[] = {
    JS_FN("resolvedOptions", listFormat_resolvedOptions, 0, 0),
    JS_FN("format", listFormat_format, 1, 0),
    JS_FN("formatToParts", listFormat_formatToParts, 1, 0),
    JS_FN("toSource", listFormat_toSource, 0, 0),
    JS_FS_END,
};

static const JSPropertySpec listFormat_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "Intl.ListFormat", JSPROP_READONLY),
    JS_PS_END,
};

static bool ListFormat(JSContext* cx, unsigned argc, Value* vp);

const ClassSpec ListFormatObject::classSpec_ = {
    GenericCreateConstructor<ListFormat, 0, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<ListFormatObject>,
    listFormat_static_methods,
    nullptr,
    listFormat_methods,
    listFormat_properties,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

struct js::intl::ListFormatOptions {
  using Type = mozilla::intl::ListFormat::Type;
  Type type = Type::Conjunction;

  using Style = mozilla::intl::ListFormat::Style;
  Style style = Style::Long;
};

struct PackedListFormatOptions {
  using RawValue = uint32_t;

  using TypeField =
      packed::EnumField<RawValue, ListFormatOptions::Type::Conjunction,
                        ListFormatOptions::Type::Unit>;

  using StyleField =
      packed::EnumField<TypeField, ListFormatOptions::Style::Long,
                        ListFormatOptions::Style::Narrow>;

  using PackedValue = packed::PackedValue<StyleField>;

  static auto pack(const ListFormatOptions& options) {
    RawValue rawValue =
        TypeField::pack(options.type) | StyleField::pack(options.style);
    return PackedValue::toValue(rawValue);
  }

  static auto unpack(JS::Value value) {
    RawValue rawValue = PackedValue::fromValue(value);
    return ListFormatOptions{
        .type = TypeField::unpack(rawValue),
        .style = StyleField::unpack(rawValue),
    };
  }
};

ListFormatOptions js::intl::ListFormatObject::getOptions() const {
  const auto& slot = getFixedSlot(OPTIONS);
  if (slot.isUndefined()) {
    return {};
  }
  return PackedListFormatOptions::unpack(slot);
}

void js::intl::ListFormatObject::setOptions(const ListFormatOptions& options) {
  setFixedSlot(OPTIONS, PackedListFormatOptions::pack(options));
}

static constexpr std::string_view ListFormatTypeToString(
    ListFormatOptions::Type type) {
  MOZ_USING_ENUM(ListFormatOptions::Type, Conjunction, Disjunction, Unit);
  switch (type) {
    case Conjunction:
      return "conjunction";
    case Disjunction:
      return "disjunction";
    case Unit:
      return "unit";
  }
  MOZ_CRASH("invalid list format type");
}

static constexpr std::string_view ListFormatStyleToString(
    ListFormatOptions::Style style) {
  MOZ_USING_ENUM(ListFormatOptions::Style, Long, Short, Narrow);
  switch (style) {
    case Long:
      return "long";
    case Short:
      return "short";
    case Narrow:
      return "narrow";
  }
  MOZ_CRASH("invalid list format style");
}

/**
 * Intl.ListFormat([ locales [, options]])
 */
static bool ListFormat(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  if (!ThrowIfNotConstructing(cx, args, "Intl.ListFormat")) {
    return false;
  }

  // Step 2 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  Rooted<JSObject*> proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_ListFormat,
                                          &proto)) {
    return false;
  }

  Rooted<ListFormatObject*> listFormat(
      cx, NewObjectWithClassProto<ListFormatObject>(cx, proto));
  if (!listFormat) {
    return false;
  }

  // Step 3. (Inlined ResolveOptions)

  // ResolveOptions, step 1.
  auto* requestedLocales = CanonicalizeLocaleList(cx, args.get(0));
  if (!requestedLocales) {
    return false;
  }
  listFormat->setRequestedLocales(requestedLocales);

  ListFormatOptions lfOptions{};

  if (args.hasDefined(1)) {
    // ResolveOptions, steps 2-3.
    Rooted<JSObject*> options(
        cx, RequireObjectArg(cx, "options", "Intl.ListFormat", args[1]));
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
    // Intl.ListFormat doesn't support any Unicode extension keys.

    // ResolveOptions, step 7. (Not applicable)

    // ResolveOptions, step 8. (Performed in ResolveLocale)

    // ResolveOptions, step 9. (Return)

    // Step 4. (Not applicable when ResolveOptions is inlined.)

    // Steps 5-6. (Performed in ResolveLocale)

    // Steps 7-8.
    static constexpr auto types = MapOptions<ListFormatTypeToString>(
        ListFormatOptions::Type::Conjunction,
        ListFormatOptions::Type::Disjunction, ListFormatOptions::Type::Unit);
    if (!GetStringOption(cx, options, cx->names().type, types,
                         ListFormatOptions::Type::Conjunction,
                         &lfOptions.type)) {
      return false;
    }

    // Steps 9-10.
    static constexpr auto styles = MapOptions<ListFormatStyleToString>(
        ListFormatOptions::Style::Long, ListFormatOptions::Style::Short,
        ListFormatOptions::Style::Narrow);
    if (!GetStringOption(cx, options, cx->names().style, styles,
                         ListFormatOptions::Style::Long, &lfOptions.style)) {
      return false;
    }
  }
  listFormat->setOptions(lfOptions);

  // Steps 11-13. (Not applicable in our implementation.)

  // Step 14.
  args.rval().setObject(*listFormat);
  return true;
}

void js::intl::ListFormatObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  auto* listFormat = &obj->as<ListFormatObject>();

  if (auto* lf = listFormat->getListFormatSlot()) {
    RemoveICUCellMemory(gcx, obj, ListFormatObject::EstimatedMemoryUse);
    delete lf;
  }
}

/**
 * Resolve the actual locale to finish initialization of the ListFormat.
 */
static bool ResolveLocale(JSContext* cx, Handle<ListFormatObject*> listFormat) {
  // Return if the locale was already resolved.
  if (listFormat->isLocaleResolved()) {
    return true;
  }

  Rooted<ArrayObject*> requestedLocales(
      cx, &listFormat->getRequestedLocales()->as<ArrayObject>());

  // %Intl.ListFormat%.[[RelevantExtensionKeys]] is « ».
  mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys{};

  // Initialize locale options from constructor arguments.
  Rooted<LocaleOptions> localeOptions(cx);

  // Use the default locale data.
  auto localeData = LocaleData::Default;

  // Resolve the actual locale.
  Rooted<ResolvedLocale> resolved(cx);
  if (!ResolveLocale(cx, AvailableLocaleKind::ListFormat, requestedLocales,
                     localeOptions, relevantExtensionKeys, localeData,
                     &resolved)) {
    return false;
  }

  // Finish initialization by setting the actual locale.
  auto* locale = resolved.toLocale(cx);
  if (!locale) {
    return false;
  }
  listFormat->setLocale(locale);

  MOZ_ASSERT(listFormat->isLocaleResolved(), "locale successfully resolved");
  return true;
}

/**
 * Returns a new ListFormat with the locale and list formatting options
 * of the given ListFormat object.
 */
static mozilla::intl::ListFormat* NewListFormat(
    JSContext* cx, Handle<ListFormatObject*> listFormat) {
  if (!ResolveLocale(cx, listFormat)) {
    return nullptr;
  }
  auto lfOptions = listFormat->getOptions();

  auto locale = EncodeLocale(cx, listFormat->getLocale());
  if (!locale) {
    return nullptr;
  }

  mozilla::intl::ListFormat::Options options = {
      .mType = lfOptions.type,
      .mStyle = lfOptions.style,
  };

  auto result = mozilla::intl::ListFormat::TryCreate(
      mozilla::MakeStringSpan(locale.get()), options);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  return result.unwrap().release();
}

static mozilla::intl::ListFormat* GetOrCreateListFormat(
    JSContext* cx, Handle<ListFormatObject*> listFormat) {
  // Obtain a cached mozilla::intl::ListFormat object.
  if (auto* lf = listFormat->getListFormatSlot()) {
    return lf;
  }

  auto* lf = NewListFormat(cx, listFormat);
  if (!lf) {
    return nullptr;
  }
  listFormat->setListFormatSlot(lf);

  AddICUCellMemory(listFormat, ListFormatObject::EstimatedMemoryUse);
  return lf;
}

class TwoByteStringList final {
  JSContext* cx_;

  // 'strings' takes the ownership of those strings, and 'list' will be passed
  // to mozilla::intl::ListFormat as a Span.
  Vector<UniqueTwoByteChars, mozilla::intl::DEFAULT_LIST_LENGTH> strings_;
  mozilla::intl::ListFormat::StringList list_;

 public:
  explicit TwoByteStringList(JSContext* cx) : cx_(cx), strings_(cx) {}

  bool append(JSString* string) {
    auto* linear = string->ensureLinear(cx_);
    if (!linear) {
      return false;
    }

    size_t length = linear->length();
    auto chars = cx_->make_pod_array<char16_t>(length);
    if (!chars) {
      return false;
    }
    CopyChars(chars.get(), *linear);

    return strings_.append(std::move(chars)) &&
           list_.emplaceBack(strings_.back().get(), length);
  }

  size_t length() const { return list_.length(); }

  const auto& operator[](size_t i) const { return list_[i]; }

  const auto& getList() const { return list_; }
};

/**
 * FormatList ( listFormat, list )
 */
static JSLinearString* FormatList(JSContext* cx,
                                  Handle<ListFormatObject*> listFormat,
                                  const TwoByteStringList& list) {
  // We can directly return if |list| contains less than two elements.
  if (list.length() == 0) {
    return cx->emptyString();
  }
  if (list.length() == 1) {
    return NewStringCopy<CanGC>(cx, list[0]);
  }

  auto* lf = GetOrCreateListFormat(cx, listFormat);
  if (!lf) {
    return nullptr;
  }

  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> formatBuffer(cx);
  auto formatResult = lf->Format(list.getList(), formatBuffer);
  if (formatResult.isErr()) {
    ReportInternalError(cx, formatResult.unwrapErr());
    return nullptr;
  }

  return formatBuffer.toString(cx);
}

static PlainObject* NewFormatPart(JSContext* cx,
                                  mozilla::intl::ListFormat::PartType type,
                                  Handle<JSString*> value) {
  JSString* typeStr = type == mozilla::intl::ListFormat::PartType::Element
                          ? cx->names().element
                          : cx->names().literal;

  Rooted<IdValueVector> part(cx, cx);
  if (!part.emplaceBack(NameToId(cx->names().type), StringValue(typeStr))) {
    return nullptr;
  }
  if (!part.emplaceBack(NameToId(cx->names().value), StringValue(value))) {
    return nullptr;
  }
  return NewPlainObjectWithUniqueNames(cx, part);
}

/**
 * FormatListToParts ( listFormat, list )
 */
static ArrayObject* FormatListToParts(JSContext* cx,
                                      Handle<ListFormatObject*> listFormat,
                                      const TwoByteStringList& list) {
  // We can directly return if |list| contains less than two elements.
  if (list.length() == 0) {
    return NewDenseEmptyArray(cx);
  }
  if (list.length() == 1) {
    Rooted<JSString*> value(cx, NewStringCopy<CanGC>(cx, list[0]));
    if (!value) {
      return nullptr;
    }

    Rooted<PlainObject*> part(
        cx,
        NewFormatPart(cx, mozilla::intl::ListFormat::PartType::Element, value));
    if (!part) {
      return nullptr;
    }

    auto* array = NewDenseFullyAllocatedArray(cx, 1);
    if (!array) {
      return nullptr;
    }
    array->setDenseInitializedLength(1);
    array->initDenseElement(0, ObjectValue(*part));

    return array;
  }

  auto* lf = GetOrCreateListFormat(cx, listFormat);
  if (!lf) {
    return nullptr;
  }

  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> buffer(cx);
  mozilla::intl::ListFormat::PartVector parts;
  auto formatResult = lf->FormatToParts(list.getList(), buffer, parts);
  if (formatResult.isErr()) {
    ReportInternalError(cx, formatResult.unwrapErr());
    return nullptr;
  }

  Rooted<JSString*> overallResult(cx, buffer.toString(cx));
  if (!overallResult) {
    return nullptr;
  }

  Rooted<ArrayObject*> partsArray(
      cx, NewDenseFullyAllocatedArray(cx, parts.length()));
  if (!partsArray) {
    return nullptr;
  }
  partsArray->ensureDenseInitializedLength(0, parts.length());

  Rooted<JSString*> value(cx);

  size_t index = 0;
  size_t beginIndex = 0;
  for (const auto& part : parts) {
    // |endIndex| can be equal to |beginIndex| when the string is empty.
    MOZ_ASSERT(part.second >= beginIndex);
    value = NewDependentString(cx, overallResult, beginIndex,
                               part.second - beginIndex);
    if (!value) {
      return nullptr;
    }

    auto* obj = NewFormatPart(cx, part.first, value);
    if (!obj) {
      return nullptr;
    }

    beginIndex = part.second;
    partsArray->initDenseElement(index++, ObjectValue(*obj));
  }

  MOZ_ASSERT(index == parts.length());
  MOZ_ASSERT(beginIndex == buffer.length());

  return partsArray;
}

/**
 * StringListFromIterable ( iterable )
 */
static bool StringListFromIterable(JSContext* cx, Handle<JS::Value> iterable,
                                   const char* methodName,
                                   TwoByteStringList& list) {
  // Step 1.
  if (iterable.isUndefined()) {
    return true;
  }

  // Step 2.
  JS::ForOfIterator iterator(cx);
  if (!iterator.init(iterable)) {
    return false;
  }

  // Step 3. (Not applicable)

  // Step 4.
  Rooted<JS::Value> value(cx);
  while (true) {
    // Step 4.a.
    bool done;
    if (!iterator.next(&value, &done)) {
      return false;
    }

    // Step 4.b.
    if (done) {
      return true;
    }

    // Step 4.c.
    if (!value.isString()) {
      // Step 4.c.i.
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_NOT_EXPECTED_TYPE, methodName, "string",
                                JS::InformalValueTypeName(value));

      // Step 4.c.ii.
      iterator.closeThrow();
      return false;
    }

    // Step 4.d.
    if (!list.append(value.toString())) {
      return false;
    }
  }
}

static bool IsListFormat(Handle<JS::Value> v) {
  return v.isObject() && v.toObject().is<ListFormatObject>();
}

/**
 * Intl.ListFormat.prototype.format ( list )
 */
static bool listFormat_format(JSContext* cx, const CallArgs& args) {
  Rooted<ListFormatObject*> listFormat(
      cx, &args.thisv().toObject().as<ListFormatObject>());

  // Step 3.
  TwoByteStringList stringList(cx);
  if (!StringListFromIterable(cx, args.get(0), "format", stringList)) {
    return false;
  }

  // Step 4.
  auto* str = FormatList(cx, listFormat, stringList);
  if (!str) {
    return false;
  }
  args.rval().setString(str);
  return true;
}

/**
 * Intl.ListFormat.prototype.format ( list )
 */
static bool listFormat_format(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsListFormat, listFormat_format>(cx, args);
}

/**
 * Intl.ListFormat.prototype.formatToParts ( list )
 */
static bool listFormat_formatToParts(JSContext* cx, const CallArgs& args) {
  Rooted<ListFormatObject*> listFormat(
      cx, &args.thisv().toObject().as<ListFormatObject>());

  // Step 3.
  TwoByteStringList stringList(cx);
  if (!StringListFromIterable(cx, args.get(0), "formatToParts", stringList)) {
    return false;
  }

  // Step 4.
  auto* array = FormatListToParts(cx, listFormat, stringList);
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}

/**
 * Intl.ListFormat.prototype.formatToParts ( list )
 */
static bool listFormat_formatToParts(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsListFormat, listFormat_formatToParts>(cx, args);
}

/**
 * Intl.ListFormat.prototype.resolvedOptions ( )
 */
static bool listFormat_resolvedOptions(JSContext* cx, const CallArgs& args) {
  Rooted<ListFormatObject*> listFormat(
      cx, &args.thisv().toObject().as<ListFormatObject>());

  if (!ResolveLocale(cx, listFormat)) {
    return false;
  }
  auto lfOptions = listFormat->getOptions();

  // Step 3.
  Rooted<IdValueVector> options(cx, cx);

  // Step 4.
  if (!options.emplaceBack(NameToId(cx->names().locale),
                           StringValue(listFormat->getLocale()))) {
    return false;
  }

  auto* type = NewStringCopy<CanGC>(cx, ListFormatTypeToString(lfOptions.type));
  if (!type) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().type), StringValue(type))) {
    return false;
  }

  auto* style =
      NewStringCopy<CanGC>(cx, ListFormatStyleToString(lfOptions.style));
  if (!style) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().style), StringValue(style))) {
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
 * Intl.ListFormat.prototype.resolvedOptions ( )
 */
static bool listFormat_resolvedOptions(JSContext* cx, unsigned argc,
                                       Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsListFormat, listFormat_resolvedOptions>(cx,
                                                                        args);
}

/**
 * Intl.ListFormat.supportedLocalesOf ( locales [ , options ] )
 */
static bool listFormat_supportedLocalesOf(JSContext* cx, unsigned argc,
                                          Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-3.
  auto* array = SupportedLocalesOf(cx, AvailableLocaleKind::ListFormat,
                                   args.get(0), args.get(1));
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}
