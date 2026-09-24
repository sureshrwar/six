/*
 * applications/vold/compat/six_cxx_runtime.cpp
 *
 * C++ ABI hooks (operator new/delete, __cxa_pure_virtual) over SIX guest libc.
 */

#include "six_cxx_compat.h"

void* operator new(size_t size) {
    if (size == 0) size = 1;
    return malloc(size);
}

void* operator new[](size_t size) {
    if (size == 0) size = 1;
    return malloc(size);
}

void operator delete(void* ptr) noexcept {
    if (ptr) free(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    if (ptr) free(ptr);
}

void operator delete[](void* ptr) noexcept {
    if (ptr) free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    if (ptr) free(ptr);
}

extern "C" void __cxa_pure_virtual() {
    printf("vold: pure virtual function called!\n");
    _exit(1);
}
