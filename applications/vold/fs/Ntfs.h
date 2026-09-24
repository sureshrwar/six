/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * applications/vold/fs/Ntfs.h — AOSP system/vold/fs/Ntfs.h (ag/41271392)
 */

#ifndef ANDROID_VOLD_NTFS_H
#define ANDROID_VOLD_NTFS_H

#include "../Utils.h"

/*
 * NTFS support via userspace FUSE driver (ntfs-3g).
 *
 * Architecture Note:
 * This operates as a "lower-tier" block-level FUSE driver: it mounts the raw
 * partition under /mnt/media_rw/<uuid> as a drop-in replacement for in-kernel
 * filesystems (such as vfat or exfat).
 *
 * This is completely separate from Android's "upper-tier" MediaProvider FUSE daemon,
 * which provides Scoped Storage virtualization under /storage/... on top of the
 * volume's internal mount path.
 */
namespace android {
namespace vold {
namespace ntfs {

bool IsSupported();

status_t Check(const std::string& source);
status_t Mount(const std::string& source, const std::string& target, int ownerUid, int ownerGid,
               int permMask, bool ro = false, pid_t* outDriverPid = nullptr);
status_t Format(const std::string& source);

}  // namespace ntfs
}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_NTFS_H
