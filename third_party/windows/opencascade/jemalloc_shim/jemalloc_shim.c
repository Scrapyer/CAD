#include <malloc.h>
#include <stdlib.h>

__declspec(dllexport) void* je_malloc(size_t size)
{
    return malloc(size);
}

__declspec(dllexport) void je_free(void* pointer)
{
    free(pointer);
}

__declspec(dllexport) void* je_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

__declspec(dllexport) void* je_realloc(void* pointer, size_t size)
{
    return realloc(pointer, size);
}

__declspec(dllexport) void* je_aligned_alloc(size_t alignment, size_t size)
{
    return _aligned_malloc(size, alignment);
}
