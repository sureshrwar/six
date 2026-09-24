/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * applications/vold/fs/Ntfs.h — AOSP portable storage NTFS support via FUSE ntfs-3g
 */

#ifndef ANDROID_VOLD_NTFS_H
#define ANDROID_VOLD_NTFS_H

#include "../Utils.h"

namespace android {
namespace vold {
namespace ntfs {

bool IsSupported();

status_t Check(const std::string& source);
status_t Mount(const std::string& source, const std::string& target, int ownerUid,
               int ownerGid, int permMask);
status_t Format(const std::string& source);

}  // namespace ntfs
}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_NTFS_H
