#pragma once

#include <mimalloc.h>

#define mozmem_malloc_impl(fn) mi_ ## fn
#define mozmem_dup_impl(fn) mi_ ## fn
