#pragma once

#include <mimalloc.h>
#include "servo_embedder_malloc_prefix.h"

#define SERVO_CONCAT(x, y) x ## y
#define SERVO_CONCAT2(x, y) SERVO_CONCAT(x, y)

#define mozmem_malloc_impl(fn) SERVO_CONCAT2(SERVO_EMBEDDER_MALLOC_PREFIX, fn)
#define mozmem_dup_impl(fn) SERVO_CONCAT2(SERVO_EMBEDDER_MALLOC_PREFIX, fn)
