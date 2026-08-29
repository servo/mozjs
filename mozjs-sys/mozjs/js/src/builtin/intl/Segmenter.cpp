/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Intl.Segmenter implementation. */

#include "builtin/intl/Segmenter.h"

#include "mozilla/Assertions.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UsingEnum.h"

#include "jspubtd.h"
#include "NamespaceImports.h"

#include "builtin/Array.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "builtin/intl/StringAsciiChars.h"
#include "gc/AllocKind.h"
#include "gc/GCContext.h"
#include "icu4x/GraphemeClusterSegmenter.hpp"
#include "icu4x/Locale.hpp"
#include "icu4x/SentenceSegmenter.hpp"
#include "icu4x/WordSegmenter.hpp"
#include "js/CallArgs.h"
#include "js/PropertyDescriptor.h"
#include "js/PropertySpec.h"
#include "js/RootingAPI.h"
#include "js/StableStringChars.h"
#include "js/TypeDecls.h"
#include "js/Value.h"
#include "util/Unicode.h"
#include "vm/ArrayObject.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"
#include "vm/WellKnownAtom.h"

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::intl;

const JSClassOps SegmenterObject::classOps_ = {
    .finalize = SegmenterObject::finalize,
};

const JSClass SegmenterObject::class_ = {
    "Intl.Segmenter",
    JSCLASS_HAS_RESERVED_SLOTS(SegmenterObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_Segmenter) |
        JSCLASS_FOREGROUND_FINALIZE,
    &SegmenterObject::classOps_,
    &SegmenterObject::classSpec_,
};

const JSClass& SegmenterObject::protoClass_ = PlainObject::class_;

static bool segmenter_supportedLocalesOf(JSContext* cx, unsigned argc,
                                         Value* vp);

static bool segmenter_segment(JSContext* cx, unsigned argc, Value* vp);

static bool segmenter_resolvedOptions(JSContext* cx, unsigned argc, Value* vp);

static bool segmenter_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().Segmenter);
  return true;
}

static const JSFunctionSpec segmenter_static_methods[] = {
    JS_FN("supportedLocalesOf", segmenter_supportedLocalesOf, 1, 0),
    JS_FS_END,
};

static const JSFunctionSpec segmenter_methods[] = {
    JS_FN("resolvedOptions", segmenter_resolvedOptions, 0, 0),
    JS_FN("segment", segmenter_segment, 1, 0),
    JS_FN("toSource", segmenter_toSource, 0, 0),
    JS_FS_END,
};

static const JSPropertySpec segmenter_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "Intl.Segmenter", JSPROP_READONLY),
    JS_PS_END,
};

static bool Segmenter(JSContext* cx, unsigned argc, Value* vp);

const ClassSpec SegmenterObject::classSpec_ = {
    GenericCreateConstructor<Segmenter, 0, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<SegmenterObject>,
    segmenter_static_methods,
    nullptr,
    segmenter_methods,
    segmenter_properties,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

static constexpr std::string_view GranularityToString(
    SegmenterGranularity granularity) {
  MOZ_USING_ENUM(SegmenterGranularity, Grapheme, Word, Sentence);
  switch (granularity) {
    case Grapheme:
      return "grapheme";
    case Word:
      return "word";
    case Sentence:
      return "sentence";
  }
  MOZ_CRASH("invalid segmenter granularity");
}

/**
 * Intl.Segmenter ([ locales [ , options ]])
 */
static bool Segmenter(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  if (!ThrowIfNotConstructing(cx, args, "Intl.Segmenter")) {
    return false;
  }

  // Steps 2-3 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  Rooted<JSObject*> proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_Segmenter,
                                          &proto)) {
    return false;
  }

  Rooted<SegmenterObject*> segmenter(cx);
  segmenter = NewObjectWithClassProto<SegmenterObject>(cx, proto);
  if (!segmenter) {
    return false;
  }

  // Step 4. (Inlined ResolveOptions)

  // ResolveOptions, step 1.
  auto* requestedLocales = CanonicalizeLocaleList(cx, args.get(0));
  if (!requestedLocales) {
    return false;
  }
  segmenter->setRequestedLocales(requestedLocales);

  auto granularity = SegmenterGranularity::Grapheme;
  if (args.hasDefined(1)) {
    // ResolveOptions, steps 2-3.
    Rooted<JSObject*> options(
        cx, RequireObjectArg(cx, "options", "Intl.Segmenter", args[1]));
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
    // Intl.Segmenter doesn't support any Unicode extension keys.

    // ResolveOptions, step 7. (Not applicable)

    // ResolveOptions, step 8. (Performed in ResolveLocale)

    // ResolveOptions, step 9. (Return)

    // Step 5. (Not applicable when ResolveOptions is inlined.)

    // Steps 6-7. (Performed in ResolveLocale)

    // Step 8.
    static constexpr auto granularities = MapOptions<GranularityToString>(
        SegmenterGranularity::Grapheme, SegmenterGranularity::Word,
        SegmenterGranularity::Sentence);
    if (!GetStringOption(cx, options, cx->names().granularity, granularities,
                         SegmenterGranularity::Grapheme, &granularity)) {
      return false;
    }
  }

  // Step 9.
  segmenter->setGranularity(granularity);

  // Step 10.
  args.rval().setObject(*segmenter);
  return true;
}

