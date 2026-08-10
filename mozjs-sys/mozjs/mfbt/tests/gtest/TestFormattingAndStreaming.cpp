/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "fmt/format.h"

#include "mozilla/OwningNonNull.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ToString.h"
#include "nsCOMPtr.h"
#include "nsISupports.h"

namespace mozilla {

#define NS_ONLYFORMATTABLE_IID \
  {0xc5709378, 0x9a1d, 0x4b30, {0xb0, 0xae, 0x37, 0x96, 0x8d, 0x49, 0x20, 0xdb}}

// Currently, nsCOMPtr, RefPtr, mozilla::OwningNonNull, mozilla::StaticAutoPtr
// and mozilla::StaticRefPtr uses mozilla::DebugValue(std::ostream& aOut, T*
// aValue). Then, mozilla::DebugValue prefers the formatter of fmt. If it's not
// formattable, fallback to the stream.

// Thus, this class is printed with the format_as.
struct OnlyFormattable : public nsISupports {
  NS_INLINE_DECL_STATIC_IID(NS_ONLYFORMATTABLE_IID)
  NS_DECL_ISUPPORTS

  explicit OnlyFormattable(const std::string& aValue) : mValue(aValue) {}

  std::string mValue;

 private:
  virtual ~OnlyFormattable() = default;
};

NS_IMPL_ISUPPORTS(OnlyFormattable, OnlyFormattable)

inline auto format_as(const OnlyFormattable& aObj) {
  return "formatted value: " + aObj.mValue;
}

#define NS_ONLYSTREAMABLE_IID \
  {0xedb2a297, 0x0dfc, 0x4cf6, {0xa3, 0xbb, 0x3c, 0x80, 0xf7, 0xff, 0x0d, 0x5c}}

// This class is not formattable. Therefore, `operator<<` is used instead.
struct OnlyStreamable : public nsISupports {
  NS_INLINE_DECL_STATIC_IID(NS_ONLYSTREAMABLE_IID)
  NS_DECL_ISUPPORTS

  explicit OnlyStreamable(const std::string& aValue) : mValue(aValue) {}

  std::string mValue;

  friend inline std::ostream& operator<<(std::ostream& aStream,
                                         const OnlyStreamable& aObj) {
    return aStream << "streamed value: " << aObj.mValue;
  }

 private:
  virtual ~OnlyStreamable() = default;
};

NS_IMPL_ISUPPORTS(OnlyStreamable, OnlyStreamable)

#define NS_FORMATTABLEANDSTREAMABLE_IID \
  {0xf477bebc, 0xbbbc, 0x4cbd, {0x85, 0x77, 0xc3, 0x92, 0xa1, 0xec, 0x4c, 0x55}}

// Finally, this class is formattable and supports stream. Because of
// mozilla::DebugValue, the format_as is used both for fmt and stream if printed
// via a pointer wrapper class.
struct FormattableAndStreamable : public nsISupports {
  NS_INLINE_DECL_STATIC_IID(NS_FORMATTABLEANDSTREAMABLE_IID)
  NS_DECL_ISUPPORTS

  explicit FormattableAndStreamable(const std::string& aValue)
      : mValue(aValue) {}

  std::string mValue;

  friend inline std::ostream& operator<<(std::ostream& aStream,
                                         const FormattableAndStreamable& aObj) {
    return aStream << "formatted or streamed value: " << aObj.mValue;
  }

 private:
  virtual ~FormattableAndStreamable() = default;
};

NS_IMPL_ISUPPORTS(FormattableAndStreamable, FormattableAndStreamable)

inline auto format_as(const FormattableAndStreamable& aObj) {
  return "formatted value: " + aObj.mValue;
}

TEST(FormattingAndStreaming, nsCOMPtr)
{
  {
    nsCOMPtr<OnlyFormattable> onlyFormattable =
        MakeRefPtr<OnlyFormattable>("Foo");
    EXPECT_EQ(fmt::format("{}", onlyFormattable),
              fmt::format("{} @ {}", *onlyFormattable,
                          ToString(onlyFormattable.get())))
        << ": formatted only formattable object";
    EXPECT_EQ(fmt::format("{}", ToString(onlyFormattable)),
              fmt::format("{} @ {}", *onlyFormattable,
                          ToString(onlyFormattable.get())))
        << ": streamed only formattable object";
  }
  {
    nsCOMPtr<OnlyStreamable> onlyStreamable = MakeRefPtr<OnlyStreamable>("Bar");
    EXPECT_EQ(fmt::format("{}", onlyStreamable),
              fmt::format("{} @ {}", ToString(*onlyStreamable),
                          ToString(onlyStreamable.get())))
        << ": formatted only streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(onlyStreamable)),
              fmt::format("{} @ {}", ToString(*onlyStreamable),
                          ToString(onlyStreamable.get())))
        << ": streamed only streamable object";
  }
  {
    nsCOMPtr<FormattableAndStreamable> formattableAndStreamable =
        MakeRefPtr<FormattableAndStreamable>("Baz");
    EXPECT_EQ(fmt::format("{}", formattableAndStreamable),
              fmt::format("{} @ {}", *formattableAndStreamable,
                          ToString(formattableAndStreamable.get())))
        << ": formatted formattable and streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(formattableAndStreamable)),
              fmt::format("{} @ {}", *formattableAndStreamable,
                          ToString(formattableAndStreamable.get())))
        << ": streamed formattable and streamable object";
  }
}

