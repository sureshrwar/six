/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/fs/Ext2.h
 */

#ifndef ANDROID_VOLD_EXT2_H
#define ANDROID_VOLD_EXT2_H

#include "../Utils.h"

namespace android {
namespace vold {
namespace ext2 {

bool IsSupported();

status_t Check(const std::string& source, const std::string& target);
status_t Mount(const std::string& source, const std::string& target, bool ro,
               bool remount, bool executable);
status_t Format(const std::string& source);

}  // namespace ext2
}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_EXT2_H