const JSClassOps SegmentsObject::classOps_ = {
    .finalize = SegmentsObject::finalize,
};

const JSClass SegmentsObject::class_ = {
    "Intl.Segments",
    JSCLASS_HAS_RESERVED_SLOTS(SegmentsObject::SLOT_COUNT) |
        JSCLASS_FOREGROUND_FINALIZE,
    &SegmentsObject::classOps_,
};

static const JSFunctionSpec segments_methods[] = {
    JS_SELF_HOSTED_FN("containing", "Intl_Segments_containing", 1, 0),
    JS_SELF_HOSTED_SYM_FN(iterator, "Intl_Segments_iterator", 0, 0),
    JS_FS_END,
};

bool GlobalObject::initSegmentsProto(JSContext* cx,
                                     Handle<GlobalObject*> global) {
  Rooted<JSObject*> proto(
      cx, GlobalObject::createBlankPrototype<PlainObject>(cx, global));
  if (!proto) {
    return false;
  }

  if (!JS_DefineFunctions(cx, proto, segments_methods)) {
    return false;
  }

  global->initBuiltinProto(ProtoKind::SegmentsProto, proto);
  return true;
}

const JSClassOps SegmentIteratorObject::classOps_ = {
    .finalize = SegmentIteratorObject::finalize,
};

const JSClass SegmentIteratorObject::class_ = {
    "Intl.SegmentIterator",
    JSCLASS_HAS_RESERVED_SLOTS(SegmentIteratorObject::SLOT_COUNT) |
        JSCLASS_FOREGROUND_FINALIZE,
    &SegmentIteratorObject::classOps_,
};

static const JSFunctionSpec segment_iterator_methods[] = {
    JS_SELF_HOSTED_FN("next", "Intl_SegmentIterator_next", 0, 0),
    JS_FS_END,
};

static const JSPropertySpec segment_iterator_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "Segmenter String Iterator", JSPROP_READONLY),
    JS_PS_END,
};

bool GlobalObject::initSegmentIteratorProto(JSContext* cx,
                                            Handle<GlobalObject*> global) {
  Rooted<JSObject*> iteratorProto(
      cx, GlobalObject::getOrCreateIteratorPrototype(cx, global));
  if (!iteratorProto) {
    return false;
  }

  Rooted<JSObject*> proto(
      cx, GlobalObject::createBlankPrototypeInheriting<PlainObject>(
              cx, iteratorProto));
  if (!proto) {
    return false;
  }

  if (!JS_DefineFunctions(cx, proto, segment_iterator_methods)) {
    return false;
  }

  if (!JS_DefineProperties(cx, proto, segment_iterator_properties)) {
    return false;
  }

  global->initBuiltinProto(ProtoKind::SegmentIteratorProto, proto);
  return true;
}

struct Boundaries {
  // Start index of this segmentation boundary.
  int32_t startIndex = 0;

  // End index of this segmentation boundary.
  int32_t endIndex = 0;

  // |true| if the segment is word-like. (Only used for word segmentation.)
  bool isWordLike = false;
};

/**
 * Find the segmentation boundary for the string character whose position is
 * |index|. The end position of the last segment boundary is |previousIndex|.
 */
template <class T>
static Boundaries FindBoundaryFrom(const T& iter, int32_t previousIndex,
                                   int32_t index) {
  MOZ_ASSERT(previousIndex <= index,
             "previous index must not exceed the search index");

  int32_t previous = previousIndex;
  while (true) {
    // Find the next possible break index.
    int32_t next = iter.next();

    // If |next| is larger than the search index, we've found our segment end
    // index.
    if (next > index) {
      return {previous, next, iter.isWordLike()};
    }

    // Otherwise store |next| as the start index of the next segment,
    previous = next;
  }
}

// TODO: Consider switching to the ICU4X C++ headers when the C++ headers
// are in better shape: https://github.com/rust-diplomat/diplomat/issues/280

