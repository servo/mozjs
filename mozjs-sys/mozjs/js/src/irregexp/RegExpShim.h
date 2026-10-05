/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RegexpShim_h
#define RegexpShim_h

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "mozilla/SegmentedVector.h"
#include "mozilla/Sprintf.h"

#include <algorithm>
#include <bit>
#include <optional>
#include <ostream>

#include "irregexp/RegExpTypes.h"
#include "irregexp/util/BitFieldShim.h"
#include "irregexp/util/BitVectorShim.h"
#include "irregexp/util/FlagsShim.h"
#include "irregexp/util/VectorShim.h"
#include "irregexp/util/ZoneShim.h"
#include "jit/JitCode.h"
#include "jit/Label.h"
#include "jit/shared/Assembler-shared.h"
#include "js/friend/StackLimits.h"  // js::AutoCheckRecursionLimit
#include "js/RegExpFlags.h"
#include "js/Value.h"
#include "threading/ExclusiveData.h"
#include "util/DifferentialTesting.h"
#include "vm/JSContext.h"
#include "vm/MutexIDs.h"
#include "vm/NativeObject.h"
#include "vm/RegExpShared.h"

// Forward declaration of classes
namespace js::jit {
class MacroAssembler;
}

namespace v8 {
namespace internal {

class Heap;
class Isolate;
class RegExpMatchInfo;

namespace regexp {
class Stack;
class SMRegExpMacroAssembler;
class Utils;
}  // namespace regexp

using MacroAssembler = ::js::jit::MacroAssembler;

template <typename T>
class Handle;

}  // namespace internal
}  // namespace v8

#define V8_WARN_UNUSED_RESULT [[nodiscard]]
#define V8_EXPORT_PRIVATE
#define V8_FALLTHROUGH [[fallthrough]]
#define V8_NODISCARD [[nodiscard]]
#define V8_NOEXCEPT noexcept
#define V8_LIFETIME_BOUND /* unsupported */
#define V8_GSL_POINTER    /* [[gsl::Pointer]] unsupported */

#define V8_LIKELY(x) MOZ_LIKELY(x)
#define V8_UNLIKELY(x) MOZ_UNLIKELY(x)

#define FATAL(x) MOZ_CRASH(x)
#define UNREACHABLE() MOZ_CRASH("unreachable code")
#define UNIMPLEMENTED() MOZ_CRASH("unimplemented code")
#define STATIC_ASSERT(exp) static_assert(exp, #exp)

#define DCHECK MOZ_ASSERT
#define DCHECK_EQ(lhs, rhs) MOZ_ASSERT((lhs) == (rhs))
#define DCHECK_NE(lhs, rhs) MOZ_ASSERT((lhs) != (rhs))
#define DCHECK_GT(lhs, rhs) MOZ_ASSERT((lhs) > (rhs))
#define DCHECK_GE(lhs, rhs) MOZ_ASSERT((lhs) >= (rhs))
#define DCHECK_LT(lhs, rhs) MOZ_ASSERT((lhs) < (rhs))
#define DCHECK_LE(lhs, rhs) MOZ_ASSERT((lhs) <= (rhs))
#define DCHECK_NULL(val) MOZ_ASSERT((val) == nullptr)
#define DCHECK_NOT_NULL(val) MOZ_ASSERT((val) != nullptr)
#define DCHECK_IMPLIES(lhs, rhs) MOZ_ASSERT_IF(lhs, rhs)
#define CHECK MOZ_RELEASE_ASSERT
#define CHECK_EQ(lhs, rhs) MOZ_RELEASE_ASSERT((lhs) == (rhs))
#define CHECK_NE(lhs, rhs) MOZ_RELEASE_ASSERT((lhs) != (rhs))
#define CHECK_GT(lhs, rhs) MOZ_RELEASE_ASSERT((lhs) > (rhs))
#define CHECK_GE(lhs, rhs) MOZ_RELEASE_ASSERT((lhs) >= (rhs))
#define CHECK_LT(lhs, rhs) MOZ_RELEASE_ASSERT((lhs) < (rhs))
#define CHECK_LE(lhs, rhs) MOZ_RELEASE_ASSERT((lhs) <= (rhs))
#define CHECK_IMPLIES(lhs, rhs) MOZ_RELEASE_ASSERT(!(lhs) || (rhs))
#define CONSTEXPR_DCHECK MOZ_ASSERT

// These assertions are necessary to preserve the soundness of the V8
// sandbox. In V8, they are debug-only if the sandbox is off, but release
// asserts if the sandbox is turned on. We don't have an equivalent sandbox,
// so they can be debug checks for now.
#define SBXCHECK MOZ_ASSERT
#define SBXCHECK_EQ(lhs, rhs) MOZ_ASSERT((lhs) == (rhs))
#define SBXCHECK_NE(lhs, rhs) MOZ_ASSERT((lhs) != (rhs))
#define SBXCHECK_GT(lhs, rhs) MOZ_ASSERT((lhs) > (rhs))
#define SBXCHECK_GE(lhs, rhs) MOZ_ASSERT((lhs) >= (rhs))
#define SBXCHECK_LT(lhs, rhs) MOZ_ASSERT((lhs) < (rhs))
#define SBXCHECK_LE(lhs, rhs) MOZ_ASSERT((lhs) <= (rhs))

// GET_NTH_ARG(N, ...) returns the Nth argument in the list of arguments
// following. Currently, up to N=8 is supported.
#define GET_NTH_ARG(N, ...) CONCAT(GET_NTH_ARG_IMPL_, N)(__VA_ARGS__)
#define GET_NTH_ARG_IMPL_0(_0, ...) _0
#define GET_NTH_ARG_IMPL_1(_0, _1, ...) _1
#define GET_NTH_ARG_IMPL_2(_0, _1, _2, ...) _2
#define GET_NTH_ARG_IMPL_3(_0, _1, _2, _3, ...) _3
#define GET_NTH_ARG_IMPL_4(_0, _1, _2, _3, _4, ...) _4
#define GET_NTH_ARG_IMPL_5(_0, _1, _2, _3, _4, _5, ...) _5
#define GET_NTH_ARG_IMPL_6(_0, _1, _2, _3, _4, _5, _6, ...) _6
#define GET_NTH_ARG_IMPL_7(_0, _1, _2, _3, _4, _5, _6, _7, ...) _7

// Expands to true if __VA_ARGS__ is empty, false otherwise.
#define IS_VA_EMPTY(...) GET_NTH_ARG(0, __VA_OPT__(false, ) true)

// UNPAREN(x) removes a layer of nested parentheses on x, if any. This means
// that both UNPAREN(x) and UNPAREN((x)) expand to x. This is helpful for macros
// that want to support multi argument templates with commas, e.g.
//
//   #define FOO(Type, Name) UNPAREN(Type) Name;
//
// will work with both
//
//   FOO(int, x);
//   FOO((Foo<int, double, float>), x);
#define CONCAT_(a, ...) a##__VA_ARGS__
#define CONCAT(a, ...) CONCAT_(a, __VA_ARGS__)
#define UNPAREN(X) CONCAT(DROP_, UNPAREN_ X)
#define UNPAREN_(...) UNPAREN_ __VA_ARGS__
#define DROP_UNPAREN_

#define MemCopy memcpy

#define PROFILE(isolate, event)

