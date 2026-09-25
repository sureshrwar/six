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

    class DiskSource {
    public:
        DiskSource(const std::string& sysPattern, const std::string& nickname, int partnum,
                   int flags, const std::string& fstype = "", const std::string& mntopts = "")
            : mSysPattern(sysPattern),
              mNickname(nickname),
              mPartNum(partnum),
              mFlags(flags),
              mFsType(fstype),
              mMntOpts(mntopts) {}

        bool matches(const std::string& sysPath) const;

        const std::string& getSysPattern() const { return mSysPattern; }
        const std::string& getNickname() const { return mNickname; }
        int getPartNum() const { return mPartNum; }
        int getFlags() const { return mFlags; }
        const std::string& getFsType() const { return mFsType; }
        const std::string& getMntOpts() const { return mMntOpts; }

    private:
        std::string mSysPattern;
        std::string mNickname;
        int mPartNum;
        int mFlags;
        std::string mFsType;
        std::string mMntOpts;
    };

    void addDiskSource(const std::shared_ptr<DiskSource>& diskSource);
    int loadFstabConfig(const char* fstabPath = "/etc/fstab");

private:
    VolumeManager();

    static VolumeManager* sInstance;

    std::shared_ptr<android::os::IVoldListener> mListener;
    std::list<std::shared_ptr<DiskSource>> mDiskSources;
    std::list<std::shared_ptr<Disk>> mDisks;
    std::list<std::shared_ptr<VolumeBase>> mInternalEmulatedVolumes;
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_VOLUME_MANAGER_H