template <typename Interface>
class SegmenterBreakIteratorType {
  typename Interface::BreakIterator* impl_;

 public:
  explicit SegmenterBreakIteratorType(void* impl)
      : impl_(static_cast<typename Interface::BreakIterator*>(impl)) {
    MOZ_ASSERT(impl);
  }

  int32_t next() const { return Interface::next(impl_); }

  bool isWordLike() const { return Interface::isWordLike(impl_); }
};

// Each SegmenterBreakIterator interface contains the following definitions:
//
// - BreakIterator: Type of the ICU4X break iterator.
// - Segmenter: Type of the ICU4X segmenter.
// - Char: Character type, either `JS::Latin1Char` or `char16_t`.
// - create: Static method to create a new instance of `BreakIterator`.
// - destroy: Static method to destroy an instance of `BreakIterator`.
// - next: Static method to fetch the next break iteration index.
// - isWordLike: Static method to determine if the current segment is word-like.
//
//
// Each Segmenter interface contains the following definitions:
//
// - Segmenter: Type of the ICU4X segmenter.
// - BreakIteratorLatin1: SegmenterBreakIterator interface to Latin1 strings.
// - BreakIteratorTwoByte: SegmenterBreakIterator interface to TwoByte strings.
// - create: Static method to create a new instance of `Segmenter`.
// - destroy: Static method to destroy an instance of `Segmenter`.

struct GraphemeClusterSegmenterBreakIteratorLatin1 {
  using BreakIterator = icu4x::capi::GraphemeClusterBreakIteratorLatin1;
  using Segmenter = icu4x::capi::GraphemeClusterSegmenter;
  using Char = JS::Latin1Char;
  using StringView = icu4x::diplomat::capi::DiplomatU8View;

  static constexpr auto& create =
      icu4x::capi::icu4x_GraphemeClusterSegmenter_segment_latin1_mv1;
  static constexpr auto& destroy =
      icu4x::capi::icu4x_GraphemeClusterBreakIteratorLatin1_destroy_mv1;
  static constexpr auto& next =
      icu4x::capi::icu4x_GraphemeClusterBreakIteratorLatin1_next_mv1;

  static bool isWordLike(const BreakIterator*) { return false; }
};

struct GraphemeClusterSegmenterBreakIteratorTwoByte {
  using BreakIterator = icu4x::capi::GraphemeClusterBreakIteratorUtf16;
  using Segmenter = icu4x::capi::GraphemeClusterSegmenter;
  using Char = char16_t;
  using StringView = icu4x::diplomat::capi::DiplomatString16View;

  static constexpr auto& create =
      icu4x::capi::icu4x_GraphemeClusterSegmenter_segment_utf16_mv1;
  static constexpr auto& destroy =
      icu4x::capi::icu4x_GraphemeClusterBreakIteratorUtf16_destroy_mv1;
  static constexpr auto& next =
      icu4x::capi::icu4x_GraphemeClusterBreakIteratorUtf16_next_mv1;

  static bool isWordLike(const BreakIterator*) { return false; }
};

struct GraphemeClusterSegmenter {
  using Segmenter = icu4x::capi::GraphemeClusterSegmenter;
  using BreakIteratorLatin1 =
      SegmenterBreakIteratorType<GraphemeClusterSegmenterBreakIteratorLatin1>;
  using BreakIteratorTwoByte =
      SegmenterBreakIteratorType<GraphemeClusterSegmenterBreakIteratorTwoByte>;

  static constexpr auto& create =
      icu4x::capi::icu4x_GraphemeClusterSegmenter_create_mv1;
  static constexpr auto& destroy =
      icu4x::capi::icu4x_GraphemeClusterSegmenter_destroy_mv1;
};

struct WordSegmenterBreakIteratorLatin1 {
  using BreakIterator = icu4x::capi::WordBreakIteratorLatin1;
  using Segmenter = icu4x::capi::WordSegmenter;
  using Char = JS::Latin1Char;
  using StringView = icu4x::diplomat::capi::DiplomatU8View;

  static constexpr auto& create =
      icu4x::capi::icu4x_WordSegmenter_segment_latin1_mv1;
  static constexpr auto& destroy =
      icu4x::capi::icu4x_WordBreakIteratorLatin1_destroy_mv1;
  static constexpr auto& next =
      icu4x::capi::icu4x_WordBreakIteratorLatin1_next_mv1;
  static constexpr auto& isWordLike =
      icu4x::capi::icu4x_WordBreakIteratorLatin1_is_word_like_mv1;
};

