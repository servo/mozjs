/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsISupports.h"
#include "nsISupportsImpl.h"
#include <mozilla/WeakPtr.h>
#include "nsCycleCollectionParticipant.h"
#include "nsWeakReference.h"

// --- Bad: Unlink does not call DetachWeakPtr ---

class BadClass : public nsISupports, // expected-error {{Cycle-collected class 'BadClass' inherits from 'SupportsWeakPtr' but its cycle collection Unlink does not call 'DetachWeakPtr'()}}
                 public mozilla::SupportsWeakPtr {
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(BadClass)

protected:
  ~BadClass() = default;
};

NS_IMPL_CYCLE_COLLECTING_ADDREF(BadClass)
NS_IMPL_CYCLE_COLLECTING_RELEASE(BadClass)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(BadClass)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTION_CLASS(BadClass)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(BadClass) // expected-note {{Unlink defined here; add 'NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR'}}
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(BadClass)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

// --- Good: uses NS_IMPL_CYCLE_COLLECTION_WEAK_PTR ---

class GoodClass : public nsISupports,
                  public mozilla::SupportsWeakPtr {
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(GoodClass)

protected:
  ~GoodClass() = default;
};

NS_IMPL_CYCLE_COLLECTING_ADDREF(GoodClass)
NS_IMPL_CYCLE_COLLECTING_RELEASE(GoodClass)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(GoodClass)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTION_WEAK_PTR(GoodClass)

// --- Good: INHERITED variant where parent handles DetachWeakPtr ---

class ParentClass : public nsISupports,
                    public mozilla::SupportsWeakPtr {
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(ParentClass)

protected:
  ~ParentClass() = default;
};

NS_IMPL_CYCLE_COLLECTING_ADDREF(ParentClass)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ParentClass)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ParentClass)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTION_WEAK_PTR(ParentClass)

// ChildClass does not directly inherit SupportsWeakPtr — no warning.
class ChildClass : public ParentClass {
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ChildClass, ParentClass)

protected:
  ~ChildClass() = default;
};

NS_IMPL_ADDREF_INHERITED(ChildClass, ParentClass)
NS_IMPL_RELEASE_INHERITED(ChildClass, ParentClass)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ChildClass)
NS_INTERFACE_MAP_END_INHERITING(ParentClass)
NS_IMPL_CYCLE_COLLECTION_INHERITED(ChildClass, ParentClass)

// --- OK: SupportsWeakPtr with no cycle collection ---

class NotCycleCollected : public mozilla::SupportsWeakPtr {
};

// --- Bad: nsSupportsWeakReference Unlink does not call ClearWeakReferences ---

class BadWeakRefClass : public nsSupportsWeakReference { // expected-error {{Cycle-collected class 'BadWeakRefClass' inherits from 'nsSupportsWeakReference' but its cycle collection Unlink does not call 'ClearWeakReferences'()}}
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(BadWeakRefClass)

protected:
  ~BadWeakRefClass() = default;
};

NS_IMPL_CYCLE_COLLECTING_ADDREF(BadWeakRefClass)
NS_IMPL_CYCLE_COLLECTING_RELEASE(BadWeakRefClass)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(BadWeakRefClass)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTION_CLASS(BadWeakRefClass)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(BadWeakRefClass) // expected-note {{Unlink defined here; add 'NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE'}}
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(BadWeakRefClass)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

// --- Good: nsSupportsWeakReference with NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE ---

class GoodWeakRefClass : public nsSupportsWeakReference {
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(GoodWeakRefClass)

protected:
  ~GoodWeakRefClass() = default;
};

NS_IMPL_CYCLE_COLLECTING_ADDREF(GoodWeakRefClass)
NS_IMPL_CYCLE_COLLECTING_RELEASE(GoodWeakRefClass)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(GoodWeakRefClass)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTION_CLASS(GoodWeakRefClass)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(GoodWeakRefClass)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(GoodWeakRefClass)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
