/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * applications/vold/fs/Ext4.h — AOSP system/vold/fs/Ext4.h
 */

#ifndef ANDROID_VOLD_EXT4_H
#define ANDROID_VOLD_EXT4_H

#include "../Utils.h"

namespace android {
namespace vold {
namespace ext4 {

bool IsSupported();

status_t Check(const std::string& source, const std::string& target);
status_t Mount(const std::string& source, const std::string& target, bool ro,
               bool remount, bool executable);
status_t Format(const std::string& source, unsigned int numSectors,
                const std::string& target);

}  // namespace ext4
}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_EXT4_H