// Origin:
// https://github.com/v8/v8/blob/855591a54d160303349a5f0a32fab15825c708d1/src/base/macros.h#L310-L319
// ptrdiff_t is 't' according to the standard, but MSVC uses 'I'.
#ifdef _MSC_VER
#  define V8PRIxPTRDIFF "Ix"
#  define V8PRIdPTRDIFF "Id"
#  define V8PRIuPTRDIFF "Iu"
#else
#  define V8PRIxPTRDIFF "tx"
#  define V8PRIdPTRDIFF "td"
#  define V8PRIuPTRDIFF "tu"
#endif

// The arraysize(arr) macro returns the # of elements in an array arr.
// The expression is a compile-time constant, and therefore can be
// used in defining new arrays, for example.  If you use arraysize on
// a pointer by mistake, you will get a compile-time error.
#define arraysize(array) (sizeof(ArraySizeHelper(array)))

// This template function declaration is used in defining arraysize.
// Note that the function doesn't need an implementation, as we only
// use its type.
template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];
template <typename T, size_t N>
char (&ArraySizeHelper(const T (&array)[N]))[N];

// Explicitly declare the assignment operator as deleted.
#define DISALLOW_ASSIGN(TypeName) TypeName& operator=(const TypeName&) = delete

// Explicitly declare the copy constructor and assignment operator as deleted.
// This also deletes the implicit move constructor and implicit move assignment
// operator, but still allows to manually define them.
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  DISALLOW_ASSIGN(TypeName)

// Explicitly declare all implicit constructors as deleted, namely the
// default constructor, copy constructor and operator= functions.
// This is especially useful for classes containing only static methods.
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName() = delete;                           \
  DISALLOW_COPY_AND_ASSIGN(TypeName)

#define ZONE_NAME __func__

#if defined(JS_CODEGEN_ARM)
#  define kUnalignedReadSupported !js::jit::ARMFlags::HasAlignmentFault
#elif defined(JS_CODEGEN_MIPS64)
#  define kUnalignedReadSupported false
#else
#  define kUnalignedReadSupported true
#endif

namespace v8 {

// Origin:
// https://github.com/v8/v8/blob/855591a54d160303349a5f0a32fab15825c708d1/src/base/macros.h#L364-L367
template <typename T, typename U>
constexpr inline bool IsAligned(T value, U alignment) {
  return (value & (alignment - 1)) == 0;
}

using Address = uintptr_t;
static const Address kNullAddress = 0;

inline uintptr_t GetCurrentStackPosition() {
  return reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
}

// Return the largest multiple of m which is <= x.
template <typename T>
constexpr T RoundDown(T x, intptr_t m) {
  static_assert(std::is_integral_v<T>);
  // m must be a power of two.
  DCHECK(m != 0 && ((m & (m - 1)) == 0));
  return x & static_cast<T>(-m);
}
template <intptr_t m, typename T>
constexpr T RoundDown(T x) {
  static_assert(std::is_integral_v<T>);
  // m must be a power of two.
  static_assert(m != 0 && ((m & (m - 1)) == 0));
  return x & static_cast<T>(-m);
}

// Return the smallest multiple of m which is >= x.
template <typename T>
constexpr T RoundUp(T x, intptr_t m) {
  static_assert(std::is_integral_v<T>);
  DCHECK_GE(x, 0);
  DCHECK_GE(std::numeric_limits<T>::max() - x, m - 1);  // Overflow check.
  return RoundDown<T>(static_cast<T>(x + (m - 1)), m);
}

template <intptr_t m, typename T>
constexpr T RoundUp(T x) {
  static_assert(std::is_integral_v<T>);
  DCHECK_GE(x, 0);
  DCHECK_GE(std::numeric_limits<T>::max() - x, m - 1);  // Overflow check.
  return RoundDown<m, T>(static_cast<T>(x + (m - 1)));
}

namespace base {

// Latin1/UTF-16 constants
// Code-point values in Unicode 4.0 are 21 bits wide.
// Code units in UTF-16 are 16 bits wide.
using uc16 = char16_t;
using uc32 = uint32_t;

constexpr int kUC16Size = sizeof(base::uc16);

// Origin:
// https://github.com/v8/v8/blob/855591a54d160303349a5f0a32fab15825c708d1/src/base/macros.h#L247-L258
// The USE(x, ...) template is used to silence C++ compiler warnings
// issued for (yet) unused variables (typically parameters).
// The arguments are guaranteed to be evaluated from left to right.
struct Use {
  template <typename T>
  Use(T&&) {}  // NOLINT(runtime/explicit)
};
#define USE(...)                                                   \
  do {                                                             \
    ::v8::base::Use unused_tmp_array_for_use_macro[]{__VA_ARGS__}; \
    (void)unused_tmp_array_for_use_macro;                          \
  } while (false)

// Origin:
// https://github.com/v8/v8/blob/855591a54d160303349a5f0a32fab15825c708d1/src/base/safe_conversions.h#L35-L39
// saturated_cast<> is analogous to static_cast<> for numeric types, except
// that the specified numeric conversion will saturate rather than overflow or
// underflow.
template <typename Dst, typename Src>
inline Dst saturated_cast(Src value);

// These are the only specializations that are needed for regexp code.
// Instead of pulling in dozens of lines of template goo
// to derive it, I used the implementation from uint8_clamped in
// ArrayBufferObject.h.
template <>
inline uint8_t saturated_cast<uint8_t, int>(int x) {
  return (x >= 0) ? ((x < 255) ? uint8_t(x) : 255) : 0;
}
template <>
inline uint8_t saturated_cast<uint8_t, uint32_t>(uint32_t x) {
  return (x < 255) ? uint8_t(x) : 255;
}

// Origin:
// https://github.com/v8/v8/blob/fc088cdaccadede84886eee881e67af9db53669a/src/base/bounds.h#L14-L28
// Checks if value is in range [lower_limit, higher_limit] using a single
// branch.
template <typename T, typename U>
inline constexpr bool IsInRange(T value, U lower_limit, U higher_limit) {
  using unsigned_T = typename std::make_unsigned<T>::type;
  // Use static_cast to support enum classes.
  return static_cast<unsigned_T>(static_cast<unsigned_T>(value) -
                                 static_cast<unsigned_T>(lower_limit)) <=
         static_cast<unsigned_T>(static_cast<unsigned_T>(higher_limit) -
                                 static_cast<unsigned_T>(lower_limit));
}

#define LAZY_INSTANCE_INITIALIZER \
  {                               \
  }

template <typename T>
class LazyInstanceImpl {
 public:
  LazyInstanceImpl() : value_(js::mutexid::IrregexpLazyStatic) {}

  const T* Pointer() {
    auto val = value_.lock();
    if (val->isNothing()) {
      val->emplace();
    }
    return val->ptr();
  }

 private:
  js::ExclusiveData<mozilla::Maybe<T>> value_;
};

template <typename T>
class LazyInstance {
 public:
  using type = LazyInstanceImpl<T>;
};

// LeakyObject<T> wraps an object of type T, which is initialized in the
// constructor but never destructed. Thus LeakyObject<T> is trivially
// destructible and can be used in static (lazily initialized) variables.
template <typename T>
class LeakyObject {
 public:
  template <typename... Args>
  explicit LeakyObject(Args&&... args) {
    new (storage_) T(std::forward<Args>(args)...);
  }

  LeakyObject(const LeakyObject&) = delete;
  LeakyObject& operator=(const LeakyObject&) = delete;

  T* get() { return reinterpret_cast<T*>(storage_); }

