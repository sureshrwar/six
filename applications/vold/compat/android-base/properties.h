/*
 * applications/vold/compat/android-base/properties.h
 *
 * AOSP <android-base/properties.h> shim.
 */

#ifndef _ANDROID_BASE_PROPERTIES_H
#define _ANDROID_BASE_PROPERTIES_H

#include "../six_cxx_compat.h"

namespace android {
namespace base {

static inline std::string GetProperty(const std::string& key, const std::string& default_value) {
    if (key == "ro.build.version.sdk") return "34";
    if (key == "vold.has_adoptable") return "1";
    return default_value;
}

static inline bool GetBoolProperty(const std::string& key, bool default_value) {
    if (key == "vold.has_adoptable") return true;
    return default_value;
}

static inline bool SetProperty(const std::string& /*key*/, const std::string& /*value*/) {
    return true;
}

} // namespace base
} // namespace android

#endif /* _ANDROID_BASE_PROPERTIES_H */
