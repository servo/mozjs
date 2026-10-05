.. _build_toolchains-update-policy:

========================
Toolchains update policy
========================

We document here the decision making and planning around when we update the
various toolchains used and required to build Firefox.

Rust
====

The Rust update policy is documented in a
:ref:`separate document<Rust Update Policy>`.

Clang
=====

*We ship official stable Firefox with a released Clang.*

As a general rule, we update the Clang compiler version used to build Firefox
Nightly as soon as possible, considering the constraint that the Clang compiler
and the Rust compiler need to use the same version of LLVM.

We don't upgrade the Clang version in the beta or release branches of Firefox.

The following exception applies:

- We may skip the update (or backout the update) if major problems are
  encountered (typically, we’ve had to do so because of build problems, crash
  reporting bustage, or performance issues).

*Local developer builds use the same version as used for the official builds.*

Minimum Supported Clang Version
-------------------------------

*We will update the Minimum Supported Clang Version to match the smallest LLVM
version supported by the Minimum Supported Rust Version.*

That smallest LLVM version can be found in the `check_llvm_version` function in
the `src/bootstrap/src/core/build_steps/llvm.rs` source file in the Rust
compiler source code for the Minimum Supported Rust Version.

Rationale
^^^^^^^^^

Downstreams that need to procure a Rust compiler will also need an LLVM
toolchain, which in practice includes a Clang with the same version. Since Rust
enforces a minimum supported LLVM version, setting our Minimum Supported Clang
Version to match that same baseline makes the requirements straightforward: a
single minimum LLVM version covers both Rust and C/C++ compilation.

This approach strikes a balance between the constraints faced by downstreams in
acquiring and maintaining toolchains, and our own need to use reasonably modern
compilers for C and C++ code.

libstdc++
=========

*We build stable Firefox with a libstdc++ that allows to provide the necessary
binary compatibility*

The binary compatibility is defined by the Firefox system requirements on Linux.
Those can be adjusted but a balance must be made between the breadth of systems
supported by Firefox builds, and the desired C++ features on the development
side. As of writing there isn't a policy describing exactly how the system
requirements are updated, yet.

Tricks may be put in place to guarantee the required binary compatibility with
older versions of libstdc++ than the one used to build.
(see `build/unix/stdc++compat`)

GCC
===

*The Minimum Supported GCC Version matches the version of libstdc++ above*

GCC and libstdc++ are, generally speaking, coupled, and it can be difficult to
use a version of GCC that doesn't match the version of libstdc++. As such, we
don't support building with a version of GCC older than the version of
libstdc++ Firefox is built against by default (from the development sysroot).