 private:
  alignas(T) char storage_[sizeof(T)];
};

// Define a function which returns a pointer to a lazily initialized and never
// destructed object of type T, using a static local variable.
// This is Meyer's singleton, and is guaranteed to be thread-safe.
// See https://stackoverflow.com/a/1661564.
#define DEFINE_LAZY_LEAKY_OBJECT_GETTER(T, FunctionName, ...) \
  T* FunctionName() {                                         \
    static ::v8::base::LeakyObject<T> object{__VA_ARGS__};    \
    return object.get();                                      \
  }

// Origin:
// https://github.com/v8/v8/blob/855591a54d160303349a5f0a32fab15825c708d1/src/utils/utils.h#L40-L48
// Returns the value (0 .. 15) of a hexadecimal character c.
// If c is not a legal hexadecimal character, returns a value < 0.
// Used in regexp-parser.cc
inline int HexValue(base::uc32 c) {
  c -= '0';
  if (static_cast<unsigned>(c) <= 9) return c;
  c = (c | 0x20) - ('a' - '0');  // detect 0x11..0x16 and 0x31..0x36.
  if (static_cast<unsigned>(c) <= 5) return c + 10;
  return -1;
}

template <typename... Args>
[[nodiscard]] uint32_t hash_combine(uint32_t aHash, Args... aArgs) {
  return mozilla::AddToHash(aHash, aArgs...);
}

namespace bits {

inline uint64_t CountTrailingZeros(uint64_t value) {
  return std::countr_zero(value);
}

inline constexpr size_t RoundUpToPowerOfTwo32(size_t value) {
  return mozilla::RoundUpPow2(value);
}

inline constexpr size_t RoundUpToPowerOfTwo(size_t value) {
  return mozilla::RoundUpPow2(value);
}

template <typename T>
constexpr bool IsPowerOfTwo(T value) {
  return std::has_single_bit(value);
}

constexpr uint32_t CountPopulation(uint32_t value) {
  return std::popcount(value);
}

}  // namespace bits

namespace internal {

template <typename T>
class CheckedNumeric : public mozilla::CheckedInt<T> {
 public:
  template <typename U>
  MOZ_IMPLICIT constexpr CheckedNumeric(U val) : mozilla::CheckedInt<T>(val) {}

  // AssignIfValid(Dst) - Assigns the underlying value if it is currently valid
  // and is within the range supported by the destination type. Returns true if
  // successful and false otherwise.
  template <typename Dst>
  bool AssignIfValid(Dst* result) const {
    if (MOZ_LIKELY(this->isValid() && std::in_range<Dst>(this->value()))) {
      *result = this->value();
      return true;
    }
    return false;
  }
};

}  // namespace internal

}  // namespace base

namespace unibrow {

using uchar = unsigned int;

// Origin:
// https://github.com/v8/v8/blob/1f1e4cdb04c75eab77adbecd5f5514ddc3eb56cf/src/strings/unicode.h#L133-L150
class Latin1 {
 public:
  static const base::uc16 kMaxChar = 0xff;

  // Convert the character to Latin-1 case equivalent if possible.
  static inline base::uc16 TryConvertToLatin1(base::uc16 c) {
    // "GREEK CAPITAL LETTER MU" case maps to "MICRO SIGN".
    // "GREEK SMALL LETTER MU" case maps to "MICRO SIGN".
    if (c == 0x039C || c == 0x03BC) {
      return 0xB5;
    }
    // "LATIN CAPITAL LETTER Y WITH DIAERESIS" case maps to "LATIN SMALL LETTER
    // Y WITH DIAERESIS".
    if (c == 0x0178) {
      return 0xFF;
    }
    return c;
  }
};

// Origin:
// https://github.com/v8/v8/blob/b4bfbce6f91fc2cc72178af42bb3172c5f5eaebb/src/strings/unicode.h#L99-L131
class Utf16 {
 public:
  static inline bool IsLeadSurrogate(int code) {
    return js::unicode::IsLeadSurrogate(code);
  }
  static inline bool IsTrailSurrogate(int code) {
    return js::unicode::IsTrailSurrogate(code);
  }
  static inline base::uc16 LeadSurrogate(uint32_t char_code) {
    return js::unicode::LeadSurrogate(char_code);
  }
  static inline base::uc16 TrailSurrogate(uint32_t char_code) {
    return js::unicode::TrailSurrogate(char_code);
  }
  static inline uint32_t CombineSurrogatePair(char16_t lead, char16_t trail) {
    return js::unicode::UTF16Decode(lead, trail);
  }
  static const uchar kMaxNonSurrogateCharCode = 0xffff;
};

#ifndef V8_INTL_SUPPORT

// A cache used in case conversion.  It caches the value for characters
// that either have no mapping or map to a single character independent
// of context.  Characters that map to more than one character or that
// map differently depending on context are always looked up.
// Origin:
// https://github.com/v8/v8/blob/b4bfbce6f91fc2cc72178af42bb3172c5f5eaebb/src/strings/unicode.h#L64-L88
template <class T, int size = 256>
class Mapping {
 public:
  inline Mapping() = default;
  inline int get(uchar c, uchar n, uchar* result) {
    CacheEntry entry = entries_[c & kMask];
    if (entry.code_point_ == c) {
      if (entry.offset_ == 0) {
        return 0;
      } else {
        result[0] = c + entry.offset_;
        return 1;
      }
    } else {
      return CalculateValue(c, n, result);
    }
  }

 private:
  int CalculateValue(uchar c, uchar n, uchar* result) {
    bool allow_caching = true;
    int length = T::Convert(c, n, result, &allow_caching);
    if (allow_caching) {
      if (length == 1) {
        entries_[c & kMask] = CacheEntry(c, result[0] - c);
        return 1;
      } else {
        entries_[c & kMask] = CacheEntry(c, 0);
        return 0;
      }
    } else {
      return length;
    }
  }

