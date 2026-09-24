/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/PrivateVolume.cpp — AOSP system/vold/model/PrivateVolume.cpp
 * Adoptable storage encrypted with dm-crypt (ChaCha20-256) and stacked EmulatedVolume.
 */

#include "PrivateVolume.h"
#include "EmulatedVolume.h"
#include "../Utils.h"
#include "../VolumeManager.h"
#include "../fs/Ext2.h"

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

using android::base::StringPrintf;

namespace android {
namespace vold {

PrivateVolume::PrivateVolume(dev_t device, const std::string& keyRaw)
    : VolumeBase(Type::kPrivate), mDevice(device), mKeyRaw(keyRaw) {
    setId(StringPrintf("private:%u,%u", major(device), minor(device)));
    mRawDevPath = StringPrintf("/dev/block/vold/%s", getId().c_str());
}

PrivateVolume::~PrivateVolume() {}

status_t PrivateVolume::doCreate() {
    if (CreateDeviceNode(mRawDevPath, mDevice) != OK) {
        return -EIO;
    }
    if (SetupDmCryptDevice("crypt_usb", mRawDevPath, mKeyRaw, &mDmDevPath) != OK) {
        PLOG(ERROR) << getId() << " failed to setup dm-crypt device";
        return -EIO;
    }
    mFsType = "dm-crypt+ext2";
    mFsUuid = "CRYPT-8A01";
    mFsLabel = "ADOPTABLE_USB";
    auto listener = getListener();
    if (listener) {
        listener->onVolumeMetadataChanged(getId(), mFsType, mFsUuid, mFsLabel);
    }
    return OK;
}

status_t PrivateVolume::doDestroy() {
    TeardownDmCryptDevice("crypt_usb");
    return DestroyDeviceNode(mRawDevPath);
}

status_t PrivateVolume::doMount() {
    mPath = StringPrintf("/mnt/expand/%s", mFsUuid.c_str());
    setInternalPath(mPath);
    setPath(mPath);

    if (PrepareDir(mPath, 0771, AID_ROOT, AID_ROOT) != OK) {
        return -EIO;
    }

    unlink("/mnt/expand/usb");
    rmdir("/mnt/expand/usb");
    symlink(mPath.c_str(), "/mnt/expand/usb");

    if (ext2::Mount(mDmDevPath, mPath, false, false, true) != OK) {
        PLOG(ERROR) << getId() << " failed to mount dm-crypt volume " << mDmDevPath;
        return -EIO;
    }

    /* Populate adoptable metadata directory layout (/mnt/expand/<uuid>/media/0) */
    std::string mediaPath = mPath + "/media";
    PrepareDir(mediaPath, 0770, AID_ROOT, AID_MEDIA_RW);

    int fd = open((mPath + "/ADOPTABLE_KEY_INFO.txt").c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd >= 0) {
        const char* info =
            "Android Adoptable Storage (AOSP PrivateVolume + dm-crypt ChaCha20-256)\n"
            "Raw Device:     /dev/block/vold/private:8,1 (8:1)\n"
            "Mapped Device:  /dev/mapper/crypt_usb (254:0)\n"
            "Expand Path:    /mnt/expand/CRYPT-8A01 (/mnt/expand/usb)\n"
            "Key Location:   /data/misc/vold/expand_CRYPT-8A01.key\n";
        write(fd, info, strlen(info));
        close(fd);
    }
    sync();
    return OK;
}

void PrivateVolume::doPostMount() {
    std::string mediaPath = mPath + "/media";
    auto vol = std::make_shared<EmulatedVolume>(mediaPath, mDevice, mFsUuid);
    addVolume(vol);
    vol->create();
    vol->mount();
}

status_t PrivateVolume::doUnmount() {
    ForceUnmount(mPath);
    unlink("/mnt/expand/usb");
    rmdir(mPath.c_str());
    return OK;
}

status_t PrivateVolume::doFormat(const std::string& /*fsType*/) {
    return ext2::Format(mDmDevPath);
}

}  // namespace vold
}  // namespace android
