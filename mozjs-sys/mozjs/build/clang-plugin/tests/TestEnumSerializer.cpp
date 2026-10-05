#define MOZ_ENUM_SERIALIZER_ALLOW_SENTINEL_UPPER_BOUND \
  __attribute__((annotate("moz_enum_serializer_allow_sentinel_upper_bound")))
#define MOZ_ENUM_SERIALIZER_ALLOW_MIN_MISMATCH \
  __attribute__((annotate("moz_enum_serializer_allow_min_mismatch")))

// Minimal definitions matching ipc/glue/EnumSerializer.h
template <typename E, typename EnumValidator> struct EnumSerializer {};

template <typename E, E MinLegal, E HighBound>
class ContiguousEnumValidator {};

template <typename E, E MinLegal, E MaxLegal>
class ContiguousEnumValidatorInclusive {};

template <typename E, E MinLegal, E HighBound>
struct ContiguousEnumSerializer
    : EnumSerializer<E, ContiguousEnumValidator<E, MinLegal, HighBound>> {};

template <typename E, E MinLegal, E MaxLegal>
struct ContiguousEnumSerializerInclusive
    : EnumSerializer<E,
                     ContiguousEnumValidatorInclusive<E, MinLegal, MaxLegal>> {};

template <typename E, E AllBits>
struct BitFlagsEnumSerializer {};

template <class P> struct ParamTraits;

// --- Test: Inclusive with sentinel value ---

enum class WithCount { A, B, C, OP_COUNT };

template <>
struct ParamTraits<WithCount> // expected-warning {{ContiguousEnumSerializerInclusive includes sentinel value 'OP_COUNT' as valid; use ContiguousEnumSerializer with an exclusive upper bound instead}}
    : ContiguousEnumSerializerInclusive<
          WithCount, WithCount::A, WithCount::OP_COUNT> {};

enum class WithMax { None, Partial, Full, MAX };

template <>
struct ParamTraits<WithMax> // expected-warning {{ContiguousEnumSerializerInclusive includes sentinel value 'MAX' as valid; use ContiguousEnumSerializer with an exclusive upper bound instead}}
    : ContiguousEnumSerializerInclusive<
          WithMax, WithMax::None, WithMax::MAX> {};

enum class WithInvalid { Default, Verbose, Invalid };

template <>
struct ParamTraits<WithInvalid> // expected-warning {{ContiguousEnumSerializerInclusive includes sentinel value 'Invalid' as valid; use ContiguousEnumSerializer with an exclusive upper bound instead}}
    : ContiguousEnumSerializerInclusive<
          WithInvalid, WithInvalid::Default, WithInvalid::Invalid> {};

// --- Test: Min value doesn't match first enumerator ---

enum class SkippedFirst { First = 0, Second = 1, Third = 2, NUM };

template <>
struct ParamTraits<SkippedFirst> // expected-warning {{ContiguousEnumSerializer min value 'Second' does not match the first enumerator 'First' (value 0); the range excludes valid enum values}}
    : ContiguousEnumSerializer<
          SkippedFirst, SkippedFirst::Second, SkippedFirst::NUM> {};

// --- Test: Non-contiguous enum (gaps) ---

enum class Rotation { R0 = 0, R90 = 90, R180 = 180, R270 = 270, SENTINEL };

template <>
struct ParamTraits<Rotation> // expected-error {{ContiguousEnumSerializer used with non-contiguous enum; range accepts 271 values but only 4 enumerators exist (267 invalid values accepted)}}
    : ContiguousEnumSerializer<
          Rotation, Rotation::R0, Rotation::SENTINEL> {};


// Non-contiguous (duplication)

enum class Duplicate { First = 1, Second = 2, AlsoSecond = 2, Fourth = 4, Count };

template <>
struct ParamTraits<Duplicate> // expected-error {{ContiguousEnumSerializer used with non-contiguous enum; range accepts 4 values but only 3 enumerators exist (1 invalid values accepted)}}
    : ContiguousEnumSerializer<
          Duplicate, Duplicate::First, Duplicate::Count> {};