  struct CacheEntry {
    inline CacheEntry() : code_point_(kNoChar), offset_(0) {}
    inline CacheEntry(uchar code_point, signed offset)
        : code_point_(code_point), offset_(offset) {}
    uchar code_point_;
    signed offset_;
    static const int kNoChar = (1 << 21) - 1;
  };
  static const int kSize = size;
  static const int kMask = kSize - 1;
  CacheEntry entries_[kSize];
};

// Origin:
// https://github.com/v8/v8/blob/b4bfbce6f91fc2cc72178af42bb3172c5f5eaebb/src/strings/unicode.h#L241-L252
struct Ecma262Canonicalize {
  static const int kMaxWidth = 1;
  static int Convert(uchar c, uchar n, uchar* result, bool* allow_caching_ptr);
};
struct Ecma262UnCanonicalize {
  static const int kMaxWidth = 4;
  static int Convert(uchar c, uchar n, uchar* result, bool* allow_caching_ptr);
};
struct CanonicalizationRange {
  static const int kMaxWidth = 1;
  static int Convert(uchar c, uchar n, uchar* result, bool* allow_caching_ptr);
};

#endif  // !V8_INTL_SUPPORT

struct Letter {
  static bool Is(uchar c);
};

}  // namespace unibrow

namespace internal {

#define PRINTF_FORMAT(x, y) MOZ_FORMAT_PRINTF(x, y)
void PRINTF_FORMAT(1, 2) PrintF(const char* format, ...);
void PRINTF_FORMAT(2, 3) PrintF(FILE* out, const char* format, ...);

// Superclass for classes only using static method functions.
// The subclass of AllStatic cannot be instantiated at all.
class AllStatic {
#ifdef DEBUG
 public:
  AllStatic() = delete;
#endif
};

// Superclass for classes managed with new and delete.
// In irregexp, this is only AlternativeGeneration (in regexp-compiler.cc)
// Compare:
// https://github.com/v8/v8/blob/7b3332844212d78ee87a9426f3a6f7f781a8fbfa/src/utils/allocation.cc#L88-L96
class Malloced {
 public:
  static void* operator new(size_t size) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    void* result = js_malloc(size);
    if (!result) {
      oomUnsafe.crash("Irregexp Malloced shim");
    }
    return result;
  }
  static void operator delete(void* p) { js_free(p); }
};

constexpr int32_t KB = 1024;
constexpr int32_t MB = 1024 * 1024;

#define kMaxInt JSVAL_INT_MAX
#define kMinInt JSVAL_INT_MIN
constexpr int kSystemPointerSize = sizeof(void*);
constexpr int kSystemPointerHexDigits = kSystemPointerSize == 4 ? 8 : 12;

// The largest integer n such that n and n + 1 are both exactly
// representable as a Number value.  ES6 section 20.1.2.6
constexpr uint64_t kMaxSafeIntegerUint64 = (uint64_t{1} << 53) - 1;
constexpr double kMaxSafeInteger = static_cast<double>(kMaxSafeIntegerUint64);

constexpr int kBitsPerByte = 8;
constexpr int kBitsPerByteLog2 = 3;
constexpr int kUInt16Size = sizeof(uint16_t);
constexpr int kInt32Size = sizeof(int32_t);
constexpr int kUInt32Size = sizeof(uint32_t);
constexpr int kInt64Size = sizeof(int64_t);

constexpr int kMaxUInt16 = (1 << 16) - 1;
constexpr uint32_t kMaxUInt32 = 0xffffffff;

inline constexpr bool IsDecimalDigit(base::uc32 c) {
  return c >= '0' && c <= '9';
}

inline constexpr int AsciiAlphaToLower(base::uc32 c) { return c | 0x20; }

inline bool is_uint8(int64_t val) { return (val >> 8) == 0; }

inline bool IsIdentifierStart(base::uc32 c) {
  return js::unicode::IsIdentifierStart(char32_t(c));
}
inline bool IsIdentifierPart(base::uc32 c) {
  return js::unicode::IsIdentifierPart(char32_t(c));
}

// Wrappers to disambiguate char16_t and uc16.
struct AsUC16 {
  explicit AsUC16(char16_t v) : value(v) {}
  char16_t value;
};

struct AsUC32 {
  explicit AsUC32(int32_t v) : value(v) {}
  int32_t value;
};

// Output the given value as hex, with a minimum width and optional prefix (0x).
// E.g. AsHex(23, 3, true) produces "0x017". Produces an empty string if both
// {min_width} and the value are 0.
struct AsHex {
  explicit AsHex(uint64_t v, uint8_t min_width = 1, bool with_prefix = false)
      : value(v), min_width(min_width), with_prefix(with_prefix) {}
  uint64_t value;
  uint8_t min_width;
  bool with_prefix;

  static AsHex Address(Address a) {
    return AsHex(a, kSystemPointerHexDigits, true);
  }
};

std::ostream& operator<<(std::ostream& os, const AsUC16& c);
std::ostream& operator<<(std::ostream& os, const AsUC32& c);
std::ostream& operator<<(std::ostream& os, const AsHex& c);

// This class is used for the output of trace-regexp-parser.  V8 has
// an elaborate implementation to ensure that the output gets to the
// right place, even on Android. We just need something that will
// print output (ideally to stderr, to match the rest of our tracing
// code). This is an empty wrapper that will convert itself to
// std::cerr when used.
class StdoutStream : public std::ostream {
 public:
  StdoutStream();
};

// Reuse existing Maybe implementation
using mozilla::Maybe;

template <typename T>
Maybe<T> Just(const T& value) {
  return mozilla::Some(value);
}

template <typename T>
mozilla::Nothing Nothing() {
  return mozilla::Nothing();
}

template <typename T>
using PseudoHandle = mozilla::UniquePtr<T, JS::FreePolicy>;

// Compare 8bit/16bit chars to 8bit/16bit chars.
// Used indirectly by regexp-interpreter.cc
// Taken from: https://github.com/v8/v8/blob/master/src/utils/utils.h
template <typename lchar, typename rchar>
inline int CompareCharsUnsigned(const lchar* lhs, const rchar* rhs,
                                size_t chars) {
  const lchar* limit = lhs + chars;
  if (sizeof(*lhs) == sizeof(char) && sizeof(*rhs) == sizeof(char)) {
    // memcmp compares byte-by-byte, yielding wrong results for two-byte
    // strings on little-endian systems.
    return memcmp(lhs, rhs, chars);
  }
  while (lhs < limit) {
    int r = static_cast<int>(*lhs) - static_cast<int>(*rhs);
    if (r != 0) return r;
    ++lhs;
    ++rhs;
  }
  return 0;
}
template <typename lchar, typename rchar>
inline int CompareChars(const lchar* lhs, const rchar* rhs, size_t chars) {
  DCHECK_LE(sizeof(lchar), 2);
  DCHECK_LE(sizeof(rchar), 2);
  if (sizeof(lchar) == 1) {
    if (sizeof(rchar) == 1) {
      return CompareCharsUnsigned(reinterpret_cast<const uint8_t*>(lhs),
                                  reinterpret_cast<const uint8_t*>(rhs), chars);
    } else {
      return CompareCharsUnsigned(reinterpret_cast<const uint8_t*>(lhs),
                                  reinterpret_cast<const char16_t*>(rhs),
                                  chars);
    }
  } else {
    if (sizeof(rchar) == 1) {
      return CompareCharsUnsigned(reinterpret_cast<const char16_t*>(lhs),
                                  reinterpret_cast<const uint8_t*>(rhs), chars);
    } else {
      return CompareCharsUnsigned(reinterpret_cast<const char16_t*>(lhs),
                                  reinterpret_cast<const char16_t*>(rhs),
                                  chars);
    }
  }
}

// Compare 8bit/16bit chars to 8bit/16bit chars.
template <typename lchar, typename rchar>
inline bool CompareCharsEqualUnsigned(const lchar* lhs, const rchar* rhs,
                                      size_t chars) {
  STATIC_ASSERT(std::is_unsigned<lchar>::value);
  STATIC_ASSERT(std::is_unsigned<rchar>::value);
  if (sizeof(*lhs) == sizeof(*rhs)) {
    // memcmp compares byte-by-byte, but for equality it doesn't matter whether
    // two-byte char comparison is little- or big-endian.
    return memcmp(lhs, rhs, chars * sizeof(*lhs)) == 0;
  }
  for (const lchar* limit = lhs + chars; lhs < limit; ++lhs, ++rhs) {
    if (*lhs != *rhs) return false;
  }
  return true;
}

template <typename lchar, typename rchar>
inline bool CompareCharsEqual(const lchar* lhs, const rchar* rhs,
                              size_t chars) {
  using ulchar = typename std::make_unsigned<lchar>::type;
  using urchar = typename std::make_unsigned<rchar>::type;
  return CompareCharsEqualUnsigned(reinterpret_cast<const ulchar*>(lhs),
                                   reinterpret_cast<const urchar*>(rhs), chars);
}

// V8::Object ~= JS::Value
class Object {
 public:
  // The default object constructor in V8 stores a nullptr,
  // which has its low bit clear and is interpreted as Smi(0).
  constexpr Object() : asBits_(JS::Int32Value(0).asRawBits()) {}

  Object(const JS::Value& value) : asBits_(value.asRawBits()) {}

  // This constructor is only used in an unused implementation of
  // IsCharacterInRangeArray in regexp-macro-assembler.cc.
  Object(uintptr_t raw) : asBits_(raw) { MOZ_CRASH("unused"); }

