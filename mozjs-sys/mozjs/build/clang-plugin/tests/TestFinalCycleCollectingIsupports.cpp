/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Fake the path so the checker's directory allowlist matches.
#line 6 "/dom/html/TestFinalCycleCollectingIsupports.cpp"

// Minimal stubs matching the real macro structure in nsISupportsImpl.h.
// NS_DECL_CYCLE_COLLECTING_ISUPPORTS_META is the inner macro that emits
// `override` vs `final`; the checker inspects its name on the expansion stack.
#define NS_DECL_CYCLE_COLLECTING_ISUPPORTS_META(...)                    \
  virtual void AddRef() __VA_ARGS__;                                    \
  virtual void Release() __VA_ARGS__;

#define NS_DECL_CYCLE_COLLECTING_ISUPPORTS \
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_META(override)

#define NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL \
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_META(final)

struct Base {
  virtual void AddRef() = 0;
  virtual void Release() = 0;
};

// Should error: final class using the non-final macro.
struct BadFinal final : Base { // expected-error {{final class 'BadFinal' uses NS_DECL_CYCLE_COLLECTING_ISUPPORTS; use NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL instead}}
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS // expected-note {{replace NS_DECL_CYCLE_COLLECTING_ISUPPORTS with NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL}}
};

// Should not error: final class already using the final macro.
struct GoodFinal final : Base {
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
};

// Should not error: non-final class using the non-final macro.
struct GoodNonFinal : Base {
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
};

// Should not error: final class with manually-written override (not via macro).
struct ManualOverride final : Base {
  virtual void AddRef() override;
  virtual void Release() override;
};