// --- Test: Bit-flag enum ---

enum class Flags {
  NONE = 0,
  A = 1 << 0,
  B = 1 << 1,
  C = 1 << 2,
  D = 1 << 3,
  SENTINEL
};

template <>
struct ParamTraits<Flags> // expected-error {{ContiguousEnumSerializer used with non-contiguous enum; range accepts 9 values but only 5 enumerators exist (4 invalid values accepted)}}
    : ContiguousEnumSerializer<
          Flags, Flags::NONE, Flags::SENTINEL> {};

// --- Test: Correct usage (should not warn) ---

enum class Good { First, Second, Third, NUM };

template <>
struct ParamTraits<Good>
    : ContiguousEnumSerializer<Good, Good::First, Good::NUM> {};

enum class GoodInclusive { First, Second, Third };

template <>
struct ParamTraits<GoodInclusive>
    : ContiguousEnumSerializerInclusive<GoodInclusive, GoodInclusive::First,
                                        GoodInclusive::Third> {};

// --- Test: Inclusive + sentinel AND skipped first (multiple errors) ---

enum class ComboIssue { Clear = 0, Over = 1, Xor = 2, OP_COUNT };

template <>
struct ParamTraits<ComboIssue> // expected-warning {{ContiguousEnumSerializerInclusive includes sentinel value 'OP_COUNT' as valid; use ContiguousEnumSerializer with an exclusive upper bound instead}} expected-warning {{ContiguousEnumSerializerInclusive min value 'Over' does not match the first enumerator 'Clear' (value 0); the range excludes valid enum values}}
    : ContiguousEnumSerializerInclusive<
          ComboIssue, ComboIssue::Over, ComboIssue::OP_COUNT> {};

// --- Test: Non-contiguous with small gap ---

enum class StorageAccess { Deny = -2, Default = -1, Allow = 0, Prompt = 1, Grant = 3 };

template <>
struct ParamTraits<StorageAccess> // expected-error {{ContiguousEnumSerializerInclusive used with non-contiguous enum; range accepts 6 values but only 5 enumerators exist (1 invalid values accepted)}}
    : ContiguousEnumSerializerInclusive<
          StorageAccess, StorageAccess::Deny, StorageAccess::Grant> {};

// --- Test: Upper bound doesn't match last enumerator (inclusive) ---

enum class MissedHigh { A = 0, B = 1, C = 2, D = 3 };

template <>
struct ParamTraits<MissedHigh> // expected-warning {{ContiguousEnumSerializerInclusive upper bound does not match the last enumerator 'D' (value 3); the range may exclude valid enum values}}
    : ContiguousEnumSerializerInclusive<
          MissedHigh, MissedHigh::A, MissedHigh::B> {};

// --- Test: Upper bound doesn't match last enumerator (exclusive) ---

enum class MissedHighExcl { A = 0, B = 1, C = 2, D = 3, Count };

template <>
struct ParamTraits<MissedHighExcl> // expected-warning {{ContiguousEnumSerializer upper bound does not match the last enumerator 'Count' (value 4); the range may exclude valid enum values}}
    : ContiguousEnumSerializer<
          MissedHighExcl, MissedHighExcl::A, MissedHighExcl::C> {};

// --- Test: Attribute suppresses sentinel-upper-bound warning ---

enum class AllowedSentinel { A, B, C, END };

template <>
struct MOZ_ENUM_SERIALIZER_ALLOW_SENTINEL_UPPER_BOUND ParamTraits<AllowedSentinel>
    : ContiguousEnumSerializerInclusive<AllowedSentinel, AllowedSentinel::A,
                                        AllowedSentinel::END> {};

// --- Test: Attribute suppresses min-mismatch warning ---

enum class AllowedMinMismatch { First = 0, Second = 1, Third = 2, NUM };

template <>
struct MOZ_ENUM_SERIALIZER_ALLOW_MIN_MISMATCH ParamTraits<AllowedMinMismatch>
    : ContiguousEnumSerializer<AllowedMinMismatch, AllowedMinMismatch::Second,
                               AllowedMinMismatch::NUM> {};