  JS::Value value() const { return JS::Value::fromRawBits(asBits_); }

  inline static Object cast(Object object) { return object; }

 protected:
  void setValue(const JS::Value& val) { asBits_ = val.asRawBits(); }
  uint64_t asBits_;
} JS_HAZ_GC_POINTER;

// Used in regexp-interpreter.cc to check the return value of
// isolate->stack_guard()->HandleInterrupts(). We want to handle
// interrupts in the caller, so we return a magic value from
// HandleInterrupts and check for it here.
inline bool IsExceptionHole(Object obj) {
  return obj.value().isMagic(JS_INTERRUPT_REGEXP);
}

class Smi : public Object {
 public:
  static Smi FromInt(int32_t value) {
    Smi smi;
    smi.setValue(JS::Int32Value(value));
    return smi;
  }
  static inline int32_t ToInt(const Object object) {
    return object.value().toInt32();
  }
};

// V8::HeapObject ~= GC thing
class HeapObject : public Object {
 public:
  inline static HeapObject cast(Object object) {
    HeapObject h;
    h.setValue(object.value());
    return h;
  }
};

// V8's values use low-bit tagging. If the LSB is 0, it's a small
// integer. If the LSB is 1, it's a pointer to some GC thing. In V8,
// this wrapper class is used to represent a pointer that has the low
// bit set, or a small integer that has been shifted left by one
// bit. We don't use the same tagging system, so all we need is a
// transparent wrapper that automatically converts to/from the wrapped
// type.
template <typename T>
class Tagged {
 public:
  Tagged() = default;
  MOZ_IMPLICIT Tagged(const T& value) : value_(value) {}
  MOZ_IMPLICIT Tagged(T&& value) : value_(std::move(value)) {}

  T* operator->() { return &value_; }
  constexpr operator T() const { return value_; }

 private:
  T value_;
};

// Adapted from v8/src/objects/casting.h

template <typename To, typename From>
inline Tagged<To> UncheckedCast(Tagged<From> value) {
  return Tagged<To>(To::cast(value));
}

template <typename To, typename From>
inline Tagged<To> Cast(const From& value) {
  return UncheckedCast<To>(Tagged(value));
}

// A fixed-size array with Objects (aka Values) as element types.
// Implemented using the dense elements of an ArrayObject.
// Used for named captures.
class FixedArray : public HeapObject {
 public:
  inline void set(uint32_t index, Object value) {
    inner()->setDenseElement(index, value.value());
  }
  inline static FixedArray cast(Object object) {
    FixedArray f;
    MOZ_ASSERT(object.value().isObject() &&
               object.value().toObject().is<js::ArrayObject>());
    f.setValue(object.value());
    return f;
  }
  js::NativeObject* inner() {
    return &value().toObject().as<js::NativeObject>();
  }
};

/*
 * Conceptually, ByteArrayData is a variable-size structure. To
 * implement this in a C++-approved way, we allocate a struct
 * containing the 32-bit length field, followed by additional memory
 * for the data. To access the data, we get a pointer to the next byte
 * after the length field and cast it to the correct type.
 */
inline uint8_t* ByteArrayData::data() {
  static_assert(alignof(uint8_t) <= alignof(ByteArrayData),
                "The trailing data must be aligned to start immediately "
                "after the header with no padding.");
  ByteArrayData* immediatelyAfter = this + 1;
  return reinterpret_cast<uint8_t*>(immediatelyAfter);
}

template <typename T>
T* ByteArrayData::typedData() {
  static_assert(alignof(T) <= alignof(ByteArrayData));
  MOZ_ASSERT(uintptr_t(data()) % alignof(T) == 0);
  return reinterpret_cast<T*>(data());
}

template <typename T>
T ByteArrayData::getTyped(uint32_t index) {
  MOZ_ASSERT(index < length() / sizeof(T));
  return typedData<T>()[index];
}

// V8 uses this wrapper class as an alias for uint32_t to help enforce
// sandbox safety. See https://issues.chromium.org/issues/425896305.
class SafeHeapObjectSize {
  uint32_t value_;

 public:
  explicit SafeHeapObjectSize(uint32_t value) : value_(value) {}
  uint32_t value() { return value_; }
};

template <typename T>
void ByteArrayData::setTyped(uint32_t index, T value) {
  MOZ_ASSERT(index < length() / sizeof(T));
  typedData<T>()[index] = value;
}

// A fixed-size array of bytes.
class ByteArray : public HeapObject {
 protected:
  ByteArrayData* inner() const {
    return static_cast<ByteArrayData*>(value().toPrivate());
  }
  friend bool IsByteArray(Object obj);

 public:
  PseudoHandle<ByteArrayData> takeOwnership(Isolate* isolate);
  PseudoHandle<ByteArrayData> maybeTakeOwnership(Isolate* isolate);

  uint8_t get(uint32_t index) { return inner()->get(index); }
  void set(uint32_t index, uint8_t val) { inner()->set(index, val); }

  SafeHeapObjectSize length() const {
    return SafeHeapObjectSize(inner()->length());
  }
  SafeHeapObjectSize ulength() const { return length(); }
  uint8_t* begin() { return inner()->data(); }
  uint8_t* end() { return inner()->data() + inner()->length(); }

  static ByteArray cast(Object object) {
    ByteArray b;
    b.setValue(object.value());
    return b;
  }

  friend class regexp::SMRegExpMacroAssembler;
};

// A byte array that can be trusted to not contain malicious data.
// See https://issues.chromium.org/issues/40069826.
class TrustedByteArray : public ByteArray {
 public:
  static TrustedByteArray cast(Object object) {
    TrustedByteArray b;
    b.setValue(object.value());
    return b;
  }
};

// This is only used in assertions. In debug builds, we put a magic value
// in the header of each ByteArrayData, and assert here that it matches.
inline bool IsByteArray(Object obj) {
  MOZ_ASSERT(ByteArray::cast(obj).inner()->magic() ==
             ByteArrayData::ExpectedMagic);
  return true;
}

// This is a convenience class used in V8 for treating a ByteArray as an array
// of fixed-size integers. This version supports integral types up to 32 bits.
template <typename T>
class FixedIntegerArray : public ByteArray {
  static_assert(alignof(T) <= alignof(ByteArrayData));
  static_assert(std::is_integral<T>::value);

 public:
  static Handle<FixedIntegerArray<T>> New(Isolate* isolate, uint32_t length);

  T get(uint32_t index) { return inner()->template getTyped<T>(index); };
  void set(uint32_t index, T value) {
    inner()->template setTyped<T>(index, value);
  }

  static FixedIntegerArray<T> cast(Object object) {
    FixedIntegerArray<T> f;
    f.setValue(object.value());
    return f;
  }

