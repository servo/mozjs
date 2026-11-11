/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_SymbolType_h
#define vm_SymbolType_h

#include <stdio.h>

#include "gc/Barrier.h"
#include "gc/Tracer.h"
#include "js/AllocPolicy.h"
#include "js/GCHashTable.h"
#include "js/RootingAPI.h"
#include "js/shadow/Symbol.h"  // JS::shadow::Symbol
#include "js/Symbol.h"
#include "js/TypeDecls.h"
#include "vm/StringType.h"

struct JSContext;

namespace js {
class JS_PUBLIC_API GenericPrinter;
class JSONPrinter;
} /* namespace js */

namespace JS {

/*
 * Symbol represents a JS Symbol, or a per-zone wrapper to a unique Symbol.
 *
 * JS symbols come in several kinds:
 *  - well known symbols, e.g. Symbol.iterator
 *  - registered symbols created with e.g. Symbol.for()
 *  - unique symbols, created with Symbol()
 *  - private name symbols
 *
 * For all symbols there is a single runtime-wide Symbol instance that is the
 * canonical representation of the JS Symbol. This is allocated in the atoms
 * zone (or for well known symbols this can be the atoms zone of the parent
 * runtime). The isShared() method returns true for these objects.
 *
 * Unique symbols also have a per-zone wrapper allocated in the referencing
 * zone. This is also a Symbol object and holds a pointer to the shared
 * symbol. The isLocal() method returns true for these objects. Scripts only
 * ever refer to unique symbols via the local wrapper. The only references to
 * the canonical shared symbol for a unique symbol are from local symbols.
 *
 * When symbols are passed between zones they must be wrapped into the new zone
 * to get the correct local symbol wrapper for unique symbols.
 *
 *             Script zone                  |        Atoms zone
 *                                          |
 * +-----------+                            |      +--------------+
 * |           |                            |      |  Non-unique  |
 * | Referrer  +---------------------------------->| SharedSymbol |
 * |           |                            |      |              |
 * +-----------+                            |      +--------------+
 *                                          |
 * +-----------+       +-------------+      |      +--------------+
 * |           |       |   Unique    |      |      |   Unique     |
 * | Referrer  +------>| LocalSymbol +------------>| SharedSymbol |
 * |           |       |             |      |      |              |
 * +-----------+       +-------------+      |      +--------------+
 *
 * This setup exists to allow tracking of gray marking state for unique symbols,
 * which may participate in cycles by being used as weakmap keys.
 */
class Symbol : public js::gc::CellWithTenuredGCPointer<js::gc::TenuredCell,
                                                       js::gc::TenuredCell> {
  friend class js::gc::CellAllocator;

 public:
  Symbol(const Symbol&) = delete;
  void operator=(const Symbol&) = delete;

  // User description of symbol.
  JSAtom* description() const;

 private:
  const SymbolCode code_;

  // Each Symbol gets its own hash code so that we don't have to use
  // addresses as hash codes (a security hazard).
  const js::HashNumber hash_;

  Symbol(SymbolCode code, js::HashNumber hash, Handle<JSAtom*> desc)
      : CellWithTenuredGCPointer(desc ? &desc->asTenured() : nullptr),
        code_(code),
        hash_(hash) {}
  Symbol(SymbolCode code, js::HashNumber hash, Handle<JS::Symbol*> shared)
      : CellWithTenuredGCPointer(shared), code_(code), hash_(hash) {}

  static Symbol* newSharedSymbol(JSContext* cx, SymbolCode code,
                                 js::HashNumber hash,
                                 Handle<JSAtom*> description);
  static Symbol* newLocalSymbol(JSContext* cx, SymbolCode code,
                                js::HashNumber hash,
                                Handle<JS::Symbol*> shared);

  static Symbol* wrap(JSContext* cx, Handle<JS::Symbol*> shared);
  friend struct ::JSContext;

  static void staticAsserts() {
    static_assert(uint32_t(SymbolCode::WellKnownAPILimit) ==
                      JS::shadow::Symbol::WellKnownAPILimit,
                  "JS::shadow::Symbol::WellKnownAPILimit must match "
                  "SymbolCode::WellKnownAPILimit");
    static_assert(
        offsetof(Symbol, code_) == offsetof(JS::shadow::Symbol, code_),
        "JS::shadow::Symbol::code_ offset must match SymbolCode::code_");
  }

 public:
  static Symbol* new_(JSContext* cx, SymbolCode code,
                      js::HandleString description);
  static Symbol* newWellKnown(JSContext* cx, SymbolCode code,
                              Handle<js::PropertyName*> description);
  static Symbol* for_(JSContext* cx, js::HandleString description);

  SymbolCode code() const { return code_; }
  js::HashNumber hash() const { return hash_; }

  // Whether this is a zone-local symbol.
  bool isLocal() const;

  // Whether this is a shared symbol that is allocated in the atoms zone.
  bool isShared() const;

  // For a local symbol, get the corresponding shared symbol.
  Symbol* sharedSymbol() const;

  bool isWellKnownSymbol() const {
    return uint32_t(code_) < WellKnownSymbolLimit;
  }

  // An "interesting symbol" is a well-known symbol, like @@toStringTag,
  // that's often looked up on random objects but is usually not present. We
  // optimize this by setting a flag on the object's BaseShape when such
  // symbol properties are added, so we can optimize lookups on objects that
  // don't have the BaseShape flag.
  bool isInterestingSymbol() const {
    return code_ == SymbolCode::toStringTag ||
           code_ == SymbolCode::toPrimitive ||
           code_ == SymbolCode::isConcatSpreadable;
  }

  // Symbol created for the #PrivateName syntax.
  bool isPrivateName() const { return code_ == SymbolCode::PrivateNameSymbol; }

  bool isUnique() const { return code_ == JS::SymbolCode::UniqueSymbol; }

  static const JS::TraceKind TraceKind = JS::TraceKind::Symbol;

  void traceChildren(JSTracer* trc);

  // Override base class implementation to tell GC about well-known symbols.
  bool isPermanentAndMayBeShared() const { return isWellKnownSymbol(); }

#if defined(DEBUG) || defined(JS_JITSPEW)
  void dump() const;  // Debugger-friendly stderr dump.
  void dump(js::GenericPrinter& out) const;
  void dump(js::JSONPrinter& json) const;

  void dumpFields(js::JSONPrinter& json) const;
  void dumpStringContent(js::GenericPrinter& out) const;
  void dumpPropertyName(js::GenericPrinter& out) const;
#endif

  static constexpr size_t offsetOfHash() { return offsetof(Symbol, hash_); }
  static constexpr size_t offsetOfCode() { return offsetof(Symbol, code_); }
};

} /* namespace JS */

