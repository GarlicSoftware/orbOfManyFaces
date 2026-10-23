#pragma once
#include <cstddef>
#include <cstdint>
// Deterministic hash double for storage-fault tests only. Production uses mbedTLS.
struct mbedtls_sha256_context {uint32_t h=2166136261u;};
inline void mbedtls_sha256_init(mbedtls_sha256_context *c){c->h=2166136261u;}
inline void mbedtls_sha256_free(mbedtls_sha256_context*){}
inline int mbedtls_sha256_starts(mbedtls_sha256_context *c,int){c->h=2166136261u;return 0;}
inline int mbedtls_sha256_update(mbedtls_sha256_context *c,const uint8_t*p,size_t n){while(n--){c->h^=*p++;c->h*=16777619u;}return 0;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context *c,uint8_t*p){for(int i=0;i<32;++i)p[i]=uint8_t((c->h>>((i%4)*8))^i);return 0;}
