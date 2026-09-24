/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * applications/vold/VolumeManager.h — AOSP system/vold/VolumeManager.h
 */

#ifndef ANDROID_VOLD_VOLUME_MANAGER_H
#define ANDROID_VOLD_VOLUME_MANAGER_H

#include "model/Disk.h"
#include "model/VolumeBase.h"
#include <sysutils/NetlinkEvent.h>
#include <android/os/IVoldListener.h>

namespace android {
namespace vold {

class VolumeManager {
public:
    virtual ~VolumeManager();

    static VolumeManager* Instance();

    int start();
    int stop();

    void handleBlockEvent(NetlinkEvent* evt);

    void setListener(std::shared_ptr<android::os::IVoldListener> listener) {
        mListener = listener;
    }
    std::shared_ptr<android::os::IVoldListener> getListener() const {
        return mListener;
    }

    std::shared_ptr<Disk> findDisk(const std::string& id);
    std::shared_ptr<VolumeBase> findVolume(const std::string& id);

    int reset();
    int unmountAll();

private:
    VolumeManager();

    static VolumeManager* sInstance;

    std::shared_ptr<android::os::IVoldListener> mListener;
    std::list<std::shared_ptr<Disk>> mDisks;
    std::list<std::shared_ptr<VolumeBase>> mInternalEmulatedVolumes;
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_VOLUME_MANAGER_H
