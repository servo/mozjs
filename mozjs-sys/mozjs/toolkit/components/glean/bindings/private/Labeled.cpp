/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/glean/bindings/Labeled.h"

#include "mozilla/dom/GleanBinding.h"
#include "mozilla/dom/GleanMetricsBinding.h"
#include "mozilla/dom/Record.h"
#include "mozilla/glean/fog_ffi_generated.h"
#include "mozilla/glean/bindings/GleanJSMetricsLookup.h"
#include "mozilla/glean/bindings/MetricTypes.h"
#include "mozilla/glean/bindings/ScalarGIFFTMap.h"
#include "nsString.h"
#include "nsXULAppAPI.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/RemoteType.h"

namespace mozilla::glean {

JSObject* GleanLabeled::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return dom::GleanLabeled_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<GleanMetric> GleanLabeled::NamedGetter(const nsAString& aName,
                                                        bool& aFound) {
  auto label = NS_ConvertUTF16toUTF8(aName);
  // All strings will map to a label. Either a valid one or `__other__`.
  aFound = true;
  uint32_t submetricId = 0;
  already_AddRefed<GleanMetric> submetric =
      NewSubMetricFromIds(mTypeId, mId, label, &submetricId, mParent);

  auto mirrorId = ScalarIdForMetric(mId);
  if (mirrorId) {
    GetLabeledMirrorLock().apply([&](const auto& lock) {
      auto tuple = std::make_tuple<Telemetry::ScalarID, nsString>(
          mirrorId.extract(), nsString(aName));
      lock.ref()->InsertOrUpdate(submetricId, std::move(tuple));
    });
  } else if (auto mirrorHgramId = HistogramIdForMetric(mId)) {
    GetLabeledDistributionMirrorLock().apply([&](const auto& lock) {
      auto tuple = std::make_tuple<Telemetry::HistogramID, nsCString>(
          mirrorHgramId.extract(), nsCString(label));
      lock.ref()->InsertOrUpdate(submetricId, std::move(tuple));
    });
  }
  return submetric;
}

bool GleanLabeled::NameIsEnumerable(const nsAString& aName) { return false; }

void GleanLabeled::GetSupportedNames(nsTArray<nsString>& aNames) {
  // We really don't know, so don't do anything.
}

using GleanLabeledTestValue =
    dom::OwningBooleanOrUnsignedLongLongOrUTF8StringOrGleanDistributionData;

void GleanLabeled::TestGetValue(
    const nsACString& aPingName,
    dom::Nullable<dom::Record<nsCString, GleanLabeledTestValue>>& aResult,
    ErrorResult& aRv) {
  auto type = static_cast<MetricTypeId>(mTypeId);
  Maybe<nsCString> err;

  switch (type) {
    case MetricTypeId::LABELED_BOOLEAN:
      err = impl::fog_labeled_test_get_error<impl::BooleanMetric>(mId);
      break;
    case MetricTypeId::LABELED_COUNTER:
      err = impl::fog_labeled_test_get_error<
          impl::CounterMetric<impl::CounterType::eBaseOrLabeled>>(mId);
      break;
    case MetricTypeId::LABELED_STRING:
      err = impl::fog_labeled_test_get_error<impl::StringMetric>(mId);
      break;
    case MetricTypeId::LABELED_QUANTITY:
      err = impl::fog_labeled_test_get_error<impl::QuantityMetric>(mId);
      break;
    case MetricTypeId::LABELED_CUSTOM_DISTRIBUTION:
      err =
          impl::fog_labeled_test_get_error<impl::CustomDistributionMetric>(mId);
      break;
    case MetricTypeId::LABELED_MEMORY_DISTRIBUTION:
      err =
          impl::fog_labeled_test_get_error<impl::MemoryDistributionMetric>(mId);
      break;
    case MetricTypeId::LABELED_TIMING_DISTRIBUTION:
      err =
          impl::fog_labeled_test_get_error<impl::TimingDistributionMetric>(mId);
      break;
    default:
      err = Some(nsCString("type ID supplied is not a Labeled metric type"));
  }
  if (err.isSome()) {
    aResult.SetNull();
    aRv.ThrowDataError(err.value());
    return;
  }

  dom::Record<nsCString, GleanLabeledTestValue> retVal;
  uint64_t count = 0;
  nsTArray<nsCString> keys;
  if (type == MetricTypeId::LABELED_BOOLEAN) {
    nsTArray<bool> values;
    impl::fog_labeled_boolean_test_get_value(mId, &aPingName, &count, &keys,
                                             &values);
    for (uint64_t i = 0; i < count; i++) {
      auto el = retVal.Entries().AppendElement();
      el->mKey = keys[i];
      el->mValue.SetAsBoolean() = values[i];
    }
  } else if (type == MetricTypeId::LABELED_COUNTER) {
    nsTArray<int32_t> values;
    impl::fog_labeled_counter_test_get_value(mId, &aPingName, &count, &keys,
                                             &values);
    for (uint64_t i = 0; i < count; i++) {
      auto el = retVal.Entries().AppendElement();
      el->mKey = keys[i];
      el->mValue.SetAsUnsignedLongLong() = values[i];
    }
  } else if (type == MetricTypeId::LABELED_STRING) {
    nsTArray<nsCString> values;
    impl::fog_labeled_string_test_get_value(mId, &aPingName, &count, &keys,
                                            &values);
    for (uint64_t i = 0; i < count; i++) {
      auto el = retVal.Entries().AppendElement();
      el->mKey = keys[i];
      el->mValue.SetAsUTF8String() = values[i];
    }
  } else if (type == MetricTypeId::LABELED_QUANTITY) {
    nsTArray<int64_t> values;
    impl::fog_labeled_quantity_test_get_value(mId, &aPingName, &count, &keys,
                                              &values);
    for (uint64_t i = 0; i < count; i++) {
      auto el = retVal.Entries().AppendElement();
      el->mKey = keys[i];
      el->mValue.SetAsUnsignedLongLong() = values[i];
    }
  } else if (type == MetricTypeId::LABELED_CUSTOM_DISTRIBUTION ||
             type == MetricTypeId::LABELED_MEMORY_DISTRIBUTION ||
             type == MetricTypeId::LABELED_TIMING_DISTRIBUTION) {
    nsTArray<impl::FfiDistributionData> values;
    if (type == MetricTypeId::LABELED_CUSTOM_DISTRIBUTION) {
      impl::fog_labeled_custom_distribution_test_get_value(
          mId, &aPingName, &count, &keys, &values);
    } else if (type == MetricTypeId::LABELED_MEMORY_DISTRIBUTION) {
      impl::fog_labeled_memory_distribution_test_get_value(
          mId, &aPingName, &count, &keys, &values);
    } else if (type == MetricTypeId::LABELED_TIMING_DISTRIBUTION) {
      impl::fog_labeled_timing_distribution_test_get_value(
          mId, &aPingName, &count, &keys, &values);
    }
    for (size_t i = 0; i < keys.Length(); i++) {
      auto el = retVal.Entries().AppendElement();
      el->mKey = keys[i];
      auto& distributionData = el->mValue.SetAsGleanDistributionData();
      distributionData.mCount = values[i].count;
      distributionData.mSum = values[i].sum;

      for (size_t j = 0; j < values[i].keys.Length(); j++) {
        auto elem = distributionData.mValues.Entries().AppendElement();
        elem->mKey.AppendInt(values[i].keys[j]);
        elem->mValue = values[i].values[j];
      }
    }
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "The function should have returned instead of hitting this.");
  }

  aResult.SetValue(std::move(retVal));
}

namespace impl {

// Helper function to get the current process type string for telemetry labeling
nsCString GetProcessTypeForTelemetry() {
  nsCString processType(XRE_GetProcessTypeString());

  // For content processes, check the specific remote type
  if (processType.EqualsLiteral("tab")) {
    auto* cc = mozilla::dom::ContentChild::GetSingleton();
    if (cc) {
      const nsACString& remoteType = cc->GetRemoteType();
      if (remoteType == EXTENSION_REMOTE_TYPE) {
        processType.AssignLiteral("extension");
      } else if (remoteType == INFERENCE_REMOTE_TYPE) {
        processType.AssignLiteral("inference");
      }
      // Otherwise keep "tab" for regular content processes
    }
  }

  return processType;
}

}  // namespace impl

}  // namespace mozilla::glean