  SafeHeapObjectSize length() const {
    uint32_t byteLength = ByteArray::length().value();
    MOZ_ASSERT(byteLength % sizeof(T) == 0);
    return SafeHeapObjectSize(byteLength / sizeof(T));
  }
};

using FixedUInt16Array = FixedIntegerArray<uint16_t>;

// Like Handles in SM, V8 handles are references to marked pointers.
// Unlike SM, where Rooted pointers are created individually on the
// stack, the target of a V8 handle lives in an arena on the isolate
// (~= JSContext). Whenever a Handle is created, a new "root" is
// created at the end of the arena.
//
// HandleScopes are used to manage the lifetimes of these handles.  A
// HandleScope lives on the stack and stores the size of the arena at
// the time of its creation. When the function returns and the
// HandleScope is destroyed, the arena is truncated to its previous
// size, clearing all roots that were created since the creation of
// the HandleScope.
//
// In some cases, objects that are GC-allocated in V8 are not in SM.
// In particular, irregexp allocates ByteArrays during code generation
// to store lookup tables. This does not play nicely with the SM
// macroassembler's requirement that no GC allocations take place
// while it is on the stack. To work around this, this shim layer also
// provides the ability to create pseudo-handles, which are not
// managed by the GC but provide the same API to irregexp. The "root"
// of a pseudohandle is a unique pointer living in a second arena. If
// the allocated object should outlive the HandleScope, it must be
// manually moved out of the arena using maybeTakeOwnership.
// (If maybeTakeOwnership is called multiple times, it will return
// a null pointer on subsequent calls.)

class MOZ_STACK_CLASS HandleScope {
 public:
  HandleScope(Isolate* isolate);
  ~HandleScope();

 private:
  size_t level_ = 0;
  size_t non_gc_level_ = 0;
  Isolate* isolate_;

  friend class Isolate;
};

// Origin:
// https://github.com/v8/v8/blob/5792f3587116503fc047d2f68c951c72dced08a5/src/handles/handles.h#L88-L171
template <typename T>
class MOZ_NONHEAP_CLASS Handle {
 public:
  Handle() : location_(nullptr) {}
  Handle(T object, Isolate* isolate);
  Handle(const JS::Value& value, Isolate* isolate);

  // Constructor for handling automatic up casting.
  template <typename S,
            typename = std::enable_if_t<std::is_convertible_v<S*, T*>>>
  inline Handle(Handle<S> handle) : location_(handle.location_) {}

  static Handle null() { return Handle(); }
  inline bool is_null() const { return location_ == nullptr; }

  inline T operator*() const { return T::cast(Object(*location_)); };

  template <typename S>
  inline static Handle<T> cast(Handle<S> that) {
    T::cast(Object(*that.location_));
    return Handle<T>(that.location_);
  }

  // {ObjectRef} is returned by {Handle::operator->}. It should never be stored
  // anywhere or used in any other code; no one should ever have to spell out
  // {ObjectRef} in code. Its only purpose is to be dereferenced immediately by
  // "operator-> chaining". Returning the address of the field is valid because
  // this object's lifetime only ends at the end of the full statement.
  // Origin:
  // https://github.com/v8/v8/blob/03aaa4b3bf4cb01eee1f223b252e6869b04ab08c/src/handles/handles.h#L91-L105
  class MOZ_TEMPORARY_CLASS ObjectRef {
   public:
    T* operator->() { return &object_; }

   private:
    friend class Handle;
    explicit ObjectRef(T object) : object_(object) {}

    T object_;
  };
  inline ObjectRef operator->() const { return ObjectRef{**this}; }

  static Handle<T> fromHandleValue(JS::HandleValue handle) {
    return Handle(handle.address());
  }

 private:
  Handle(const JS::Value* location) : location_(location) {}

  template <typename>
  friend class Handle;
  template <typename>
  friend class MaybeHandle;

  const JS::Value* location_;
};

template <typename To, typename From>
inline Handle<To> CheckedCast(Handle<From> value) {
  return Handle<To>::cast(value);
}

// A Handle can be converted into a MaybeHandle. Converting a MaybeHandle
// into a Handle requires checking that it does not point to nullptr.  This
// ensures nullptr checks before use.
//
// Also note that Handles do not provide default equality comparison or hashing
// operators on purpose. Such operators would be misleading, because intended
// semantics is ambiguous between Handle location and object identity.
// Origin:
// https://github.com/v8/v8/blob/5792f3587116503fc047d2f68c951c72dced08a5/src/handles/maybe-handles.h#L15-L78
template <typename T>
class MOZ_NONHEAP_CLASS MaybeHandle final {
 public:
  MaybeHandle() : location_(nullptr) {}

  // Constructor for handling automatic up casting from Handle.
  // Ex. Handle<JSArray> can be passed when MaybeHandle<Object> is expected.
  template <typename S,
            typename = std::enable_if_t<std::is_convertible_v<S*, T*>>>
  MaybeHandle(Handle<S> handle) : location_(handle.location_) {}

  inline Handle<T> ToHandleChecked() const {
    MOZ_RELEASE_ASSERT(location_);
    return Handle<T>(location_);
  }

  // Convert to a Handle with a type that can be upcasted to.
  template <typename S>
  inline bool ToHandle(Handle<S>* out) const {
    if (location_) {
      *out = Handle<T>(location_);
      return true;
    } else {
      *out = Handle<T>();
      return false;
    }
  }

 private:
  JS::Value* location_;
};

// From v8/src/handles/handles-inl.h

template <typename T>
inline Handle<T> handle(T object, Isolate* isolate) {
  return Handle<T>(object, isolate);
}

// V8 is migrating to a conservative stack scanning approach. When that
// is enabled, a DirectHandle points directly at the V8 heap, and an
// IndirectHandle is an unmigrated old-style Handle with a layer of
// indirection. When disabled (which matches our implementation) the two
// types are the same. See:
// https://github.com/v8/v8/blob/887ec63c43e23c4fefba1c52d4525654bdc71e5b/src/common/globals.h#L1000-L1012
template <typename T>
using DirectHandle = Handle<T>;

template <typename T>
using IndirectHandle = Handle<T>;

template <typename T>
using MaybeDirectHandle = MaybeHandle<T>;

// RAII Guard classes

using DisallowGarbageCollection = JS::AutoAssertNoGC;

// V8 uses this inside DisallowGarbageCollection regions to turn
// allocation back on before throwing a stack overflow exception or
// handling interrupts. AutoSuppressGC is sufficient for the former
// case, but not for the latter: handling interrupts can execute
// arbitrary script code, and V8 jumps through some scary hoops to
// "manually relocate unhandlified references" afterwards. To keep
// things sane, we don't try to handle interrupts while regex code is
// still on the stack. Instead, we return EXCEPTION and handle
// interrupts in the caller. (See RegExpShared::execute.)

class AllowGarbageCollection {
 public:
  AllowGarbageCollection() = default;
};

// Origin:
// https://github.com/v8/v8/blob/84f3877c15bc7f8956d21614da4311337525a3c8/src/objects/string.h#L83-L474
class String : public HeapObject {
 private:
  JSString* str() const { return value().toString(); }

 public:
  String() = default;
  String(JSString* str) { setValue(JS::StringValue(str)); }

  operator JSString*() const { return str(); }

  // Max char codes.
  static const int32_t kMaxOneByteCharCode = unibrow::Latin1::kMaxChar;
  static const uint32_t kMaxOneByteCharCodeU = unibrow::Latin1::kMaxChar;
  static const int kMaxUtf16CodeUnit = 0xffff;
  static const uint32_t kMaxUtf16CodeUnitU = kMaxUtf16CodeUnit;
  static const base::uc32 kMaxCodePoint = 0x10ffff;

  MOZ_ALWAYS_INLINE int length() const { return str()->length(); }
  bool IsFlat() { return str()->isLinear(); };

  // Origin:
  // https://github.com/v8/v8/blob/84f3877c15bc7f8956d21614da4311337525a3c8/src/objects/string.h#L95-L152
  class FlatContent {
   public:
    FlatContent(JSLinearString* string, const DisallowGarbageCollection& no_gc)
        : string_(string), no_gc_(no_gc) {}
    inline bool IsOneByte() const { return string_->hasLatin1Chars(); }
    inline bool IsTwoByte() const { return !string_->hasLatin1Chars(); }

