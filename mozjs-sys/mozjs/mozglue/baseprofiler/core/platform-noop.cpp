/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

namespace mozilla {
namespace baseprofiler {

// StreamMetaJSCustomObject needs the "Unix epoch" (microseconds since January
// 1, 1970 GMT). In the Profiler, we use PR_Now to get that information.
// However, we cannot use PR_Now in the baseprofiler because PR_Now belongs to
// nspr which is not linked to mozglue.
//
// Stub returns microseconds since ProcessStartTime.
static int64_t MicrosecondsSince1970() {
  return static_cast<int64_t>(
      (TimeStamp::Now() - CorePS::ProcessStartTime()).ToMicroseconds());
}

// @aGuess: a pointer to a variable on the stack (with attribute("stack")).
// Note: GetStackTop is manually inlined in
// mozilla::profiler::ThreadRegistrationData::ThreadRegistrationData.
void* GetStackTop(void* aGuess) { return aGuess; }

// On linux and freebsd, we use SIGPROF to Suspend threads before sampling them.
// However, if that thread is currently inside fork(), the fork() will restart
// which can cause serious delays. PlatformInit is currently used to install
// hooks at the beginning and at the end of fork(). The hooks pause and resume
// the sampling, i.e. SIGPROF is no longer sent. As a result, fork() pauses
// sampling of all threads, not just the threads that are forking.
static void PlatformInit(PSLockRef aLock) { /* Noop */ }

// The thread that we want to profile will construct an instance of
// PlatformData. The constructor can store platform specific data related to
// that thread to better suspend/resume it afterwards in
// Sampler::SuspendAndSampleAndResumeThread.
class PlatformData {
 public:
  explicit PlatformData(BaseProfilerThreadId aThreadId) { /* Noop */ }
};

// Prepare this thread to be sampled in the future.
// On linux and freebsd, we register a handler for SIGPROF.
Sampler::Sampler(PSLockRef aLock) { /* Noop */ }

// Restore modifications done in Constructor.
// On linux and freebsd, we deregister/restore the original SIGPROF handler.
void Sampler::Disable(PSLockRef aLock) { /* Noop */ }

template <typename Func>
void Sampler::SuspendAndSampleAndResumeThread(
    PSLockRef aLock, const RegisteredThread& aRegisteredThread,
    const TimeStamp& aNow, const Func& aProcessRegs) { /* Noop */ }

// Spawn a thread that calls mSampler->Run()
SamplerThread::SamplerThread(PSLockRef aLock, uint32_t aActivityGeneration,
                             double aIntervalMilliseconds, uint32_t aFeatures)
    : mSampler(aLock),
      mActivityGeneration(aActivityGeneration),
      mIntervalMicroseconds(std::max(
          1, int(floor(aIntervalMilliseconds * 1000 + 0.5)))) { /* Noop */ }
// Wait for the thread to terminate
SamplerThread::~SamplerThread() { /* Noop */ }
void SamplerThread::Stop(PSLockRef aLock) { mSampler.Disable(aLock); }
void SamplerThread::SleepMicro(uint32_t aMicroseconds) {
  MOZ_CRASH("Not reachable because we never spawn SamplerThread");
}
}  // namespace baseprofiler
}  // namespace mozilla
