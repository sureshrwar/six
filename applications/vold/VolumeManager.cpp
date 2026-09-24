/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * applications/vold/VolumeManager.cpp — AOSP system/vold/VolumeManager.cpp
 */

#include "VolumeManager.h"
#include "model/EmulatedVolume.h"

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

using android::base::StringPrintf;

namespace android {
namespace vold {

VolumeManager* VolumeManager::sInstance = nullptr;

VolumeManager* VolumeManager::Instance() {
    if (!sInstance) {
        sInstance = new VolumeManager();
    }
    return sInstance;
}

VolumeManager::VolumeManager() {}

VolumeManager::~VolumeManager() {}

int VolumeManager::start() {
    unmountAll();

    auto vol = std::make_shared<EmulatedVolume>("/data/media");
    vol->setMountUserId(0);
    vol->create();
    vol->mount();
    mInternalEmulatedVolumes.push_back(vol);
    return 0;
}

int VolumeManager::stop() {
    for (auto it = mInternalEmulatedVolumes.begin(); it != mInternalEmulatedVolumes.end(); ++it) {
        auto vol = *it;
        vol->destroy();
    }
    mInternalEmulatedVolumes.clear();
    return 0;
}

void VolumeManager::handleBlockEvent(NetlinkEvent* evt) {
    const char* eventPathStr = evt->findParam("DEVPATH");
    const char* devTypeStr = evt->findParam("DEVTYPE");
    const char* majorStr = evt->findParam("MAJOR");
    const char* minorStr = evt->findParam("MINOR");

    std::string eventPath = eventPathStr ? eventPathStr : "";
    std::string devType = devTypeStr ? devTypeStr : "disk";
    int maj = majorStr ? atoi(majorStr) : 8;
    int min = minorStr ? atoi(minorStr) : 0;
    (void)min;

    /* Disk device in AOSP is always whole-disk minor 0 (e.g. 8:0 for sda) */
    dev_t diskDevice = makedev(maj, 0);

    LOG(DEBUG) << "handleBlockEvent: action=" << static_cast<int>(evt->getAction())
               << " devPath=" << eventPath << " devType=" << devType
               << " disk=" << maj << ":0";

    switch (evt->getAction()) {
        case NetlinkEvent::Action::kAdd: {
            for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
                auto existing = *it;
                if (existing->getDevice() == diskDevice) {
                    existing->destroy();
                    mDisks.erase(it);
                    break;
                }
            }

            int flags = Disk::Flags::kAdoptable | Disk::Flags::kUsb;
            auto disk = std::make_shared<android::vold::Disk>(eventPath, diskDevice, "usb", flags);
            mDisks.push_back(disk);
            disk->create();
            break;
        }
        case NetlinkEvent::Action::kChange: {
            for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
                auto disk = *it;
                if (disk->getDevice() == diskDevice) {
                    disk->readMetadata();
                    disk->readPartitions();
                }
            }
            break;
        }
        case NetlinkEvent::Action::kRemove: {
            for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
                auto disk = *it;
                if (disk->getDevice() == diskDevice) {
                    disk->destroy();
                    mDisks.erase(it);
                    break;
                }
            }
            break;
        }
        default: {
            LOG(WARNING) << "Unexpected block event action " << static_cast<int>(evt->getAction());
            break;
        }
    }
}

std::shared_ptr<Disk> VolumeManager::findDisk(const std::string& id) {
    for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
        auto disk = *it;
        if (disk->getId() == id) {
            return disk;
        }
    }
    return nullptr;
}

std::shared_ptr<VolumeBase> VolumeManager::findVolume(const std::string& id) {
    for (auto it = mInternalEmulatedVolumes.begin(); it != mInternalEmulatedVolumes.end(); ++it) {
        auto vol = *it;
        if (vol->getId() == id) {
            return vol;
        }
    }
    for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
        auto disk = *it;
        auto vol = disk->findVolume(id);
        if (vol != nullptr) {
            return vol;
        }
    }
    return nullptr;
}

int VolumeManager::unmountAll() {
    for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
        auto disk = *it;
        disk->unmountAll();
    }
    return 0;
}

int VolumeManager::reset() {
    for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
        auto disk = *it;
        disk->destroy();
        disk->create();
    }
    return 0;
}

}  // namespace vold
}  // namespace android
