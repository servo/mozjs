Scripts for building and downloading pre-built versions of SpiderMonkey, compiled to wasm32-wasi as a static library.

## Building from upstream source
The `build-engine.sh` script can be used to build either release or debug builds. It's recommended that this script only be used in CI environments, as it bootstraps a build environment for SpiderMonkey on each invokation.

The source is retrieved from the repository in `gecko-repository` at the revision in `gecko-revision`.

### Building for release or debug
The script can compile both release and debug builds, with `release` being the default. To compile a debug build, pass `debug` as the first argument:
```sh
sh build-engine.sh debug
```

### Build output
Running the build script will result in three different subdirectories in the current directory:
- `include`, containing the public header files
- `lib`, containing the object files and static libraries needed to statically link SpiderMonkey
- `obj`, the output directory the SpiderMonkey build system creates. This can be ignored or deleted

## Downloading pre-built releases
The `download-engine.sh` script can be used to download pre-built object files and headers for static linking for either release or debug builds. The object files are compiled to wasm32-wasi, and can be used on any platform.

Running `download-engine.sh [debug]` will download a tarball for a SpiderMonkey build (release by default, debug if the `debug` argument is passed), and extract it. When successful, it'll result in the subdirectories `lib` and `include` in the current working directory.

**Note:** `download-engine.sh` can be called from anywhere, but must itself reside in a git checkout whose `origin` is the github repository to download the release from. It expects that repository to have a release whose name is derived from the current git revision of the containing checkout.
