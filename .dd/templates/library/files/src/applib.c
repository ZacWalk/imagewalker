#include "applib.h"

const char* applib_name(void)
{
    return "@NAME@";
}

uint32_t applib_hash(const void* data, size_t count)
{
    const unsigned char* bytes = (const unsigned char*)data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < count; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}