namespace js {

/* Hash policy used by the SymbolRegistry. */
struct HashSymbolsByDescription {
  using Key = JS::Symbol*;
  using Lookup = JSAtom*;

  static HashNumber hash(Lookup l) { return HashNumber(l->hash()); }
  static bool match(Key sym, Lookup l) { return sym->description() == l; }
};

/*
 * [SMDOC] Symbol.for() registry (ES6 GlobalSymbolRegistry)
 *
 * The runtime-wide symbol registry, used to implement Symbol.for().
 *
 * ES6 draft rev 25 (2014 May 22) calls this the GlobalSymbolRegistry List. In
 * our implementation, it is not global. There is one per JSRuntime. The
 * symbols in the symbol registry, like all symbols, are allocated in the atoms
 * compartment and can be directly referenced from any compartment. They are
 * never shared across runtimes.
 *
 * The memory management strategy here is modeled after js::AtomSet. It's like
 * a WeakSet. The registry itself does not keep any symbols alive; when a
 * symbol in the registry is collected, the registry entry is removed. No GC
 * nondeterminism is exposed to scripts, because there is no API for
 * enumerating the symbol registry, querying its size, etc.
 */
class SymbolRegistry
    : public GCHashSet<WeakHeapPtr<JS::Symbol*>, HashSymbolsByDescription,
                       SystemAllocPolicy> {
 public:
  SymbolRegistry() = default;
};

JS::Symbol* UnwrapSymbol(JS::Symbol* symbol);

// ES6 rev 27 (2014 Aug 24) 19.4.3.3
bool SymbolDescriptiveString(JSContext* cx, JS::Symbol* sym,
                             JS::MutableHandleValue result);

} /* namespace js */

#endif /* vm_SymbolType_h */
