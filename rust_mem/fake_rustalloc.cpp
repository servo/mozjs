/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdlib.h>

extern "C" {

void*  __rust_allocate(size_t bytes, size_t align)
{
    return malloc(bytes);
}

void* __rust_reallocate(void* p, size_t old_size, size_t size, size_t align)
{
    return realloc(p, size);
}

void __rust_deallocate(void* ptr, size_t old_size, size_t align)
{
    free(ptr);
}

}