TEST(FormattingAndStreaming, RefPtr)
{
  {
    RefPtr<OnlyFormattable> onlyFormattable =
        MakeRefPtr<OnlyFormattable>("Foo");
    EXPECT_EQ(fmt::format("{}", onlyFormattable),
              fmt::format("{} @ {}", *onlyFormattable,
                          ToString(onlyFormattable.get())))
        << ": formatted only formattable object";
    EXPECT_EQ(fmt::format("{}", ToString(onlyFormattable)),
              fmt::format("{} @ {}", *onlyFormattable,
                          ToString(onlyFormattable.get())))
        << ": streamed only formattable object";
  }
  {
    RefPtr<OnlyStreamable> onlyStreamable = MakeRefPtr<OnlyStreamable>("Bar");
    EXPECT_EQ(fmt::format("{}", onlyStreamable),
              fmt::format("{} @ {}", ToString(*onlyStreamable),
                          ToString(onlyStreamable.get())))
        << ": formatted only streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(onlyStreamable)),
              fmt::format("{} @ {}", ToString(*onlyStreamable),
                          ToString(onlyStreamable.get())))
        << ": streamed only streamable object";
  }
  {
    RefPtr<FormattableAndStreamable> formattableAndStreamable =
        MakeRefPtr<FormattableAndStreamable>("Baz");
    EXPECT_EQ(fmt::format("{}", formattableAndStreamable),
              fmt::format("{} @ {}", *formattableAndStreamable,
                          ToString(formattableAndStreamable.get())))
        << ": formatted formattable and streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(formattableAndStreamable)),
              fmt::format("{} @ {}", *formattableAndStreamable,
                          ToString(formattableAndStreamable.get())))
        << ": streamed formattable and streamable object";
  }
}

TEST(FormattingAndStreaming, mozilla_OwningNonNull)
{
  {
    OwningNonNull<OnlyFormattable> onlyFormattable =
        MakeRefPtr<OnlyFormattable>("Foo");
    EXPECT_EQ(fmt::format("{}", onlyFormattable),
              fmt::format("{} @ {}", *onlyFormattable,
                          ToString(onlyFormattable.get())))
        << ": formatted only formattable object";
    EXPECT_EQ(fmt::format("{}", ToString(onlyFormattable)),
              fmt::format("{} @ {}", *onlyFormattable,
                          ToString(onlyFormattable.get())))
        << ": streamed only formattable object";
  }
  {
    OwningNonNull<OnlyStreamable> onlyStreamable =
        MakeRefPtr<OnlyStreamable>("Bar");
    EXPECT_EQ(fmt::format("{}", onlyStreamable),
              fmt::format("{} @ {}", ToString(*onlyStreamable),
                          ToString(onlyStreamable.get())))
        << ": formatted only streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(onlyStreamable)),
              fmt::format("{} @ {}", ToString(*onlyStreamable),
                          ToString(onlyStreamable.get())))
        << ": streamed only streamable object";
  }
  {
    OwningNonNull<FormattableAndStreamable> formattableAndStreamable =
        MakeRefPtr<FormattableAndStreamable>("Baz");
    EXPECT_EQ(fmt::format("{}", formattableAndStreamable),
              fmt::format("{} @ {}", *formattableAndStreamable,
                          ToString(formattableAndStreamable.get())))
        << ": formatted formattable and streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(formattableAndStreamable)),
              fmt::format("{} @ {}", *formattableAndStreamable,
                          ToString(formattableAndStreamable.get())))
        << ": streamed formattable and streamable object";
  }
}

template <typename T>
struct RefPtrWrapper {
  RefPtr<T> mRefPtr;

  friend inline std::ostream& operator<<(std::ostream& aStream,
                                         const RefPtrWrapper& aObj) {
    if constexpr (detail::supports_os<T>::value) {
      return aStream << ToString(*aObj.mRefPtr);
    } else {
      return aStream << ToString(aObj.mRefPtr.get());
    }
  }
};

template <typename T>
inline auto format_as(const RefPtrWrapper<T>& aObj) {
  if constexpr (fmt::is_formattable<T>::value) {
    return fmt::format("{}", *aObj.mRefPtr);
  } else {
    return ToString(*aObj.mRefPtr);
  }
}

