# How SpiderMonkey Optimizes

SpiderMonkey has a variety of optimization tools at its disposal. Improving program performance is often a matter of correctly identifying where a known tool has not yet been applied where it could be. This is of course not to say that novel optimizations are never necessary, but to avoid re-inventing the wheel it's good to know what already exists.

## Tiering

SpiderMonkey is a tiering JavaScript engine. This means that we focus compilation effort only to where we expect it to pay off.

The JS parser emits bytecode for a stack-based VM (See `js/src/vm/Opcodes.h` for the opcodes). We have four execution tiers. When we enter a function or reach a loop header, we increment a warmup counter. At predefined warmup thresholds, we tier up to the next execution tier. This can happen on function entry or at a loop header; in the latter case, we do on-stack replacement (OSR).

1. C++ interpreter: this is a standard threaded interpreter. See `js/src/vm/Interpreter.cpp`.
2. Baseline interpreter (blinterp): this is a JIT-compiled interpreter, which uses a small hand-crafted instruction sequence for each op. We generate a single instance of the baseline interpreter at runtime startup. The primary advantage over the C++ interpreter is the ability to use inline caches (ICs), which are a key part of our optimization strategy (See the next section for details).

    By default, we tier up to blinterp at warmup count 10.
3. Baseline compiler: this is a simple template JIT compiler. It is built on the same infrastructure as the baseline interpreter, and uses many of the same per-op handlers, but generates per-script jitcode. It prioritizes throughput. Its advantages over the baseline interpreter are that it eliminates dispatch overhead between ops, and it does simple tracking of the VM stack while compiling, reducing the amount of data that must be shuffled around. It uses the same ICs as baseline interpreter. The baseline compiler only uses bytecode as an input; it relies on ICs for dynamic optimizations. By default, we tier up to baseline at warmup count 100.
4. Ion: this is our optimizing compiler. By default, we tier up to Ion at warmup count 1500. In addition to bytecode, Ion uses baseline ICs as an input. Ion compilation is typically done off-thread. On the main thread, WarpOracle captures a snapshot containing the bytecode and relevant IC data. In a background thread, WarpBuilder and the WarpCacheIRTranspiler convert that snapshot into MIR. The MIR goes through a sequence of optimization passes, then is lowered to LIR for register allocation and code generation. If a speculative optimization in Ion fails, we may have to stop running Ion code. In such cases, we perform a bailout, which captures the data necessary to rewrite the stack as if we had been running in the baseline interpreter all along.

With regards to tiering, we try not to over optimize by tweaking specific thresholds, however, if we can learn something from a previous load then we will sometimes use thresholds to optimize early. An example of this is the system we call JitHints, which takes the execution from a previous page load and stores it in an in-memory cache, which can be used on subsequent page loads to lower the up warm up threshold for a specific function, allowing it to tier up to a higher level sooner than it otherwise would.

## Inline caches / CacheIR

CacheIR is the core of our speculative optimization strategy. It is a simple linear bytecode with only a few types of instructions:
1. Guards, which verify some property. If a guard fails, the entire stub fails. Examples: GuardShape, GuardToObject
2. Simple idempotent operations with no side-effects. Examples: LoadObject, LoadWrapperTarget
3. Result operations, which return a result value, and are the only operations that can have side-effects. There can only be one result operation in a CacheIR sequence, and it must be the last operation.

(Many more details of CacheIR are available in both `[SMDOC]` comments, as well as in [this more detailed CacheIR documentation](cacheir).)

When an IC-using operation happens, we will either use a pre-existing IC if there is one, or attempt to add a new cache case (using a `CacheIRGenerator`) to handle the specific types of inputs observed. This is the core form of specialization within SpiderMonkey, and CacheIR sequences are used in all execution engines outside the C++ interpreter. This means CacheIR based optimization has a large amount of lift: Implementing the CacheIR of an optimization means we get optimization in three execution egnines.

CacheIR can also be used as a limited form of inlining: For example if we see `obj.push(val)`, we can guard that obj and push qualify (array, no overrides, etc), and then simply do the push in CacheIR. See `InlinableNativeIRGenerator::tryAttachArrayPush` for the code.

## Fuses

Fuses represent a bit of knowledge about the state of the whole runtime or a specific realm or object. The name reflects the analogy -- should something go wrong, a fuse should pop. Fuses can be grouped together such that one fuse pops another, and a composite set of properties can be represented by a fuse which depends on all the individual properties. This allows the VM and JIT code to check a composite property of the runtime or realm by loading only a single byte out of memory.

Fuses are useful in cases where a) we do not expect them to be popped, and b) we can efficiently and reliably determine when to pop them.

In Ion compilation, the advantage of transpiling CacheIR guards to MIR instructions is that it makes it easy to reason about correctness. A disadvantage is that it requires the Ion-compiled code to emit guards, which are generally cheap but can add up in hot loops. In some cases, it is more efficient to remove the guard and instead invalidate the Ion-compiled code if the guard condition becomes false.

Fuses provide an excellent mechanism for this.  For example, optimizing a spread call requires us to check various prototypes to ensure that array iteration still works the normal way. Instead of guarding each individual field, we emit CacheIR that checks the `OptimizeGetIteratorBytecodeFuse`, which is popped if those conditions are no longer true. In an IC, we check the fuse directly. When transpiling to MIR, we instead register the IonScript with the fuse. If the fuse pops, then the Ion code will be invalidated, removing the dependency on the property.

We suspect fuses have quite a bit of room still for use within SpiderMonkey.

## Pretenuring

When a profile shows that we are spending a lot of time in minor GC promoting objects to tenured space, pretenuring is sometimes a useful tool.

We have a generational GC. Objects, Strings and BigInts are allocated in the nursery, which can be collected separately; once they survive one collection, they are promoted to tenured space. (Other GC things, like Scripts and Shapes, tend to have a longer lifetime, and are always tenured.) Nursery allocation is fast, and the cost of a minor collection is proportional to the number of objects that are promoted, so when the generational hypothesis holds (that is, when most objects die before leaving the nursery), it's a significant win. However, there are some exceptions. To improve our precision, we use allocation sites and pretenuring. When we allocate an object in the nursery, we may associate it with an allocation site, which represents the place it was allocated. For example, a NewObject IC will have its own allocation site, representing objects allocated by that IC. The allocation site is stored in a header before the nursery object itself. When we do a minor collection, for each alloc site, we can compute the percentage of objects allocated at that site that were tenured. If that number is very high (>90%), then we mark the site as LongLived. In the future, objects allocated at that site will be pretenured, skipping the nursery and the cost of promotion.
