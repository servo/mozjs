# Example: Using mozjs with an embedder-provided allocator

1. In your project setup an `include` directory containing `servo_embedder_allocator.h` defining
   `SERVO_EMBEDDER_MALLOC_PREFIX` and all required allocation functions with the corresponding prefix.
    See `mimalloc/include` for an example based on mimalloc.
2. Enable the feature `custom-alloc` and set the environment variable `SERVO_CUSTOM_ALLOC_INCLUDE_DIR` to
   point to the include directory created in step 1.
3. TODO: An environment variable to instruct `build.rs` of SM to link against custom allocator library?