struct WordSegmenterBreakIteratorTwoByte {
  using BreakIterator = icu4x::capi::WordBreakIteratorUtf16;
  using Segmenter = icu4x::capi::WordSegmenter;
  using Char = char16_t;
  using StringView = icu4x::diplomat::capi::DiplomatString16View;

  static constexpr auto& create =
      icu4x::capi::icu4x_WordSegmenter_segment_utf16_mv1;
  static constexpr auto& destroy =
      icu4x::capi::icu4x_WordBreakIteratorUtf16_destroy_mv1;
  static constexpr auto& next =
      icu4x::capi::icu4x_WordBreakIteratorUtf16_next_mv1;
  static constexpr auto& isWordLike =
      icu4x::capi::icu4x_WordBreakIteratorUtf16_is_word_like_mv1;
};

struct WordSegmenter {
  using Segmenter = icu4x::capi::WordSegmenter;
  using BreakIteratorLatin1 =
      SegmenterBreakIteratorType<WordSegmenterBreakIteratorLatin1>;
  using BreakIteratorTwoByte =
      SegmenterBreakIteratorType<WordSegmenterBreakIteratorTwoByte>;

  static constexpr auto& create =
      icu4x::capi::icu4x_WordSegmenter_create_auto_with_content_locale_mv1;
  static constexpr auto& destroy = icu4x::capi::icu4x_WordSegmenter_destroy_mv1;
};

struct SentenceSegmenterBreakIteratorLatin1 {
  using BreakIterator = icu4x::capi::SentenceBreakIteratorLatin1;
  using Segmenter = icu4x::capi::SentenceSegmenter;
  using Char = JS::Latin1Char;
  using StringView = icu4x::diplomat::capi::DiplomatU8View;

  static constexpr auto& create =
      icu4x::capi::icu4x_SentenceSegmenter_segment_latin1_mv1;
  static constexpr auto& destroy =
      icu4x::capi::icu4x_SentenceBreakIteratorLatin1_destroy_mv1;
  static constexpr auto& next =
      icu4x::capi::icu4x_SentenceBreakIteratorLatin1_next_mv1;

  static bool isWordLike(const BreakIterator*) { return false; }
};

struct SentenceSegmenterBreakIteratorTwoByte {
  using BreakIterator = icu4x::capi::SentenceBreakIteratorUtf16;
  using Segmenter = icu4x::capi::SentenceSegmenter;
  using Char = char16_t;
  using StringView = icu4x::diplomat::capi::DiplomatString16View;

  static constexpr auto& create =
      icu4x::capi::icu4x_SentenceSegmenter_segment_utf16_mv1;
  static constexpr auto& destroy =
      icu4x::capi::icu4x_SentenceBreakIteratorUtf16_destroy_mv1;
  static constexpr auto& next =
      icu4x::capi::icu4x_SentenceBreakIteratorUtf16_next_mv1;

  static bool isWordLike(const BreakIterator*) { return false; }
};

struct SentenceSegmenter {
  using Segmenter = icu4x::capi::SentenceSegmenter;
  using BreakIteratorLatin1 =
      SegmenterBreakIteratorType<SentenceSegmenterBreakIteratorLatin1>;
  using BreakIteratorTwoByte =
      SegmenterBreakIteratorType<SentenceSegmenterBreakIteratorTwoByte>;

  static constexpr auto& create =
      icu4x::capi::icu4x_SentenceSegmenter_create_with_content_locale_mv1;
  static constexpr auto& destroy =
      icu4x::capi::icu4x_SentenceSegmenter_destroy_mv1;
};

class ICU4XLocaleDeleter {
 public:
  void operator()(icu4x::capi::Locale* ptr) {
    icu4x::capi::icu4x_Locale_destroy_mv1(ptr);
  }
};

using UniqueICU4XLocale =
    mozilla::UniquePtr<icu4x::capi::Locale, ICU4XLocaleDeleter>;

static UniqueICU4XLocale CreateICU4XLocale(JSContext* cx, LanguageId locale) {
  auto str = locale.toString();
  auto result =
      icu4x::capi::icu4x_Locale_from_string_mv1({str.data(), str.length()});
  if (!result.is_ok) {
    ReportInternalError(cx);
    return nullptr;
  }
  return UniqueICU4XLocale{result.ok};
}

/**
 * Create a new, locale-invariant ICU4X segmenter instance.
 */
template <typename Interface>
static typename Interface::Segmenter* CreateSegmenter() {
  return Interface::create();
}

/**
 * Create a new ICU4X segmenter instance, tailored for |locale|.
 */
