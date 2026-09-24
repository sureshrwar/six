/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/Disk.h — AOSP system/vold/model/Disk.h
 */

#ifndef ANDROID_VOLD_DISK_H
#define ANDROID_VOLD_DISK_H

#include "VolumeBase.h"

namespace android {
namespace vold {

class Disk {
public:
    enum Flags {
        kAdoptable = 1 << 0,
        kDefaultPrimary = 1 << 1,
        kSd = 1 << 2,
        kUsb = 1 << 3,
        kEmulated = 1 << 4,
        kStub = 1 << 5,
    };

    Disk(const std::string& eventPath, dev_t device, const std::string& nickname, int flags);
    virtual ~Disk();

    const std::string& getId() const { return mId; }
    const std::string& getEventPath() const { return mEventPath; }
    const std::string& getSysPath() const { return mSysPath; }
    const std::string& getDevPath() const { return mDevPath; }
    dev_t getDevice() const { return mDevice; }
    uint64_t getSize() const { return mSize; }
    const std::string& getLabel() const { return mLabel; }
    int getFlags() const { return mFlags; }

    std::shared_ptr<VolumeBase> findVolume(const std::string& id);
    void listVolumes(VolumeBase::Type type, std::list<std::string>& list) const;

    virtual status_t create();
    virtual status_t destroy();

    virtual status_t readMetadata();
    virtual status_t readPartitions();

    status_t unmountAll();
    status_t partitionPublic();
    status_t partitionPrivate();
    status_t partitionMixed(int8_t ratio);
    status_t partitionFs(const std::string& fsType);

protected:
    std::string mId;
    std::string mEventPath;
    std::string mSysPath;
    std::string mDevPath;
    dev_t mDevice;
    uint64_t mSize;
    std::string mLabel;
    int mFlags;
    std::string mNickname;
    bool mCreated;

    std::list<std::shared_ptr<VolumeBase>> mVolumes;

    void createPublicVolume(dev_t device, const std::string& fstype = "",
                            const std::string& mntopts = "");
    void createPrivateVolume(dev_t device, const std::string& partGuid);
    void destroyAllVolumes();
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_DISK_H
