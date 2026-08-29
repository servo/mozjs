/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_Labeled_h
#define mozilla_glean_Labeled_h

#include "nsISupports.h"
#include "nsWrapperCache.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/GleanBinding.h"
#include "mozilla/dom/Record.h"
#include "mozilla/glean/bindings/Boolean.h"
#include "mozilla/glean/bindings/Counter.h"
#include "mozilla/glean/bindings/CustomDistribution.h"
#include "mozilla/glean/bindings/DistributionData.h"
#include "mozilla/glean/bindings/GleanMetric.h"
#include "mozilla/glean/bindings/HistogramGIFFTMap.h"
#include "mozilla/glean/bindings/MemoryDistribution.h"
#include "mozilla/glean/bindings/Quantity.h"
#include "mozilla/glean/bindings/ScalarGIFFTMap.h"
#include "mozilla/glean/bindings/String.h"
#include "mozilla/glean/bindings/TimingDistribution.h"
#include "mozilla/glean/fog_ffi_generated.h"

enum class DynamicLabel : uint16_t;

namespace mozilla::glean {

namespace impl {

// Helper function to get the current process type string for telemetry labeling
nsCString GetProcessTypeForTelemetry();

static inline void UpdateLabeledMirror(Telemetry::ScalarID aMirrorId,
                                       uint32_t aSubmetricId,
                                       const nsACString& aLabel) {
  GetLabeledMirrorLock().apply([&](const auto& lock) {
    auto tuple = std::make_tuple<Telemetry::ScalarID, nsString>(
        std::move(aMirrorId), NS_ConvertUTF8toUTF16(aLabel));
    lock.ref()->InsertOrUpdate(aSubmetricId, std::move(tuple));
  });
}

static inline void UpdateLabeledDistributionMirror(
    Telemetry::HistogramID aMirrorId, uint32_t aSubmetricId,
    const nsACString& aLabel) {
  GetLabeledDistributionMirrorLock().apply([&](const auto& lock) {
    auto tuple = std::make_tuple<Telemetry::HistogramID, nsCString>(
        std::move(aMirrorId), nsPromiseFlatCString(aLabel));
    lock.ref()->InsertOrUpdate(aSubmetricId, std::move(tuple));
  });
}

template <typename T>
struct MetricOutputTypeTraits;

template <>
struct MetricOutputTypeTraits<BooleanMetric> {
  using Output = bool;
};

template <>
struct MetricOutputTypeTraits<QuantityMetric> {
  using Output = int64_t;
};

template <>
struct MetricOutputTypeTraits<CounterMetric<CounterType::eBaseOrLabeled>> {
  using Output = int32_t;
};

template <>
struct MetricOutputTypeTraits<StringMetric> {
  using Output = nsCString;
};

template <>
struct MetricOutputTypeTraits<CustomDistributionMetric> {
  using Output = DistributionData;
};

template <>
struct MetricOutputTypeTraits<MemoryDistributionMetric> {
  using Output = DistributionData;
};

template <>
struct MetricOutputTypeTraits<TimingDistributionMetric> {
  using Output = DistributionData;
};

template <typename T>
inline uint32_t fog_labeled_get(uint32_t aId, const nsACString* aLabel);

template <typename T>
inline uint32_t fog_labeled_enum_get(uint32_t aId, uint16_t aLabel);

template <typename T, typename O>
inline Maybe<nsTHashMap<nsCString, O>> fog_labeled_test_get_value(
    uint32_t aId, const nsACString* aPingName);

template <typename T>
inline Maybe<nsCString> fog_labeled_test_get_error(uint32_t aId);

#define FOG_LABEL_TYPE_MAP(_type, _name)                                       \
  template <>                                                                  \
  inline uint32_t fog_labeled_get<_type>(uint32_t aId,                         \
                                         const nsACString* aLabel) {           \
    return fog_labeled_##_name##_get(aId, aLabel);                             \
  }                                                                            \
  template <>                                                                  \
  inline uint32_t fog_labeled_enum_get<_type>(uint32_t aId, uint16_t aLabel) { \
    return fog_labeled_##_name##_enum_get(aId, aLabel);                        \
  }                                                                            \
  template <>                                                                  \
  inline Maybe<nsCString> fog_labeled_test_get_error<_type>(uint32_t aId) {    \
    nsCString error;                                                           \
    if (fog_labeled_##_name##_test_get_error(aId, &error)) {                   \
      return Some(error);                                                      \
    }                                                                          \
    return Nothing();                                                          \
  }

FOG_LABEL_TYPE_MAP(BooleanMetric, boolean)
FOG_LABEL_TYPE_MAP(QuantityMetric, quantity)
FOG_LABEL_TYPE_MAP(StringMetric, string)
FOG_LABEL_TYPE_MAP(CounterMetric<CounterType::eBaseOrLabeled>, counter)
FOG_LABEL_TYPE_MAP(CustomDistributionMetric, custom_distribution)
FOG_LABEL_TYPE_MAP(MemoryDistributionMetric, memory_distribution)
FOG_LABEL_TYPE_MAP(TimingDistributionMetric, timing_distribution)

#undef FOG_LABEL_TYPE_MAP

template <typename T, typename E>
class Labeled {
 public:
  constexpr explicit Labeled(uint32_t id) : mId(id) {}