    base::Vector<const uint8_t> ToOneByteVector() const {
      MOZ_ASSERT(IsOneByte());
      return base::Vector<const uint8_t>(string_->latin1Chars(no_gc_),
                                         string_->length());
    }
    base::Vector<const base::uc16> ToUC16Vector() const {
      MOZ_ASSERT(IsTwoByte());
      return base::Vector<const base::uc16>(string_->twoByteChars(no_gc_),
                                            string_->length());
    }
    void UnsafeDisableChecksumVerification() {
      // Intentional no-op. See the comment for AllowGarbageCollection above.
    }

   private:
    const JSLinearString* string_;
    const JS::AutoAssertNoGC& no_gc_;
  };
  FlatContent GetFlatContent(const DisallowGarbageCollection& no_gc) {
    MOZ_ASSERT(IsFlat());
    return FlatContent(&str()->asLinear(), no_gc);
  }

  static Handle<String> Flatten(Isolate* isolate, Handle<String> string);

  inline static String cast(Object object) {
    String s;
    MOZ_ASSERT(object.value().isString());
    s.setValue(object.value());
    return s;
  }

  inline static bool IsOneByteRepresentationUnderneath(String string) {
    return string.str()->hasLatin1Chars();
  }
  inline bool IsOneByteRepresentation() const {
    return str()->hasLatin1Chars();
  }

  std::unique_ptr<char[]> ToCString();

  template <typename Char>
  base::Vector<const Char> GetCharVector(
      const DisallowGarbageCollection& no_gc);

  friend class regexp::Utils;
};

template <>
inline base::Vector<const uint8_t> String::GetCharVector(
    const DisallowGarbageCollection& no_gc) {
  String::FlatContent flat = GetFlatContent(no_gc);
  MOZ_ASSERT(flat.IsOneByte());
  return flat.ToOneByteVector();
}

template <>
inline base::Vector<const base::uc16> String::GetCharVector(
    const DisallowGarbageCollection& no_gc) {
  String::FlatContent flat = GetFlatContent(no_gc);
  MOZ_ASSERT(flat.IsTwoByte());
  return flat.ToUC16Vector();
}

namespace regexp {
using Flags = JS::RegExpFlags;
using Flag = JS::RegExpFlags::Flag;
}  // namespace regexp

class JSRegExp {
 public:
  // Each capture (including the match itself) needs two registers.
  static constexpr int RegistersForCaptureCount(int count) {
    return (count + 1) * 2;
  }

  static regexp::Flags AsRegExpFlags(regexp::Flags flags) { return flags; }
  static regexp::Flags AsJSRegExpFlags(regexp::Flags flags) { return flags; }

  static Handle<String> StringFromFlags(Isolate* isolate, regexp::Flags flags);

  // ******************************
  // Static constants
  // ******************************

  static constexpr int kMaxCaptures = (1 << 15) - 1;

  static constexpr int kNoBacktrackLimit = 0;
};

class RegExpData : public HeapObject {
 public:
  RegExpData() : HeapObject() {}
  RegExpData(js::RegExpShared* re) { setValue(JS::PrivateGCThingValue(re)); }

  // ******************************************************
  // Methods that are called from inside the implementation
  // ******************************************************
  void TierUpTick() { inner()->tierUpTick(); }

  Tagged<TrustedByteArray> bytecode(bool is_latin1) const {
    return TrustedByteArray::cast(
        Object(JS::PrivateValue(inner()->getByteCode(is_latin1))));
  }

  bool has_bytecode(bool is_latin1) const {
    return inner()->getByteCode(is_latin1) != nullptr;
  }

  // TODO: should we expose this?
  uint32_t backtrack_limit() const { return 0; }

  static RegExpData cast(Object object) {
    RegExpData regexp;
    js::gc::Cell* regexpShared = object.value().toGCThing();
    MOZ_ASSERT(regexpShared->is<js::RegExpShared>());
    regexp.setValue(JS::PrivateGCThingValue(regexpShared));
    return regexp;
  }

  inline uint32_t max_register_count() const {
    return inner()->getMaxRegisters();
  }

  regexp::Flags flags() const { return inner()->getFlags(); }

  size_t capture_count() const {
    // Subtract 1 because pairCount includes the implicit global capture.
    return inner()->pairCount() - 1;
  }

  Tagged<String> escaped_source() const { return String(inner()->getSource()); }

 private:
  js::RegExpShared* inner() const {
    return value().toGCThing()->as<js::RegExpShared>();
  }
};

class IrRegExpData : public RegExpData {
 public:
  IrRegExpData() : RegExpData() {}
  IrRegExpData(js::RegExpShared* re) : RegExpData(re) {}

  static IrRegExpData cast(Object object) {
    IrRegExpData regexp;
    js::gc::Cell* regexpShared = object.value().toGCThing();
    MOZ_ASSERT(regexpShared->is<js::RegExpShared>());
    regexp.setValue(JS::PrivateGCThingValue(regexpShared));
    return regexp;
  }
};

inline bool IsUnicode(regexp::Flags flags) { return flags.unicode(); }
inline bool IsGlobal(regexp::Flags flags) { return flags.global(); }
inline bool IsIgnoreCase(regexp::Flags flags) { return flags.ignoreCase(); }
inline bool IsMultiline(regexp::Flags flags) { return flags.multiline(); }
inline bool IsDotAll(regexp::Flags flags) { return flags.dotAll(); }
inline bool IsSticky(regexp::Flags flags) { return flags.sticky(); }
inline bool IsUnicodeSets(regexp::Flags flags) { return flags.unicodeSets(); }
inline bool IsEitherUnicode(regexp::Flags flags) {
  return flags.unicode() || flags.unicodeSets();
}

inline std::optional<regexp::Flag> TryFlagFromChar(char c) {
  regexp::Flag flag;

  // The parser only calls this after verifying that it's a supported flag.
  if (JS::MaybeParseRegExpFlag(c, &flag)) {
    return flag;
  }

  return std::optional<regexp::Flag>{};
}

class Histogram {
 public:
  inline void AddSample(int sample) {}
};

class Counters {
 public:
  Histogram* regexp_backtracks() { return &regexp_backtracks_; }

 private:
  Histogram regexp_backtracks_;
};

class LocalHeap {
 public:
  using GCEpilogueCallback = void(void*);

  // Adds a callback that is invoked with the given |data| after each GC.
  // V8 uses this to update RegExpBytecodeIterator so that it is not
  // invalidated if the GC moves the underlying ByteArray. We implement
  // ByteArray using non-GC memory, so this is not relevant to us.
  void AddGCEpilogueCallback(GCEpilogueCallback, void*) {}
  void RemoveGCEpilogueCallback(GCEpilogueCallback, void*) {}
};

enum class AllocationType : uint8_t {
  kYoung,  // Allocate in the nursery
  kOld,    // Allocate in the tenured heap
};

using StackGuard = Isolate;
using Factory = Isolate;

class Isolate {
 public:
  Isolate(JSContext* cx) : cx_(cx) {}
  ~Isolate();
  bool init();

  static Isolate* Current() { return js::TlsContext.get()->isolate; }

  size_t sizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

  //********** Isolate code **********//
  regexp::Stack* regexp_stack() const { return regexpStack_; }

  js::LifoAlloc* allocator() { return &cx_->tempLifoAlloc(); }