template <typename Interface>
static typename Interface::Segmenter* CreateSegmenter(JSContext* cx,
                                                      LanguageId locale) {
  auto loc = CreateICU4XLocale(cx, locale);
  if (!loc) {
    return nullptr;
  }

  auto result = Interface::create(loc.get());
  if (!result.is_ok) {
    ReportInternalError(cx);
    return nullptr;
  }
  return result.ok;
}

/**
 * Resolve the actual locale to finish initialization of the Segmenter.
 */
static bool ResolveLocale(JSContext* cx, Handle<SegmenterObject*> segmenter) {
  // Return if the locale was already resolved.
  if (segmenter->isLocaleResolved()) {
    return true;
  }

  Rooted<ArrayObject*> requestedLocales(
      cx, &segmenter->getRequestedLocales()->as<ArrayObject>());

  // %Intl.Segmenter%.[[RelevantExtensionKeys]] is « ».
  mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys{};

  // Initialize locale options from constructor arguments.
  Rooted<LocaleOptions> localeOptions(cx);

  // Use the default locale data.
  auto localeData = LocaleData::Default;

  // Resolve the actual locale.
  Rooted<ResolvedLocale> resolved(cx);
  if (!ResolveLocale(cx, AvailableLocaleKind::Segmenter, requestedLocales,
                     localeOptions, relevantExtensionKeys, localeData,
                     &resolved)) {
    return false;
  }

  switch (segmenter->getGranularity()) {
    case SegmenterGranularity::Grapheme: {
      auto* seg = CreateSegmenter<GraphemeClusterSegmenter>();
      if (!seg) {
        return false;
      }
      segmenter->setSegmenter(seg);
      break;
    }
    case SegmenterGranularity::Word: {
      auto* seg = CreateSegmenter<WordSegmenter>(cx, resolved.dataLocale());
      if (!seg) {
        return false;
      }
      segmenter->setSegmenter(seg);
      break;
    }
    case SegmenterGranularity::Sentence: {
      auto* seg = CreateSegmenter<SentenceSegmenter>(cx, resolved.dataLocale());
      if (!seg) {
        return false;
      }
      segmenter->setSegmenter(seg);
      break;
    }
  }

  // Finish initialization by setting the actual locale.
  auto* locale = resolved.toLocale(cx);
  if (!locale) {
    return false;
  }
  segmenter->setLocale(locale);

  MOZ_ASSERT(segmenter->isLocaleResolved(), "locale successfully resolved");
  return true;
}

/**
 * Destroy an ICU4X segmenter instance.
 */
template <typename Interface>
static void DestroySegmenter(void* seg) {
  auto* segmenter = static_cast<typename Interface::Segmenter*>(seg);
  Interface::destroy(segmenter);
}

void SegmenterObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  MOZ_ASSERT(gcx->onMainThread());

  auto& segmenter = obj->as<SegmenterObject>();
  if (void* seg = segmenter.getSegmenter()) {
    switch (segmenter.getGranularity()) {
      case SegmenterGranularity::Grapheme: {
        DestroySegmenter<GraphemeClusterSegmenter>(seg);
        break;
      }
      case SegmenterGranularity::Word: {
        DestroySegmenter<WordSegmenter>(seg);
        break;
      }
      case SegmenterGranularity::Sentence: {
        DestroySegmenter<SentenceSegmenter>(seg);
        break;
      }
    }
  }
}

/**
 * Destroy an ICU4X break iterator instance.
 */
template <typename Interface>
static void DestroyBreakIterator(void* brk) {
  auto* breakIterator = static_cast<typename Interface::BreakIterator*>(brk);
  Interface::destroy(breakIterator);
}

/**
 * Destroy the ICU4X break iterator attached to |segments|.
 */
template <typename T>
static void DestroyBreakIterator(const T* segments) {
  void* brk = segments->getBreakIterator();
  MOZ_ASSERT(brk);

  bool isLatin1 = segments->hasLatin1StringChars();

  switch (segments->getGranularity()) {
    case SegmenterGranularity::Grapheme: {
      if (isLatin1) {
        DestroyBreakIterator<GraphemeClusterSegmenterBreakIteratorLatin1>(brk);
      } else {
        DestroyBreakIterator<GraphemeClusterSegmenterBreakIteratorTwoByte>(brk);
      }
      break;
    }
    case SegmenterGranularity::Word: {
      if (isLatin1) {
        DestroyBreakIterator<WordSegmenterBreakIteratorLatin1>(brk);
      } else {
        DestroyBreakIterator<WordSegmenterBreakIteratorTwoByte>(brk);
      }
      break;
    }
    case SegmenterGranularity::Sentence: {
      if (isLatin1) {
        DestroyBreakIterator<SentenceSegmenterBreakIteratorLatin1>(brk);
      } else {
        DestroyBreakIterator<SentenceSegmenterBreakIteratorTwoByte>(brk);
      }
      break;
    }
  }
}

void SegmentsObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  MOZ_ASSERT(gcx->onMainThread());

  auto* segments = &obj->as<SegmentsObject>();

  if (auto chars = segments->getStringChars()) {
    size_t length = segments->getString()->length();
    if (chars.has<JS::Latin1Char>()) {
      RemoveICUCellMemory(gcx, segments, length * sizeof(JS::Latin1Char));
      js_free(chars.data<JS::Latin1Char>());
    } else {
      RemoveICUCellMemory(gcx, segments, length * sizeof(char16_t));
      js_free(chars.data<char16_t>());
    }
  }

  if (segments->getBreakIterator()) {
    DestroyBreakIterator(segments);
  }
}

void SegmentIteratorObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  MOZ_ASSERT(gcx->onMainThread());

  auto* iterator = &obj->as<SegmentIteratorObject>();

  if (auto chars = iterator->getStringChars()) {
    size_t length = iterator->getString()->length();
    if (chars.has<JS::Latin1Char>()) {
      RemoveICUCellMemory(gcx, iterator, length * sizeof(JS::Latin1Char));
      js_free(chars.data<JS::Latin1Char>());
    } else {
      RemoveICUCellMemory(gcx, iterator, length * sizeof(char16_t));
      js_free(chars.data<char16_t>());
    }
  }

  if (iterator->getBreakIterator()) {
    DestroyBreakIterator(iterator);
  }
}

template <typename Iterator, typename T>
static Boundaries FindBoundaryFrom(Handle<T*> segments, int32_t index) {
  MOZ_ASSERT(0 <= index && uint32_t(index) < segments->getString()->length());

  Iterator iter(segments->getBreakIterator());
  return FindBoundaryFrom(iter, segments->getIndex(), index);
}

template <typename T>
static Boundaries GraphemeBoundaries(Handle<T*> segments, int32_t index) {
  if (segments->hasLatin1StringChars()) {
    return FindBoundaryFrom<GraphemeClusterSegmenter::BreakIteratorLatin1>(
        segments, index);
  }
  return FindBoundaryFrom<GraphemeClusterSegmenter::BreakIteratorTwoByte>(
      segments, index);
}

template <typename T>
static Boundaries WordBoundaries(Handle<T*> segments, int32_t index) {
  if (segments->hasLatin1StringChars()) {
    return FindBoundaryFrom<WordSegmenter::BreakIteratorLatin1>(segments,
                                                                index);
  }
  return FindBoundaryFrom<WordSegmenter::BreakIteratorTwoByte>(segments, index);
}

template <typename T>
static Boundaries SentenceBoundaries(Handle<T*> segments, int32_t index) {
  if (segments->hasLatin1StringChars()) {
    return FindBoundaryFrom<SentenceSegmenter::BreakIteratorLatin1>(segments,
                                                                    index);
  }
  return FindBoundaryFrom<SentenceSegmenter::BreakIteratorTwoByte>(segments,
                                                                   index);
}

/**
 * Ensure the string characters have been copied into |segments| in preparation
 * for passing the string characters to ICU4X.
 */
template <typename T>
static bool EnsureStringChars(JSContext* cx, Handle<T*> segments) {
  if (segments->hasStringChars()) {
    return true;
  }

  Rooted<JSLinearString*> string(cx, segments->getString()->ensureLinear(cx));
  if (!string) {
    return false;
  }

  size_t length = string->length();

  JS::AutoCheckCannotGC nogc;
  if (string->hasLatin1Chars()) {
    auto chars = DuplicateString(cx, string->latin1Chars(nogc), length);
    if (!chars) {
      return false;
    }
    segments->setStringChars(SegmentsStringChars{chars.release()});

    AddICUCellMemory(segments, length * sizeof(JS::Latin1Char));
  } else {
    auto chars = DuplicateString(cx, string->twoByteChars(nogc), length);
    if (!chars) {
      return false;
    }
    segments->setStringChars(SegmentsStringChars{chars.release()});

    AddICUCellMemory(segments, length * sizeof(char16_t));
  }
  return true;
}

/**
 * Create a new ICU4X break iterator instance.
 */