StaticAutoPtr<RefPtrWrapper<OnlyFormattable>> sAutoOnlyFormattable;
StaticAutoPtr<RefPtrWrapper<OnlyStreamable>> sAutoOnlyStreamable;
StaticAutoPtr<RefPtrWrapper<FormattableAndStreamable>>
    sAutoFormattableAndStreamable;

TEST(FormattingAndStreaming, mozilla_StaticAutoPtr)
{
  {
    sAutoOnlyFormattable =
        new RefPtrWrapper{MakeRefPtr<OnlyFormattable>("Foo")};
    EXPECT_EQ(fmt::format("{}", sAutoOnlyFormattable),
              fmt::format("{} @ {}", *sAutoOnlyFormattable->mRefPtr,
                          ToString(sAutoOnlyFormattable.get())))
        << ": formatted only formattable object";
    EXPECT_EQ(fmt::format("{}", ToString(sAutoOnlyFormattable)),
              fmt::format("{} @ {}", *sAutoOnlyFormattable->mRefPtr,
                          ToString(sAutoOnlyFormattable.get())))
        << ": streamed only formattable object";
    sAutoOnlyFormattable = nullptr;
  }
  {
    sAutoOnlyStreamable = new RefPtrWrapper{MakeRefPtr<OnlyStreamable>("Bar")};
    EXPECT_EQ(fmt::format("{}", sAutoOnlyStreamable),
              fmt::format("{} @ {}", ToString(*sAutoOnlyStreamable),
                          ToString(sAutoOnlyStreamable.get())))
        << ": formatted only streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(sAutoOnlyStreamable)),
              fmt::format("{} @ {}", ToString(*sAutoOnlyStreamable),
                          ToString(sAutoOnlyStreamable.get())))
        << ": streamed only streamable object";
    sAutoOnlyStreamable = nullptr;
  }
  {
    sAutoFormattableAndStreamable =
        new RefPtrWrapper{MakeRefPtr<FormattableAndStreamable>("Baz")};
    EXPECT_EQ(fmt::format("{}", sAutoFormattableAndStreamable),
              fmt::format("{} @ {}", *sAutoFormattableAndStreamable->mRefPtr,
                          ToString(sAutoFormattableAndStreamable.get())))
        << ": formatted formattable and streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(sAutoFormattableAndStreamable)),
              fmt::format("{} @ {}", *sAutoFormattableAndStreamable->mRefPtr,
                          ToString(sAutoFormattableAndStreamable.get())))
        << ": streamed formattable and streamable object";
    sAutoFormattableAndStreamable = nullptr;
  }
}

StaticRefPtr<OnlyFormattable> sOnlyFormattable;
StaticRefPtr<OnlyStreamable> sOnlyStreamable;
StaticRefPtr<FormattableAndStreamable> sFormattableAndStreamable;

TEST(FormattingAndStreaming, mozilla_StaticRefPtr)
{
  {
    sOnlyFormattable = MakeRefPtr<OnlyFormattable>("Foo");
    EXPECT_EQ(fmt::format("{}", sOnlyFormattable),
              fmt::format("{} @ {}", *sOnlyFormattable,
                          ToString(sOnlyFormattable.get())))
        << ": formatted only formattable object";
    EXPECT_EQ(fmt::format("{}", ToString(sOnlyFormattable)),
              fmt::format("{} @ {}", *sOnlyFormattable,
                          ToString(sOnlyFormattable.get())))
        << ": streamed only formattable object";
    sOnlyFormattable = nullptr;
  }
  {
    sOnlyStreamable = MakeRefPtr<OnlyStreamable>("Bar");
    EXPECT_EQ(fmt::format("{}", sOnlyStreamable),
              fmt::format("{} @ {}", ToString(*sOnlyStreamable),
                          ToString(sOnlyStreamable.get())))
        << ": formatted only streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(sOnlyStreamable)),
              fmt::format("{} @ {}", ToString(*sOnlyStreamable),
                          ToString(sOnlyStreamable.get())))
        << ": streamed only streamable object";
    sOnlyStreamable = nullptr;
  }
  {
    sFormattableAndStreamable = MakeRefPtr<FormattableAndStreamable>("Baz");
    EXPECT_EQ(fmt::format("{}", sFormattableAndStreamable),
              fmt::format("{} @ {}", *sFormattableAndStreamable,
                          ToString(sFormattableAndStreamable.get())))
        << ": formatted formattable and streamable object";
    EXPECT_EQ(fmt::format("{}", ToString(sFormattableAndStreamable)),
              fmt::format("{} @ {}", *sFormattableAndStreamable,
                          ToString(sFormattableAndStreamable.get())))
        << ": streamed formattable and streamable object";
    sFormattableAndStreamable = nullptr;
  }
}

}  // namespace mozilla