  // This is called from inside no-GC code. Instead of suppressing GC
  // to allocate the error, we return false from Execute and call
  // ReportOverRecursed in the caller.
  void StackOverflow() {}

#ifndef V8_INTL_SUPPORT
  unibrow::Mapping<unibrow::Ecma262UnCanonicalize>* jsregexp_uncanonicalize() {
    return &jsregexp_uncanonicalize_;
  }
  unibrow::Mapping<unibrow::Ecma262Canonicalize>*
  regexp_macro_assembler_canonicalize() {
    return &regexp_macro_assembler_canonicalize_;
  }
  unibrow::Mapping<unibrow::CanonicalizationRange>* jsregexp_canonrange() {
    return &jsregexp_canonrange_;
  }

 private:
  unibrow::Mapping<unibrow::Ecma262UnCanonicalize> jsregexp_uncanonicalize_;
  unibrow::Mapping<unibrow::Ecma262Canonicalize>
      regexp_macro_assembler_canonicalize_;
  unibrow::Mapping<unibrow::CanonicalizationRange> jsregexp_canonrange_;
#endif  // !V8_INTL_SUPPORT

 public:
  // An empty stub for telemetry we don't support
  void IncreaseTotalRegexpCodeGenerated(Handle<HeapObject> code) {}

  Counters* counters() { return &counters_; }

  LocalHeap* main_thread_local_heap() { return &main_thread_local_heap_; }

  //********** Factory code **********//
  inline Factory* factory() { return this; }

  Handle<ByteArray> NewByteArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  Handle<TrustedByteArray> NewTrustedByteArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a fixed array initialized with undefined values.
  Handle<FixedArray> NewFixedArray(int length);

  template <typename T>
  Handle<FixedIntegerArray<T>> NewFixedIntegerArray(uint32_t length);

  template <typename Char>
  Handle<String> InternalizeString(const base::Vector<const Char>& str);

  //********** Stack guard code **********//
  inline StackGuard* stack_guard() { return this; }

  uintptr_t real_climit() { return cx_->stackLimit(JS::StackForSystemCode); }

  // This is called from inside no-GC code. V8 runs the interrupt
  // inside the no-GC code and then "manually relocates unhandlified
  // references" afterwards. We just return a magic value and let the
  // caller handle interrupts.
  Object HandleInterrupts() {
    return Object(JS::MagicValue(JS_INTERRUPT_REGEXP));
  }

  JSContext* cx() const { return cx_; }

  void trace(JSTracer* trc);

  //********** Handle code **********//

  JS::Value* getHandleLocation(const JS::Value& value);

 private:
  mozilla::SegmentedVector<JS::Value, 256> handleArena_;
  mozilla::SegmentedVector<PseudoHandle<void>, 256> uniquePtrArena_;

  void* allocatePseudoHandle(size_t bytes);

 public:
  template <typename T>
  PseudoHandle<T> takeOwnership(void* ptr);
  template <typename T>
  PseudoHandle<T> maybeTakeOwnership(void* ptr);

  uint32_t liveHandles() const { return handleArena_.Length(); }
  uint32_t livePseudoHandles() const { return uniquePtrArena_.Length(); }

 private:
  void openHandleScope(HandleScope& scope) {
    scope.level_ = handleArena_.Length();
    scope.non_gc_level_ = uniquePtrArena_.Length();
  }
  void closeHandleScope(size_t prevLevel, size_t prevUniqueLevel) {
    size_t currLevel = handleArena_.Length();
    handleArena_.PopLastN(currLevel - prevLevel);

    size_t currUniqueLevel = uniquePtrArena_.Length();
    uniquePtrArena_.PopLastN(currUniqueLevel - prevUniqueLevel);
  }
  friend class HandleScope;

  JSContext* cx_;
  regexp::Stack* regexpStack_{};
  Counters counters_{};
  LocalHeap main_thread_local_heap_;
#ifdef DEBUG
 public:
  uint32_t shouldSimulateInterrupt_ = 0;
#endif
};

// Origin:
// https://github.com/v8/v8/blob/50dcf2af54ce27801a71c47c1be1d2c5e36b0dd6/src/execution/isolate.h#L1909-L1931
class StackLimitCheck {
 public:
  StackLimitCheck(Isolate* isolate) : cx_(isolate->cx()) {}

  // Use this to check for stack-overflows in C++ code.
  bool HasOverflowed() {
    js::AutoCheckRecursionLimit recursion(cx_);
    bool overflowed = !recursion.checkDontReport(cx_);
    if (overflowed && js::SupportDifferentialTesting()) {
      // We don't report overrecursion here, but we throw an exception later
      // and this still affects differential testing. Mimic ReportOverRecursed
      // (the fuzzers check for this particular string).
      fprintf(stderr, "ReportOverRecursed called\n");
    }
    return overflowed;
  }

  // Use this to check for interrupt request in C++ code.
  bool InterruptRequested() {
    return cx_->hasPendingInterrupt(js::InterruptReason::CallbackUrgent);
  }

  // Use this to check for stack-overflow when entering runtime from JS code.
  bool JsHasOverflowed() {
    js::AutoCheckRecursionLimit recursion(cx_);
    return !recursion.checkDontReport(cx_);
  }

 private:
  JSContext* cx_;
};

class ExternalReference {
 public:
  static const void* TopOfRegexpStack(Isolate* isolate);
  static size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf,
                                    regexp::Stack* regexpStack);
};

class Code : public HeapObject {
 public:
  uint8_t* raw_instruction_start() { return inner()->raw(); }

  static Code cast(Object object) {
    Code c;
    js::gc::Cell* jitCode = object.value().toGCThing();
    MOZ_ASSERT(jitCode->is<js::jit::JitCode>());
    c.setValue(JS::PrivateGCThingValue(jitCode));
    return c;
  }
  js::jit::JitCode* inner() {
    return value().toGCThing()->as<js::jit::JitCode>();
  }
};

// Only used in function signature of functions we don't implement
// (NativeRegExpMacroAssembler::CheckStackGuardState)
class InstructionStream {};

namespace regexp {

inline bool operator==(const Flags& lhs, const int& rhs) {
  return lhs.value() == rhs;
}
inline bool operator!=(const Flags& lhs, const int& rhs) {
  return !(lhs == rhs);
}

// Only used in the definition of RegExpGlobalExecRunner, which we don't use.
class ResultVectorScope {};

class Utils {
 public:
  static uint64_t AdvanceStringIndex(Tagged<String> string, uint64_t index,
                                     bool unicode);
};
}  // namespace regexp

// Origin: https://github.com/v8/v8/blob/master/src/codegen/label.h
class Label {
 public:
  Label() : inner_(js::jit::Label()) {}

  js::jit::Label* inner() { return &inner_; }

  void Unuse() { inner_.reset(); }
  void UnuseNear() { inner_.reset(); }

  bool is_linked() { return inner_.used(); }
  bool is_bound() { return inner_.bound(); }
  bool is_unused() { return !inner_.used() && !inner_.bound(); }

  int pos() { return inner_.offset(); }
  void link_to(int pos) { inner_.use(pos); }
  void bind_to(int pos) { inner_.bind(pos); }

 private:
  js::jit::Label inner_;
  js::jit::CodeOffset patchOffset_;

  friend class regexp::SMRegExpMacroAssembler;
};

#define v8_flags js::jit::JitOptions

#define V8_USE_COMPUTED_GOTO 1
#define COMPILING_IRREGEXP_FOR_EXTERNAL_EMBEDDER

}  // namespace internal
}  // namespace v8

namespace V8 {

inline void FatalProcessOutOfMemory(v8::internal::Isolate* isolate,
                                    const char* msg) {
  js::AutoEnterOOMUnsafeRegion oomUnsafe;
  oomUnsafe.crash(msg);
}

}  // namespace V8

#endif  // RegexpShim_h
