/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_UsingEnum_h
#define mozilla_UsingEnum_h

#include "mozilla/MacroForEach.h"

// C++20 `using enum` declaration isn't supported in GCC 10. Provide a simple
// macro until our minimum supported GCC has been upgraded to GCC 11. We can
// remove this header and `MOZ_USING_ENUM` after we desupport GCC 10.
//
// Since few developers will be using GCC locally, they may forget to list all
// of the enum's enumerator values and only realize they broke the GCC build
// after their patch is backed out of autoland. To catch this error sooner, use
// the GCC macro in DEBUG builds on all platforms.
//
// Usage:
// ```
// enum class MyEnum { Foo, Bar, Baz };
// MOZ_USING_ENUM(MyEnum, Foo, Bar, Baz);
//
// class MyClass {
//   enum class MyEnum { Foo, Bar, Baz };
//   // Use MOZ_USING_ENUM_STATIC in class declarations because the constexpr
//   // member variables need to be static. This isn't needed for `using enum`.
//   MOZ_USING_ENUM_STATIC(MyEnum, Foo, Bar, Baz);
// };
// ```

#if defined(__cpp_using_enum) && !defined(DEBUG)
#  define MOZ_USING_ENUM(ENUM, ...) using enum ENUM
#  define MOZ_USING_ENUM_STATIC(ENUM, ...) using enum ENUM
#else
#  define MOZ_USING_ENUM_DECLARE(ENUM, NAME) constexpr auto NAME = ENUM::NAME;
#  define MOZ_USING_ENUM(ENUM, ...) \
    MOZ_FOR_EACH(MOZ_USING_ENUM_DECLARE, (ENUM, ), (__VA_ARGS__))

#  define MOZ_USING_ENUM_DECLARE_STATIC(ENUM, NAME) \
    static constexpr auto NAME = ENUM::NAME;
#  define MOZ_USING_ENUM_STATIC(ENUM, ...) \
    MOZ_FOR_EACH(MOZ_USING_ENUM_DECLARE_STATIC, (ENUM, ), (__VA_ARGS__))
#endif

#endif  // mozilla_UsingEnum_h
