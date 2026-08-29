/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_JitSpewChannelList_h
#define jit_JitSpewChannelList_h

// List of JitSpew channels.
//
// Each entry is `_(name, "one-line help text")`. The help text is shown by
// `MOZ_LOG=help` and `IONFLAGS=help`.
#define JITSPEW_CHANNEL_LIST(_)                                              \
  _(Prune, "Prune unused branches")                                          \
  _(Escape, "Escape analysis")                                               \
  _(Alias, "Alias analysis")                                                 \
  _(AliasSummaries, "Alias analysis: shows summaries for every block")       \
  _(GVN, "Global Value Numbering")                                           \
  _(Sink, "Sink transformation")                                             \
  _(Range, "Range Analysis")                                                 \
  _(LICM, "Loop invariant code motion")                                      \
  _(BranchHint, "Wasm Branch Hinting")                                       \
  _(FLAC, "Fold linear arithmetic constants")                                \
  _(EAA, "Effective address analysis")                                       \
  _(WasmBCE, "Wasm Bounds Check Elimination")                                \
  _(RegAlloc, "Register allocation")                                         \
  _(Inlining, "Inlining")                                                    \
  _(Codegen, "Native code generation")                                       \
  _(Safepoints, "Safepoints")                                                \
  _(Pools, "Literal Pools (ARM only for now)")                               \
  _(Profiling, "Profiling-related information")                              \
  _(CacheFlush, "Instruction Cache flushes (ARM only for now)")              \
  _(RedundantShapeGuards, "Redundant shape guard elimination")               \
  _(RedundantGCBarriers, "Redundant GC barrier elimination")                 \
  _(MarkLoadsUsedAsPropertyKeys, "Loads used as property keys")              \
  _(MIRExpressions, "Dump the MIR expressions")                              \
  _(Unroll, "Wasm loop unrolling and peeling -- summary info")               \
  _(UnrollDetails, "Wasm loop unrolling and peeling -- details")             \
  _(StubFolding, "CacheIR stub folding")                                     \
  _(StubFoldingDetails, "Spewing of stub content during folding")            \
                                                                             \
  /* BASELINE COMPILER SPEW */                                               \
  _(BaselineAbort, "Baseline compiler abort messages")                       \
  _(BaselineScripts, "Baseline script-compilation")                          \
  _(BaselineOp, "Baseline compiler detailed op-specific messages")           \
  _(BaselineIC, "Baseline inline-cache messages")                            \
  _(BaselineICFallback, "Baseline IC fallback stub messages")                \
  _(BaselineOSR, "Baseline IC OSR messages")                                 \
  _(BaselineBailouts, "Baseline bailouts")                                   \
  _(BaselineDebugModeOSR, "Baseline debug mode on stack recompile messages") \
                                                                             \
  /* ION COMPILER SPEW */                                                    \
  _(IonAbort, "Compilation abort messages")                                  \
  _(IonScripts, "Compiled scripts")                                          \
  _(IonSyncLogs, "Info about failing to log script")                         \
  _(IonMIR, "MIR information")                                               \
  _(IonBailouts, "Bailouts")                                                 \
  _(IonInvalidate, "Invalidation")                                           \
  _(IonSnapshots, "Snapshot information")                                    \
  _(IonIC, "Inline caches")                                                  \
                                                                             \
  /* WARP SPEW */                                                            \
  _(WarpSnapshots, "WarpSnapshots created by WarpOracle")                    \
  _(WarpTranspiler, "Warp CacheIR transpiler")                               \
  _(WarpTrialInlining, "Trial inlining for Warp")

// List of IONFLAGS short-names. Used by the IONFLAGS parser and by
// IONFLAGS=help.
#define IONFLAGS_CHANNEL_LIST(_)              \
  _("aborts", IonAbort)                       \
  _("scripts", IonScripts)                    \
  _("mir", IonMIR)                            \
  _("prune", Prune)                           \
  _("escape", Escape)                         \
  _("alias", Alias)                           \
  _("alias-sum", AliasSummaries)              \
  _("gvn", GVN)                               \
  _("range", Range)                           \
  _("wasmbce", WasmBCE)                       \
  _("branch-hint", BranchHint)                \
  _("licm", LICM)                             \
  _("flac", FLAC)                             \
  _("eaa", EAA)                               \
  _("sink", Sink)                             \
  _("regalloc", RegAlloc)                     \
  _("inline", Inlining)                       \
  _("snapshots", IonSnapshots)                \
  _("codegen", Codegen)                       \
  _("bailouts", IonBailouts)                  \
  _("osi", IonInvalidate)                     \
  _("caches", IonIC)                          \
  _("safepoints", Safepoints)                 \
  _("pools", Pools)                           \
  _("cacheflush", CacheFlush)                 \
  _("shapeguards", RedundantShapeGuards)      \
  _("gcbarriers", RedundantGCBarriers)        \
  _("loadkeys", MarkLoadsUsedAsPropertyKeys)  \
  _("stubfolding", StubFolding)               \
  _("profiling", Profiling)                   \
  _("dump-mir-expr", MIRExpressions)          \
  _("unroll", Unroll)                         \
  _("warp-snapshots", WarpSnapshots)          \
  _("warp-transpiler", WarpTranspiler)        \
  _("warp-trial-inlining", WarpTrialInlining) \
  _("bl-aborts", BaselineAbort)               \
  _("bl-scripts", BaselineScripts)            \
  _("bl-op", BaselineOp)                      \
  _("bl-ic", BaselineIC)                      \
  _("bl-ic-fb", BaselineICFallback)           \
  _("bl-osr", BaselineOSR)                    \
  _("bl-bails", BaselineBailouts)             \
  _("bl-dbg-osr", BaselineDebugModeOSR)

#endif /* jit_JitSpewChannelList_h */
