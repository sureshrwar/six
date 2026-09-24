/*
 * applications/vold/compat/android-base/stringprintf.h
 *
 * AOSP <android-base/stringprintf.h> implementation.
 */

#ifndef _ANDROID_BASE_STRINGPRINTF_H
#define _ANDROID_BASE_STRINGPRINTF_H

#include "../six_cxx_compat.h"

namespace android {
namespace base {

static inline std::string StringPrintf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

} // namespace base
} // namespace android

#endif /* _ANDROID_BASE_STRINGPRINTF_H */