template <typename Interface, typename T>
static auto* CreateBreakIterator(Handle<T*> segments) {
  void* segmenter = segments->getSegmenter()->getSegmenter();
  MOZ_ASSERT(segmenter);

  auto chars = segments->getStringChars();
  MOZ_ASSERT(chars);

  size_t length = segments->getString()->length();

  auto* seg = static_cast<const typename Interface::Segmenter*>(segmenter);
  auto* ch = chars.template data<typename Interface::Char>();
  typename Interface::StringView view{ch, length};
  return Interface::create(seg, view);
}

/**
 * Ensure |segments| has a break iterator whose current segment index is at most
 * |index|.
 */
template <typename T>
static bool EnsureBreakIterator(JSContext* cx, Handle<T*> segments,
                                int32_t index) {
  if (segments->getBreakIterator()) {
    // Reuse the break iterator if its current segment index is at most |index|.
    if (index >= segments->getIndex()) {
      return true;
    }

    // Reverse iteration not supported. Destroy the previous break iterator and
    // start from fresh.
    DestroyBreakIterator(segments.get());

    // Reset internal state.
    segments->setBreakIterator(nullptr);
    segments->setIndex(0);
  }

  // Ensure the locale is resolved.
  if (!segments->getSegmenter()->isLocaleResolved()) {
    Rooted<SegmenterObject*> segmenter(cx, segments->getSegmenter());
    if (!ResolveLocale(cx, segmenter)) {
      return false;
    }
  }

  // Ensure the string characters can be passed to ICU4X.
  if (!EnsureStringChars(cx, segments)) {
    return false;
  }

  bool isLatin1 = segments->hasLatin1StringChars();

  // Create a new break iterator based on the granularity and character type.
  void* brk;
  switch (segments->getGranularity()) {
    case SegmenterGranularity::Grapheme: {
      if (isLatin1) {
        brk = CreateBreakIterator<GraphemeClusterSegmenterBreakIteratorLatin1>(
            segments);
      } else {
        brk = CreateBreakIterator<GraphemeClusterSegmenterBreakIteratorTwoByte>(
            segments);
      }
      break;
    }
    case SegmenterGranularity::Word: {
      if (isLatin1) {
        brk = CreateBreakIterator<WordSegmenterBreakIteratorLatin1>(segments);
      } else {
        brk = CreateBreakIterator<WordSegmenterBreakIteratorTwoByte>(segments);
      }
      break;
    }
    case SegmenterGranularity::Sentence: {
      if (isLatin1) {
        brk =
            CreateBreakIterator<SentenceSegmenterBreakIteratorLatin1>(segments);
      } else {
        brk = CreateBreakIterator<SentenceSegmenterBreakIteratorTwoByte>(
            segments);
      }
      break;
    }
  }

  MOZ_RELEASE_ASSERT(brk);
  segments->setBreakIterator(brk);

  MOZ_ASSERT(segments->getIndex() == 0, "index is initially zero");

  return true;
}

/**
 * Create the boundaries result array for self-hosted code.
 */
static ArrayObject* CreateBoundaries(JSContext* cx, Boundaries boundaries,
                                     SegmenterGranularity granularity) {
  auto [startIndex, endIndex, isWordLike] = boundaries;

  auto* result = NewDenseFullyAllocatedArray(cx, 3);
  if (!result) {
    return nullptr;
  }
  result->setDenseInitializedLength(3);
  result->initDenseElement(0, Int32Value(startIndex));
  result->initDenseElement(1, Int32Value(endIndex));
  if (granularity == SegmenterGranularity::Word) {
    result->initDenseElement(2, BooleanValue(isWordLike));
  } else {
    result->initDenseElement(2, UndefinedValue());
  }
  return result;
}

template <typename T>
static ArrayObject* FindSegmentBoundaries(JSContext* cx, Handle<T*> segments,
                                          int32_t index) {
  // Ensure break iteration can start at |index|.
  if (!EnsureBreakIterator(cx, segments, index)) {
    return nullptr;
  }

  // Find the actual segment boundaries.
  Boundaries boundaries{};
  switch (segments->getGranularity()) {
    case SegmenterGranularity::Grapheme: {
      boundaries = GraphemeBoundaries(segments, index);
      break;
    }
    case SegmenterGranularity::Word: {
      boundaries = WordBoundaries(segments, index);
      break;
    }
    case SegmenterGranularity::Sentence: {
      boundaries = SentenceBoundaries(segments, index);
      break;
    }
  }

  // Remember the end index of the current boundary segment.
  segments->setIndex(boundaries.endIndex);

  return CreateBoundaries(cx, boundaries, segments->getGranularity());
}

/**
 * Create a new Segments object.
 */
