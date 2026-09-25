/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/Disk.cpp — AOSP system/vold/model/Disk.cpp
 */

#include "Disk.h"
#include "PublicVolume.h"
#include "PrivateVolume.h"
#include "../Utils.h"
#include "../VolumeManager.h"

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <errno.h>

using android::base::StringPrintf;

namespace android {
namespace vold {

Disk::Disk(const std::string& eventPath, dev_t device, const std::string& nickname, int flags)
    : mEventPath(eventPath),
      mDevice(device),
      mSize(2048 * 1024),
      mLabel("SanDisk Ultra USB 3.0"),
      mFlags(flags),
      mNickname(nickname),
      mCreated(false) {
    mId = StringPrintf("disk:%u,%u", major(device), minor(device));
    mSysPath = StringPrintf("/sys/%s", eventPath.c_str());
    mDevPath = StringPrintf("/dev/block/vold/%s", mId.c_str());
    CreateDeviceNode(mDevPath, mDevice);
}

Disk::~Disk() {
    DestroyDeviceNode(mDevPath);
}

std::shared_ptr<VolumeBase> Disk::findVolume(const std::string& id) {
    for (auto it = mVolumes.begin(); it != mVolumes.end(); ++it) {
        auto vol = *it;
        if (vol->getId() == id) {
            return vol;
        }
        auto stackedVol = vol->findVolume(id);
        if (stackedVol != nullptr) {
            return stackedVol;
        }
    }
    return nullptr;
}

void Disk::listVolumes(VolumeBase::Type type, std::list<std::string>& list) const {
    for (auto it = mVolumes.begin(); it != mVolumes.end(); ++it) {
        auto vol = *it;
        if (vol->getType() == type) {
            list.push_back(vol->getId());
        }
    }
}

status_t Disk::create() {
    if (mCreated) return BAD_VALUE;
    mCreated = true;

    auto listener = VolumeManager::Instance()->getListener();
    if (listener) {
        listener->onDiskCreated(getId(), mFlags);
    }

    readMetadata();
    readPartitions();
    return OK;
}

status_t Disk::destroy() {
    if (!mCreated) return BAD_VALUE;
    destroyAllVolumes();
    mCreated = false;

    auto listener = VolumeManager::Instance()->getListener();
    if (listener) {
        listener->onDiskDestroyed(getId());
    }
    return OK;
}

void Disk::createPublicVolume(dev_t device, const std::string& fstype,
                              const std::string& mntopts) {
    auto vol = std::make_shared<PublicVolume>(device, mNickname, mntopts, fstype);
    vol->setDiskId(getId());
    mVolumes.push_back(vol);
    vol->create();
    /* In SIX/Android TV auto-mount policy, PublicVolume mounts on creation */
    vol->mount();
}

void Disk::createPrivateVolume(dev_t device, const std::string& /*partGuid*/) {
    int bfd = open("/dev/binder", O_RDWR);
    if (bfd >= 0) {
        struct binder_uevent_msg uev;
        memset(&uev, 0, sizeof(uev));
        strcpy(uev.action, "prepare");
        strcpy(uev.subsystem, "block");
        strcpy(uev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
        strcpy(uev.devname, "sda1");
        uev.major = 8;
        uev.minor = 1;
        strcpy(uev.fstype, "crypt");
        strcpy(uev.label, "ADOPTABLE_USB");
        strcpy(uev.uuid, "CRYPT-8A01");
        ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev);
        close(bfd);
    }

    std::string keyHex =
        "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
    mkdir("/data", 0755);
    mkdir("/data/misc", 0700);
    mkdir("/data/misc/vold", 0700);
    int kfd = open("/data/misc/vold/expand_CRYPT-8A01.key", O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (kfd >= 0) {
        write(kfd, keyHex.c_str(), keyHex.size());
        write(kfd, "\n", 1);
        close(kfd);
    }

    auto vol = std::make_shared<PrivateVolume>(device, keyHex);
    vol->setDiskId(getId());
    mVolumes.push_back(vol);
    vol->create();
    vol->format("ext2");
    vol->mount();
}

void Disk::destroyAllVolumes() {
    for (auto it = mVolumes.begin(); it != mVolumes.end(); ++it) {
        auto vol = *it;
        vol->destroy();
    }
    mVolumes.clear();
}

status_t Disk::readMetadata() {
    mSize = 2048 * 1024;
    mLabel = "SanDisk Ultra USB 3.0";

    auto listener = VolumeManager::Instance()->getListener();
    if (listener) {
        listener->onDiskMetadataChanged(getId(), mSize, mLabel, mSysPath);
    }
    return OK;
}

status_t Disk::readPartitions() {
    destroyAllVolumes();

    dev_t partDevice = makedev(major(mDevice), minor(mDevice) + 1);
    createPublicVolume(partDevice);

    auto listener = VolumeManager::Instance()->getListener();
    if (listener) {
        listener->onDiskScanned(getId());
    }
    return OK;
}

status_t Disk::unmountAll() {
    for (auto it = mVolumes.begin(); it != mVolumes.end(); ++it) {
        auto vol = *it;
        vol->unmount();
    }
    return OK;
}

status_t Disk::partitionPublic() {
    return partitionFs("ext2");
}

status_t Disk::partitionFs(const std::string& fsType) {
    destroyAllVolumes();

    dev_t partDevice = makedev(major(mDevice), minor(mDevice) + 1);
    auto vol = std::make_shared<PublicVolume>(partDevice, mNickname, "", fsType);
    vol->setDiskId(getId());
    mVolumes.push_back(vol);
    vol->create();
    vol->format(fsType);
    return vol->mount();
}

status_t Disk::partitionPrivate() {
    if (!(mFlags & Flags::kAdoptable)) {
        LOG(ERROR) << "Disk " << getId() << " is not adoptable (missing encryptable=userdata in /etc/fstab)";
        return -EINVAL;
    }
    destroyAllVolumes();

    dev_t partDevice = makedev(major(mDevice), minor(mDevice) + 1);
    createPrivateVolume(partDevice, "CRYPT-8A01");
    return OK;
}

status_t Disk::partitionMixed(int8_t /*ratio*/) {
    return partitionPublic();
}

}  // namespace vold
}  // namespace android
