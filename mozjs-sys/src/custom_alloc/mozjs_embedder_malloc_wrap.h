#pragma once

// Todo: This file should be provided by the embedder, but that would likely cause linker issues,
// so perhaps we would need a wrapper library inbetween - or weak symbols.....
#define SERVO_EMBEDDER_MALLOC_PREFIX mi_
