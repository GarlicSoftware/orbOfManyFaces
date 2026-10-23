#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
inline void *heap_caps_malloc(size_t n,int){return std::malloc(n);}
inline size_t heap_caps_get_free_size(int){return 180000;}