  /**
   * Gets a specific metric for a given label.
   *
   * If a set of acceptable labels were specified in the `metrics.yaml` file,
   * and the given label is not in the set, it will be recorded under the
   * special `OTHER_LABEL` label.
   *
   * If a set of acceptable labels was not specified in the `metrics.yaml` file,
   * only the first 16 unique labels will be used.
   * After that, any additional labels will be recorded under the special
   * `OTHER_LABEL` label.
   *
   * @param aLabel - a snake_case string under 30 characters in length,
   *                 otherwise the metric will be recorded under the special
   *                 `OTHER_LABEL` label and an error will be recorded.
   */
  T Get(const nsACString& aLabel) const {
    uint32_t submetricId = fog_labeled_get<T>(mId, &aLabel);

    Maybe<ScalarID> mirrorId;
    if constexpr (MayBeScalarMirror()) {
      mirrorId = ScalarIdForMetric(mId);
      if (mirrorId) {
        UpdateLabeledMirror(mirrorId.extract(), submetricId, aLabel);
      }
    }
    if constexpr (MayBeDistributionMirror()) {
      if (!mirrorId) {
        if (Maybe<HistogramID> mirrorHgramId = HistogramIdForMetric(mId)) {
          UpdateLabeledDistributionMirror(mirrorHgramId.extract(), submetricId,
                                          aLabel);
        }
      }
    }
    return T(submetricId);
  }

  /**
   * Gets a specific metric for a given label, using the label's enum variant.
   *
   * @param aLabel - a variant of this label's label enum.
   */
  template <typename U = T>
  std::enable_if_t<!std::is_same_v<U, DynamicLabel>, T> EnumGet(
      E aLabel) const {
    uint32_t submetricId =
        fog_labeled_enum_get<T>(mId, static_cast<uint16_t>(aLabel));

    Maybe<ScalarID> mirrorId;
    if constexpr (MayBeScalarMirror()) {
      mirrorId = ScalarIdForMetric(mId);
      if (mirrorId) {
        // Telemetry's keyed scalars operate on (16-bit) strings,
        // so we're going to need the string for this enum.
        nsCString label;
        fog_labeled_enum_to_str(mId, static_cast<uint16_t>(aLabel), &label);
        UpdateLabeledMirror(mirrorId.extract(), submetricId, label);
      }
    }
    if constexpr (MayBeDistributionMirror()) {
      if (!mirrorId) {
        if (Maybe<HistogramID> mirrorHgramId = HistogramIdForMetric(mId)) {
          nsCString label;
          fog_labeled_enum_to_str(mId, static_cast<uint16_t>(aLabel), &label);
          UpdateLabeledDistributionMirror(mirrorHgramId.extract(), submetricId,
                                          label);
        }
      }
    }
    return T(submetricId);
  }

