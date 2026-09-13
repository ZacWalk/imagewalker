#ifndef APPLIB_H
#define APPLIB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Name this library was scaffolded with. Never NULL. */
const char* applib_name(void);

/* FNV-1a over count bytes. data may be NULL when count is 0. */
uint32_t applib_hash(const void* data, size_t count);

#ifdef __cplusplus
}
#endif

#endif
