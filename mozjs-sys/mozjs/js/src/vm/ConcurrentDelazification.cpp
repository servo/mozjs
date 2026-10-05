/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/ConcurrentDelazification.h"

#include "mozilla/Assertions.h"       // MOZ_ASSERT, MOZ_CRASH
#include "mozilla/RefPtr.h"           // RefPtr
#include "mozilla/ReverseIterator.h"  // mozilla::Reversed
#include "mozilla/ScopeExit.h"        // mozilla::MakeScopeExit

#include <stddef.h>  // size_t
#include <utility>   // std::move, std::pair

#include "ds/LifoAlloc.h"  // LifoAlloc
#include "frontend/BytecodeCompiler.h"  // DelazifyCanonicalScriptedFunction, DelazifyFailureReason
#include "frontend/CompilationStencil.h"  // CompilationStencil, ExtensibleCompilationStencil, BorrowingCompilationStencil, ScriptStencilRef
#include "frontend/FrontendContext.h"     // JS::FrontendContext
#include "frontend/ScopeBindingCache.h"   // StencilScopeBindingCache
#include "frontend/Stencil.h"  // TaggedScriptThingIndex, ScriptStencilExtra
#include "js/AllocPolicy.h"    // ReportOutOfMemory
#include "js/experimental/JSStencil.h"  // RefPtrTraits<JS::Stencil>
#include "vm/JSContext.h"               // JSContext

using namespace js;

// This is an equivalent of std::swap but this one should work with
// ScriptStencilRef constant fields by relying on placement-new trickery to
// "create" a new value instead of updating the value of each field.
template <typename T>
void const_swap(T& a, T& b) {
  alignas(T) unsigned char buffer[sizeof(T)];
  T* temp = new (buffer) T(std::move(a));
  a.~T();
  new (&a) T(std::move(b));
  b.~T();
  new (&b) T(std::move(*temp));
  temp->~T();
}

bool DelazifyStrategy::add(FrontendContext* fc, ScriptStencilRef& ref) {
  using namespace js::frontend;

  // Only functions with bytecode are allowed to be added.
  MOZ_ASSERT(!ref.scriptDataFromEnclosing().isGhost());
  MOZ_ASSERT(ref.context()->scriptData[0].hasSharedData());

  // Lookup the gc-things range which are referenced by this script.
  auto gcThingData = ref.gcThingsFromInitial();

  // Iterate over gc-things of the script and queue inner functions.
  for (TaggedScriptThingIndex index : mozilla::Reversed(gcThingData)) {
    if (!index.isFunction()) {
      continue;
    }

    ScriptIndex innerIndex = index.toFunction();
    ScriptStencilRef innerRef{ref.stencils_, innerIndex};
    MOZ_ASSERT(innerRef.enclosingScript().scriptIndex_ == ref.scriptIndex_);

    const ScriptStencil& innerScriptData = innerRef.scriptDataFromEnclosing();
    if (innerScriptData.isGhost() ||
        !innerScriptData.functionFlags.isInterpreted() ||
        !innerScriptData.wasEmittedByEnclosingScript()) {
      continue;
    }
    if (innerScriptData.hasSharedData()) {
      // The function has been parsed as part of its enclosing script, thus we
      // should visit its inner function the same way.
      if (!add(fc, innerRef)) {
        return false;
      }
      continue;
    }

    // Maybe insert the new script index in the queue of functions to delazify.
    if (!insert(innerRef)) {
      ReportOutOfMemory(fc);
      return false;
    }
  }

  return true;
}

frontend::ScriptStencilRef LargeFirstDelazification::next() {
  const_swap(heap.back(), heap[0]);
  ScriptStencilRef result = heap.popCopy().second;

  // NOTE: These are a heap indexes offseted by 1, such that we can manipulate
  // the tree of heap-sorted values which bubble up the largest values towards
  // the root of the tree.
  size_t len = heap.length();
  size_t i = 1;
  while (true) {
    // NOTE: We write (n + 1) - 1, instead of n, to explicit that the
    // manipualted indexes are all offseted by 1.
    size_t n = 2 * i;
    size_t largest;
    if (n + 1 <= len && heap[(n + 1) - 1].first > heap[n - 1].first) {
      largest = n + 1;
    } else if (n <= len) {
      // The condition is n <= len in case n + 1 is out of the heap vector, but
      // not n, in which case we still want to check if the last element of the
      // heap vector should be swapped. Otherwise heap[n - 1] represents a
      // larger function than heap[(n + 1) - 1].
      largest = n;
    } else {
      // n is out-side the heap vector, thus our element is already in a leaf
      // position and would not be moved any more.
      break;
    }

    if (heap[i - 1].first < heap[largest - 1].first) {
      // We found a function which has a larger body as a child of the current
      // element. we swap it with the current element, such that the largest
      // element is closer to the root of the tree.
      const_swap(heap[i - 1], heap[largest - 1]);
      i = largest;
    } else {
      // The largest function found as a child of the current node is smaller
      // than the current node's function size. The heap tree is now organized
      // as expected.
      break;
    }
  }

  return result;
}

