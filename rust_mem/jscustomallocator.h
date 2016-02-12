/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define JS_OOM_POSSIBLY_FAIL() do {} while(0)
#define JS_OOM_POSSIBLY_FAIL_BOOL() do {} while(0)

namespace js {
namespace oom {
static inline bool IsSimulatedOOMAllocation() { return false; }
static inline bool ShouldFailWithOOM() { return false; }
} /* namespace oom */
} /* namespace js */

extern "C" void*  __rust_allocate(size_t bytes, size_t align);
static inline void* js_malloc(size_t bytes)
{
    return __rust_allocate(bytes, 0);
}

static inline void* js_calloc(size_t bytes)
{
    void* buf = __rust_allocate(bytes, 0);
    if (bytes && buf) {
        memset(buf, 0, bytes);
    }
    return buf;
}

static inline void* js_calloc(size_t nmemb, size_t size)
{
    size_t bytes = size * nmemb;
    void* buf = __rust_allocate(bytes, 0);
    if (bytes && buf) {
        memset(buf, 0, bytes);
    }
    return buf;
}

extern "C" void* __rust_reallocate(void* p, size_t old_size, size_t size, size_t align);
static inline void* js_realloc(void* p, size_t bytes)
{
    // XXX not actually safe, but no rust allocator uses the old size right now
    return __rust_reallocate(p, 0, bytes, 0);
}

extern "C" void __rust_deallocate(void* ptr, size_t old_size, size_t align);
static inline void js_free(void* p)
{
    __rust_deallocate(p, 0, 0);
}

static inline char* js_strdup(const char* s)
{
    size_t len = strlen(s);
    char* buf = (char*)__rust_allocate(len + 1, 0);
    if (buf) {
        memcpy(buf, s, len);
        buf[len] = 0;
    }

    return buf;
}

