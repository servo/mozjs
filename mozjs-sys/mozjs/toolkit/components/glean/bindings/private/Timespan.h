/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_glean_GleanTimespan_h
#define mozilla_glean_GleanTimespan_h

#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/glean/bindings/GleanMetric.h"
#include "mozilla/glean/bindings/TimespanStandalone.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "nsTString.h"

namespace mozilla::glean {

namespace impl {

class TimespanMetric : public TimespanStandalone {
 public:
  constexpr explicit TimespanMetric(uint32_t aId) : TimespanStandalone(aId) {}

  /**
   * **Test-only API**
   *
   * Gets the currently stored value as an integer.
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
   * @return value of the stored metric, or Nothing() if there is no value.
   */
  Result<Maybe<uint64_t>, nsCString> TestGetValue(
      const nsACString& aPingName = nsCString()) const;
};
}  // namespace impl

class GleanTimespan final : public GleanMetric {
 public:
  explicit GleanTimespan(uint32_t aId, nsISupports* aParent)
      : GleanMetric(aParent), mTimespan(aId) {}

  virtual JSObject* WrapObject(
      JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override final;

  void Start();
  void Stop();
  void Cancel();
  void SetRaw(uint32_t aDuration);

  dom::Nullable<uint64_t> TestGetValue(const nsACString& aPingName,
                                       ErrorResult& aRv);

 private:
  virtual ~GleanTimespan() = default;

  const impl::TimespanMetric mTimespan;
};

}  // namespace mozilla::glean

#endif /* mozilla_glean_GleanTimespan_h */