bool LargeFirstDelazification::insert(frontend::ScriptStencilRef& ref) {
  const frontend::ScriptStencilExtra& extra = ref.scriptExtra();
  SourceSize size = extra.extent.sourceEnd - extra.extent.sourceStart;
  if (!heap.append(std::pair(size, ref))) {
    return false;
  }

  // NOTE: These are a heap indexes offseted by 1, such that we can manipulate
  // the tree of heap-sorted values which bubble up the largest values towards
  // the root of the tree.
  size_t i = heap.length();
  while (i > 1) {
    if (heap[i - 1].first <= heap[(i / 2) - 1].first) {
      return true;
    }

    const_swap(heap[i - 1], heap[(i / 2) - 1]);
    i /= 2;
  }

  return true;
}

bool DelazificationContext::init(
    const JS::ReadOnlyCompileOptions& options,
    frontend::InitialStencilAndDelazifications* stencils) {
  using namespace js::frontend;

  stencils_ = stencils;

  if (!fc_.allocateOwnedPool()) {
    return false;
  }

  // Initialize the relative indexes which are necessary for walking
  // delazification stencils from the CompilationInput.
  auto indexesGuard = stencils->ensureRelativeIndexes(&fc_);
  if (!indexesGuard) {
    return false;
  }
  indexesGuard_.emplace(std::move(indexesGuard));

  switch (options.eagerDelazificationStrategy()) {
    case JS::DelazificationOption::OnDemandOnly:
      // OnDemandOnly will parse function as they are require to continue the
      // execution on the main thread.
      MOZ_CRASH("OnDemandOnly should not create a DelazificationContext.");
      break;
    case JS::DelazificationOption::CheckConcurrentWithOnDemand:
    case JS::DelazificationOption::ConcurrentDepthFirst:
      // ConcurrentDepthFirst visit all functions to be delazified, visiting the
      // inner functions before the siblings functions.
      strategy_ = fc_.getAllocator()->make_unique<DepthFirstDelazification>();
      break;
    case JS::DelazificationOption::ConcurrentLargeFirst:
      // ConcurrentLargeFirst visit all functions to be delazified, visiting the
      // largest function first.
      strategy_ = fc_.getAllocator()->make_unique<LargeFirstDelazification>();
      break;
    case JS::DelazificationOption::ParseEverythingEagerly:
      // ParseEverythingEagerly parse all functions eagerly, thus leaving no
      // functions to be parsed on demand.
      MOZ_CRASH(
          "ParseEverythingEagerly should not create a DelazificationContext");
      break;
  }

  if (!strategy_) {
    return false;
  }

  // Queue functions from the top-level to be delazify.
  ScriptStencilRef topLevel{*stencils_, ScriptIndex{0}};
  return strategy_->add(&fc_, topLevel);
}

bool DelazificationContext::delazify() {
  fc_.setStackQuota(stackQuota_);
  auto purgePool =
      mozilla::MakeScopeExit([&] { fc_.nameCollectionPool().purge(); });

  using namespace js::frontend;

  // Create a scope-binding cache dedicated to this delazification. The memory
  // would be reclaimed when interrupted or if all delazification are completed.
  //
  // We do not use the one from the JSContext/Runtime, as it is not thread safe
  // to use it, as it could be purged by a GC in the mean time.
  StencilScopeBindingCache scopeCache(*stencils_);

  LifoAlloc tempLifoAlloc(JSContext::TEMP_LIFO_ALLOC_PRIMARY_CHUNK_SIZE,
                          js::BackgroundMallocArena);

  while (!strategy_->done()) {
    if (isInterrupted_) {
      isInterrupted_ = false;
      break;
    }
    const CompilationStencil* innerStencil = nullptr;
    ScriptStencilRef scriptRef = strategy_->next();
    {
      // Parse and generate bytecode for the inner function and save it on the
      // InitialStencilAndDelazifications object. If the function had already
      // been parsed, then just get the result back from the stencil.
      DelazifyFailureReason failureReason;
      innerStencil = DelazifyCanonicalScriptedFunction(
          &fc_, tempLifoAlloc, initialPrefableOptions_, &scopeCache,
          scriptRef.scriptIndex_, stencils_.get(), &failureReason);
      if (!innerStencil) {
        if (failureReason == DelazifyFailureReason::Compressed) {
          // The script source is already compressed, and delazification cannot
          // be performed without decompressing.
          // There is no reason to keep our eager delazification going.
          strategy_->clear();
          return true;
        }

        strategy_->clear();
        return false;
      }
    }

    if (!strategy_->add(&fc_, scriptRef)) {
      strategy_->clear();
      return false;
    }
  }

  return true;
}

bool DelazificationContext::done() const {
  if (!strategy_) {
    return true;
  }

  return strategy_->done();
}

size_t DelazificationContext::sizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  size_t size = stencils_->sizeOfIncludingThis(mallocSizeOf);
  return size;
}