  template <typename M = T,
            typename V = typename MetricOutputTypeTraits<T>::Output>
  Result<Maybe<nsTHashMap<nsCString, V>>, nsCString> TestGetValue(
      const nsCString aPingName = nsCString()) const {
    Maybe<nsCString> err;
    err = fog_labeled_test_get_error<T>(mId);
    if (err.isSome()) {
      return Err(err.value());
    }
    uint64_t count = 0;
    nsTArray<nsCString> keys;
    nsTArray<V> values;
    if constexpr (std::is_same_v<M, BooleanMetric>) {
      fog_labeled_boolean_test_get_value(mId, &aPingName, &count, &keys,
                                         &values);
    } else if constexpr (std::is_same_v<
                             M, CounterMetric<CounterType::eBaseOrLabeled>>) {
      fog_labeled_counter_test_get_value(mId, &aPingName, &count, &keys,
                                         &values);
    } else if constexpr (std::is_same_v<M, StringMetric>) {
      fog_labeled_string_test_get_value(mId, &aPingName, &count, &keys,
                                        &values);
    } else if constexpr (std::is_same_v<M, QuantityMetric>) {
      fog_labeled_quantity_test_get_value(mId, &aPingName, &count, &keys,
                                          &values);
    } else if constexpr (std::is_same_v<M, CustomDistributionMetric>) {
      nsTArray<FfiDistributionData> ffi_values;
      fog_labeled_custom_distribution_test_get_value(mId, &aPingName, &count,
                                                     &keys, &ffi_values);
      DistributionData::fromFFIArray(ffi_values, values);
    } else if constexpr (std::is_same_v<M, MemoryDistributionMetric>) {
      nsTArray<FfiDistributionData> ffi_values;
      fog_labeled_memory_distribution_test_get_value(mId, &aPingName, &count,
                                                     &keys, &ffi_values);
      DistributionData::fromFFIArray(ffi_values, values);
    } else if constexpr (std::is_same_v<M, TimingDistributionMetric>) {
      nsTArray<FfiDistributionData> ffi_values;
      fog_labeled_timing_distribution_test_get_value(mId, &aPingName, &count,
                                                     &keys, &ffi_values);
      DistributionData::fromFFIArray(ffi_values, values);
    } else {
      static_assert(std::is_same_v<M, void>,
                    "There should be a block handling this type");
    }

    nsTHashMap<nsCStringHashKey, V> result;
    for (uint64_t i = 0; i < count; i++) {
      result.InsertOrUpdate(keys[i], std::move(values[i]));
    }

    if (result.Count() == 0) {
      return Maybe<nsTHashMap<nsCString, V>>();
    }
    return Some(std::move(result));
  }

  // This is a workaround only needed in a rare case, ensure it's only compiled
  // there.
  template <typename U = T, typename V = E>
  std::enable_if_t<std::is_same_v<U, TimingDistributionMetric> &&
                       std::is_same_v<V, DynamicLabel>,
                   T>
  MaybeTruncateAndGet(const nsACString& aLabel) const {
    // bug 1959765 is for incorporating this behaviour into the SDK.
    if (aLabel.Length() < 112) {
      return Get(aLabel);
    }
    nsAutoCStringN<111> truncated;
    truncated.Append(aLabel.BeginReading(), 108);
    truncated += "...";
    return Get(truncated);
  }

  /**
   * Gets a specific metric for the current process type.
   *
   * Automatically determines the process type and returns the appropriate
   * labeled metric instance. For content processes, distinguishes between
   * regular "tab" processes, "inference" processes, and "extension" processes.
   */
  T ProcessGet() const { return Get(GetProcessTypeForTelemetry()); }

 protected:
  uint32_t mId;

 private:
  static constexpr bool MayBeScalarMirror() {
    return std::is_same_v<T, BooleanMetric> ||
           std::is_same_v<T, CounterMetric<CounterType::eBaseOrLabeled>> ||
           std::is_same_v<T, QuantityMetric>;
  }

  static constexpr bool MayBeDistributionMirror() {
    return std::is_same_v<T, CounterMetric<CounterType::eBaseOrLabeled>> ||
           std::is_same_v<T, CustomDistributionMetric> ||
           std::is_same_v<T, MemoryDistributionMetric> ||
           std::is_same_v<T, TimingDistributionMetric>;
  }
};

}  // namespace impl

using GleanLabeledTestValue =
    dom::OwningBooleanOrUnsignedLongLongOrUTF8StringOrGleanDistributionData;

class GleanLabeled final : public GleanMetric {
 public:
  explicit GleanLabeled(uint32_t aId, uint32_t aTypeId, nsISupports* aParent)
      : GleanMetric(aParent), mId(aId), mTypeId(aTypeId) {}

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override final;

  already_AddRefed<GleanMetric> NamedGetter(const nsAString& aName,
                                            bool& aFound);
  bool NameIsEnumerable(const nsAString& aName);
  void GetSupportedNames(nsTArray<nsString>& aNames);

  void TestGetValue(
      const nsACString& aPingName,
      dom::Nullable<dom::Record<nsCString, GleanLabeledTestValue>>& aResult,
      ErrorResult& aRv);

 private:
  virtual ~GleanLabeled() = default;

  const uint32_t mId;
  const uint32_t mTypeId;
};

}  // namespace mozilla::glean

#endif /* mozilla_glean_Labeled_h */
