#pragma once

/** Example of an embedder header providing custom allocations

    Re-export the header by simply #including it here.
    We expect all allocating functions to be provided via their standard name
    with a prefix of SERVO_EMBEDDER_MALLOC_PREFIX,
    e.g. `<SERVO_EMBEDDER_MALLOC_PREFIX>malloc` -> `mi_malloc` given a prefix of `mi_`.

    If some functions are called differently in the original header, you can also provide
    static inline wrapper functions here that translate the expected name to the actual name.
*/
#include <mimalloc.h>
#define SERVO_EMBEDDER_MALLOC_PREFIX mi_
