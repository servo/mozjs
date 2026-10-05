/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_GleanQuantity_h
#define mozilla_glean_GleanQuantity_h

#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/glean/bindings/GleanMetric.h"
#include "mozilla/glean/bindings/QuantityStandalone.h"
#include "nsTString.h"
#include "nsIScriptError.h"

namespace mozilla::glean {

namespace impl {

class QuantityMetric : public QuantityStandalone {
 public:
  constexpr explicit QuantityMetric(uint32_t id) : QuantityStandalone(id) {}

  /**
   * **Test-only API**
   *
   * Gets the currently stored value.
   *
   * This function will attempt to await the last parent-process task (if any)
   * writing to the the metric's storage engine before returning a value.
   * This function will not wait for data from child processes.
   *
   * This doesn't clear the stored value.
   * Parent process only. Panics in child processes.
   *
   * @param aPingName The (optional) name of the ping to retrieve the metric
   *        for. Defaults to the first value in `send_in_pings`.
   *
   * @return value of the stored metric.
   */
  Result<Maybe<int64_t>, nsCString> TestGetValue(
      const nsACString& aPingName = nsCString()) const;
};

}  // namespace impl

class GleanQuantity final : public GleanMetric {
 public:
  explicit GleanQuantity(uint32_t id, nsISupports* aParent)
      : GleanMetric(aParent), mQuantity(id) {}

  virtual JSObject* WrapObject(
      JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override final;

  void Set(int64_t aValue);

  dom::Nullable<int64_t> TestGetValue(const nsACString& aPingName,
                                      ErrorResult& aRv);

 private:
  virtual ~GleanQuantity() = default;

  const impl::QuantityMetric mQuantity;
};

}  // namespace mozilla::glean

#endif /* mozilla_glean_GleanQuantity.h */
