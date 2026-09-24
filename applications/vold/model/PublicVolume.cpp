/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/PublicVolume.cpp — AOSP system/vold/model/PublicVolume.cpp
 */

#include "PublicVolume.h"
#include "../Utils.h"
#include "../fs/Ext2.h"
#include "../fs/Ext4.h"
#include "../fs/Ntfs.h"

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

using android::base::StringPrintf;

namespace android {
namespace vold {

static const char* kMediaRwPath = "/mnt/media_rw";

PublicVolume::PublicVolume(dev_t device, const std::string& nickname,
                           const std::string& mntopts, const std::string& fstype)
    : VolumeBase(Type::kPublic),
      mDevice(device),
      mFsType(fstype),
      mFsLabel(nickname),
      mMntOpts(mntopts) {
    setId(StringPrintf("public:%u,%u", major(device), minor(device)));
    mDevPath = StringPrintf("/dev/block/vold/%s", getId().c_str());
}

PublicVolume::~PublicVolume() {}

status_t PublicVolume::readMetadata() {
    status_t res = ReadMetadataUntrusted(mDevPath, &mFsType, &mFsUuid, &mFsLabel);
    auto listener = getListener();
    if (listener) {
        listener->onVolumeMetadataChanged(getId(), mFsType, mFsUuid, mFsLabel);
    }
    return res;
}

status_t PublicVolume::doCreate() {
    return CreateDeviceNode(mDevPath, mDevice);
}

status_t PublicVolume::doDestroy() {
    return DestroyDeviceNode(mDevPath);
}

status_t PublicVolume::doMount() {
    readMetadata();

    if (mFsType.empty()) {
        LOG(ERROR) << getId() << " unsupported or unrecognized filesystem on " << mDevPath;
        return -EIO;
    }

    std::string stableName = getId();
    if (!mFsUuid.empty()) {
        stableName = mFsUuid;
    }

    mRawPath = StringPrintf("%s/%s", kMediaRwPath, stableName.c_str());
    setInternalPath(mRawPath);
    setPath(mRawPath);

    if (PrepareDir(mRawPath, 0700, AID_ROOT, AID_ROOT) != OK) {
        PLOG(ERROR) << getId() << " failed to create mount point " << mRawPath;
        return -errno;
    }

    /* Also maintain /mnt/media_rw/usb convenience symlink pointing to /mnt/media_rw/<uuid> */
    unlink("/mnt/media_rw/usb");
    rmdir("/mnt/media_rw/usb");
    symlink(mRawPath.c_str(), "/mnt/media_rw/usb");

    status_t ret = OK;
    if (mFsType == "ext4" && ext4::IsSupported()) {
        ret = ext4::Check(mDevPath, mRawPath);
        if (ret == OK) {
            ret = ext4::Mount(mDevPath, mRawPath, false, false, true);
        }
    } else if (mFsType == "ntfs" && ntfs::IsSupported()) {
        ret = ntfs::Check(mDevPath);
        if (ret == OK) {
            ret = ntfs::Mount(mDevPath, mRawPath, AID_ROOT, AID_MEDIA_RW, 0007);
        }
    } else if (mFsType == "ext2" && ext2::IsSupported()) {
        ret = ext2::Check(mDevPath, mRawPath);
        if (ret == OK) {
            ret = ext2::Mount(mDevPath, mRawPath, false, false, true);
        }
    } else {
        LOG(ERROR) << getId() << " unsupported filesystem " << mFsType;
        ret = -EIO;
    }

    if (ret != OK) {
        PLOG(ERROR) << getId() << " failed to mount " << mDevPath << " (" << mFsType << ")";
        unlink("/mnt/media_rw/usb");
        rmdir(mRawPath.c_str());
        return ret;
    }

    LOG(INFO) << getId() << " mounted " << mDevPath << " (" << mFsType
              << ", uuid=" << mFsUuid << ", label=" << mFsLabel << ") at " << mRawPath;
    return OK;
}

status_t PublicVolume::doUnmount() {
    ForceUnmount(mDevPath);
    ForceUnmount(mRawPath);
    unlink("/mnt/media_rw/usb");
    rmdir(mRawPath.c_str());
    return OK;
}

status_t PublicVolume::doFormat(const std::string& fsType) {
    std::string targetFs = fsType;
    if (targetFs.empty() || targetFs == "auto" || targetFs == "public") {
        targetFs = "ext2";
    }

    status_t res = OK;
    if (targetFs == "ext4") {
        res = ext4::Format(mDevPath, 0, mRawPath);
    } else if (targetFs == "ntfs") {
        res = ntfs::Format(mDevPath);
    } else {
        res = ext2::Format(mDevPath);
    }
    readMetadata();
    return res;
}

}  // namespace vold
}  // namespace android
