/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/UniquePtrExtensions.h"
#ifdef XP_UNIX
#  include "fcntl.h"
#  include "unistd.h"
#endif
#ifdef XP_WIN
#  include "namedpipeapi.h"
#endif

using namespace mozilla;

static UniqueFileHandle CreateArbitraryFileHandle() {
#ifdef XP_UNIX
  return UniqueFileHandle(dup(0));
#endif
#ifdef XP_WIN
  UniqueFileHandle hnd0, hnd1;
  if (::CreatePipe(getter_Transfers(hnd0), getter_Transfers(hnd1),
                   /* lpPipeAttributes = */ nullptr,
                   /* nSize = */ 0)) {
    return hnd0;
  }
  return nullptr;
#endif
}

// Test duplicating a null UniqueFileHandle
TEST(MFBT_UniquePtrExtensions, UFH_DupNull)
{
  UniqueFileHandle fd0;
  ASSERT_FALSE(fd0);
  UniqueFileHandle fd1 = DuplicateFileHandle(fd0);
  EXPECT_FALSE(fd1);
}

// Test that DuplicateFileHandle returns a valid fd/handle which isn't
// the same as the input.
TEST(MFBT_UniquePtrExtensions, UFH_DupBasic)
{
  UniqueFileHandle fd0 = CreateArbitraryFileHandle();
  ASSERT_TRUE(fd0);
  UniqueFileHandle fd1 = DuplicateFileHandle(fd0);
  EXPECT_TRUE(fd1);
  EXPECT_NE(fd0.get(), fd1.get());
}

#ifdef XP_UNIX

// Test that SetCloseOnExec works
TEST(MFBT_UniquePtrExtensions, UFH_SetCloExec)
{
  UniqueFileHandle fd0(dup(0));
  ASSERT_TRUE(fd0);
  int rv0 = fcntl(fd0.get(), F_GETFD);
  ASSERT_GE(rv0, 0);
  EXPECT_FALSE(rv0 & FD_CLOEXEC);

  SetCloseOnExec(fd0);
  ASSERT_TRUE(fd0);
  int rv1 = fcntl(fd0.get(), F_GETFD);
  ASSERT_GE(rv1, 0);
  EXPECT_TRUE(rv1 & FD_CLOEXEC);
}

// Test that DuplicateFileHandle sets close-on-exec
TEST(MFBT_UniquePtrExtensions, UFH_DupCloExec)
{
  UniqueFileHandle fd0(dup(0));
  ASSERT_TRUE(fd0);
  int rv0 = fcntl(fd0.get(), F_GETFD);
  ASSERT_GE(rv0, 0);
  EXPECT_FALSE(rv0 & FD_CLOEXEC);

  UniqueFileHandle fd1 = DuplicateFileHandle(fd0);
  ASSERT_TRUE(fd1);
  int rv1 = fcntl(fd1.get(), F_GETFD);
  ASSERT_GE(rv1, 0);
  EXPECT_TRUE(rv1 & FD_CLOEXEC);
}

#endif  // XP_UNIX