static SegmentsObject* CreateSegmentsObject(JSContext* cx,
                                            Handle<SegmenterObject*> segmenter,
                                            Handle<JSString*> string) {
  Rooted<JSObject*> proto(
      cx, GlobalObject::getOrCreateSegmentsPrototype(cx, cx->global()));
  if (!proto) {
    return nullptr;
  }

  auto* segments = NewObjectWithGivenProto<SegmentsObject>(cx, proto);
  if (!segments) {
    return nullptr;
  }

  segments->setSegmenter(segmenter);
  segments->setGranularity(segmenter->getGranularity());
  segments->setString(string);
  segments->setIndex(0);

  return segments;
}

bool js::intl_CreateSegmentIterator(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 1);

  Rooted<SegmentsObject*> segments(cx,
                                   &args[0].toObject().as<SegmentsObject>());

  Rooted<JSObject*> proto(
      cx, GlobalObject::getOrCreateSegmentIteratorPrototype(cx, cx->global()));
  if (!proto) {
    return false;
  }

  auto* iterator = NewObjectWithGivenProto<SegmentIteratorObject>(cx, proto);
  if (!iterator) {
    return false;
  }

  iterator->setSegmenter(segments->getSegmenter());
  iterator->setGranularity(segments->getGranularity());
  iterator->setString(segments->getString());
  iterator->setIndex(0);

  args.rval().setObject(*iterator);
  return true;
}

bool js::intl_FindSegmentBoundaries(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 2);

  Rooted<SegmentsObject*> segments(cx,
                                   &args[0].toObject().as<SegmentsObject>());

  int32_t index = args[1].toInt32();
  MOZ_ASSERT(index >= 0);
  MOZ_ASSERT(uint32_t(index) < segments->getString()->length());

  auto* result = FindSegmentBoundaries(
      cx, static_cast<Handle<SegmentsObject*>>(segments), index);
  if (!result) {
    return false;
  }

  args.rval().setObject(*result);
  return true;
}

bool js::intl_FindNextSegmentBoundaries(JSContext* cx, unsigned argc,
                                        Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.length() == 1);

  Rooted<SegmentIteratorObject*> iterator(
      cx, &args[0].toObject().as<SegmentIteratorObject>());

  int32_t index = iterator->getIndex();
  MOZ_ASSERT(index >= 0);
  MOZ_ASSERT(uint32_t(index) < iterator->getString()->length());

  auto* result = FindSegmentBoundaries(
      cx, static_cast<Handle<SegmentIteratorObject*>>(iterator), index);
  if (!result) {
    return false;
  }

  args.rval().setObject(*result);
  return true;
}

static bool IsSegmenter(Handle<JS::Value> v) {
  return v.isObject() && v.toObject().is<SegmenterObject>();
}

/**
 * Intl.Segmenter.prototype.segment ( string )
 */
static bool segmenter_segment(JSContext* cx, const CallArgs& args) {
  Rooted<SegmenterObject*> segmenter(
      cx, &args.thisv().toObject().as<SegmenterObject>());

  // Step 3.
  Rooted<JSString*> string(cx, JS::ToString(cx, args.get(0)));
  if (!string) {
    return false;
  }

  // Step 4.
  auto* result = CreateSegmentsObject(cx, segmenter, string);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

/**
 * Intl.Segmenter.prototype.segment ( string )
 */
static bool segmenter_segment(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsSegmenter, segmenter_segment>(cx, args);
}

/**
 * Intl.Segmenter.prototype.resolvedOptions ( )
 */
static bool segmenter_resolvedOptions(JSContext* cx, const CallArgs& args) {
  Rooted<SegmenterObject*> segmenter(
      cx, &args.thisv().toObject().as<SegmenterObject>());

  if (!ResolveLocale(cx, segmenter)) {
    return false;
  }

  // Step 3.
  Rooted<IdValueVector> options(cx, cx);

  // Step 4.
  if (!options.emplaceBack(NameToId(cx->names().locale),
                           StringValue(segmenter->getLocale()))) {
    return false;
  }

  auto* granularity = NewStringCopy<CanGC>(
      cx, GranularityToString(segmenter->getGranularity()));
  if (!granularity) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().granularity),
                           StringValue(granularity))) {
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
 * Intl.Segmenter.prototype.resolvedOptions ( )
 */
static bool segmenter_resolvedOptions(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsSegmenter, segmenter_resolvedOptions>(cx, args);
}

/**
 * Intl.Segmenter.supportedLocalesOf ( locales [ , options ] )
 */
static bool segmenter_supportedLocalesOf(JSContext* cx, unsigned argc,
                                         Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-3.
  auto* array = SupportedLocalesOf(cx, AvailableLocaleKind::Segmenter,
                                   args.get(0), args.get(1));
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}
