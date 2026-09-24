/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/PublicVolume.h — AOSP system/vold/model/PublicVolume.h
 */

#ifndef ANDROID_VOLD_PUBLIC_VOLUME_H
#define ANDROID_VOLD_PUBLIC_VOLUME_H

#include "VolumeBase.h"

namespace android {
namespace vold {

class PublicVolume : public VolumeBase {
public:
    explicit PublicVolume(dev_t device, const std::string& nickname = "",
                          const std::string& mntopts = "", const std::string& fstype = "");
    virtual ~PublicVolume();

protected:
    status_t doCreate() override;
    status_t doDestroy() override;
    status_t doMount() override;
    status_t doUnmount() override;
    status_t doFormat(const std::string& fsType) override;

    status_t readMetadata();

private:
    dev_t mDevice;
    std::string mDevPath;
    std::string mRawPath;
    std::string mFsType;
    std::string mFsUuid;
    std::string mFsLabel;
    std::string mMntOpts;

    /*
     * PID of the lower-tier userspace filesystem driver daemon (e.g. ntfs-3g FUSE).
     * This driver mounts the raw block device under /mnt/media_rw/ before any
     * upper-tier MediaProvider FUSE or sdcardfs layer is mounted on top.
     */
    pid_t mDriverPid;
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_PUBLIC_VOLUME_H
