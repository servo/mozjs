/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/LinkedList.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ReverseIterator.h"

using mozilla::AutoCleanLinkedList;
using mozilla::LinkedList;
using mozilla::LinkedListElement;

class PtrClass : public LinkedListElement<PtrClass> {
 public:
  bool* mResult;

  explicit PtrClass(bool* result) : mResult(result) { EXPECT_TRUE(!*mResult); }

  virtual ~PtrClass() { *mResult = true; }
};

class InheritedPtrClass : public PtrClass {
 public:
  bool* mInheritedResult;

  InheritedPtrClass(bool* result, bool* inheritedResult)
      : PtrClass(result), mInheritedResult(inheritedResult) {
    EXPECT_TRUE(!*mInheritedResult);
  }

  virtual ~InheritedPtrClass() { *mInheritedResult = true; }
};

TEST(LinkedList, AutoCleanLinkedList)
{
  bool rv1 = false;
  bool rv2 = false;
  bool rv3 = false;
  {
    AutoCleanLinkedList<PtrClass> list;
    list.insertBack(new PtrClass(&rv1));
    list.insertBack(new InheritedPtrClass(&rv2, &rv3));
  }

  EXPECT_TRUE(rv1);
  EXPECT_TRUE(rv2);
  EXPECT_TRUE(rv3);
}

class CountedClass final : public LinkedListElement<RefPtr<CountedClass>> {
 public:
  int mCount;
  void AddRef() { mCount++; }
  void Release() { mCount--; }

  CountedClass() : mCount(0) {}
  ~CountedClass() { EXPECT_TRUE(mCount == 0); }
};

TEST(LinkedList, AutoCleanLinkedListRefPtr)
{
  RefPtr<CountedClass> elt1 = new CountedClass;
  CountedClass* elt2 = new CountedClass;
  {
    AutoCleanLinkedList<RefPtr<CountedClass>> list;
    list.insertBack(elt1);
    list.insertBack(elt2);

    EXPECT_TRUE(elt1->mCount == 2);
    EXPECT_TRUE(elt2->mCount == 1);
  }

  EXPECT_TRUE(elt1->mCount == 1);
  EXPECT_TRUE(elt2->mCount == 0);
}

struct SomeClass : public LinkedListElement<SomeClass> {
  unsigned int mValue;
  explicit SomeClass(int aValue = 0) : mValue(aValue) {}
  SomeClass(SomeClass&&) = default;
  SomeClass& operator=(SomeClass&&) = default;
  void incr() { ++mValue; }
};

TEST(LinkedList, TestReverseIterators)
{
  LinkedList<SomeClass> list;
  SomeClass one(1), two(2), three(3);
  list.insertBack(&one);
  list.insertBack(&two);
  list.insertBack(&three);

  // Traverse in reverse using rbegin/rend.
  unsigned int expect1[]{3, 2, 1};
  size_t idx = 0;
  for (auto it = list.rbegin(); it != list.rend(); ++it, ++idx) {
    EXPECT_EQ((*it)->mValue, expect1[idx]);
  }
  EXPECT_EQ(idx, 3u);

  // Const reverse iteration.
  const LinkedList<SomeClass>& clist = list;
  unsigned int expect2[]{3, 2, 1};
  idx = 0;
  for (auto it = clist.crbegin(); it != clist.crend(); ++it, ++idx) {
    EXPECT_EQ((*it)->mValue, expect2[idx]);
  }
  EXPECT_EQ(idx, 3u);

  // Use Reversed helper.
  unsigned int expect3[]{3, 2, 1};
  idx = 0;
  for (SomeClass* entry : mozilla::Reversed(list)) {
    EXPECT_EQ(entry->mValue, expect3[idx++]);
  }
  EXPECT_EQ(idx, 3u);

  // Mutation safety: remove after advancing.
  auto rit = list.rbegin();
  EXPECT_EQ((*rit)->mValue, 3u);
  SomeClass* toRemove = *rit;
  ++rit;               // advance before removal to avoid invalidating 'rit'
  toRemove->remove();  // remove 3
  EXPECT_EQ((*rit)->mValue, 2u);
  toRemove = *rit;
  ++rit;               // now at 1
  toRemove->remove();  // remove 2
  EXPECT_EQ(list.length(), 1u);
  EXPECT_EQ(list.getFirst()->mValue, 1u);
}
