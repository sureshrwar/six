/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/VolumeBase.h — AOSP system/vold/model/VolumeBase.h
 */

#ifndef ANDROID_VOLD_VOLUME_BASE_H
#define ANDROID_VOLD_VOLUME_BASE_H

#include "../Utils.h"
#include <android/os/IVoldListener.h>

namespace android {
namespace vold {

class VolumeBase {
public:
    enum class Type {
        kPublic = 0,
        kPrivate = 1,
        kEmulated = 2,
        kAsec = 3,
        kObb = 4,
        kStub = 5,
    };

    enum MountFlags {
        kPrimary = 1 << 0,
        kVisible = 1 << 1,
    };

    enum class State {
        kUnmounted = 0,
        kChecking = 1,
        kMounted = 2,
        kMountedReadOnly = 3,
        kFormatting = 4,
        kEjecting = 5,
        kUnmountable = 6,
        kRemoved = 7,
        kBadRemoval = 8,
    };

    virtual ~VolumeBase();

    const std::string& getId() const { return mId; }
    const std::string& getDiskId() const { return mDiskId; }
    const std::string& getPartGuid() const { return mPartGuid; }
    Type getType() const { return mType; }
    int getMountFlags() const { return mMountFlags; }
    int getMountUserId() const { return mMountUserId; }
    State getState() const { return mState; }
    const std::string& getPath() const { return mPath; }
    const std::string& getInternalPath() const { return mInternalPath; }

    status_t setDiskId(const std::string& diskId);
    status_t setPartGuid(const std::string& partGuid);
    status_t setMountFlags(int mountFlags);
    status_t setMountUserId(int mountUserId);
    status_t setSilent(bool silent);

    void addVolume(const std::shared_ptr<VolumeBase>& volume);
    void removeVolume(const std::shared_ptr<VolumeBase>& volume);
    std::shared_ptr<VolumeBase> findVolume(const std::string& id);

    status_t create();
    status_t destroy();
    status_t mount();
    status_t unmount();
    status_t format(const std::string& fsType);

protected:
    explicit VolumeBase(Type type);

    virtual status_t doCreate();
    virtual void doPostMount();
    virtual status_t doDestroy();
    virtual status_t doMount() = 0;
    virtual status_t doUnmount() = 0;
    virtual status_t doFormat(const std::string& fsType);

    status_t setId(const std::string& id);
    status_t setPath(const std::string& path);
    status_t setInternalPath(const std::string& internalPath);

    std::shared_ptr<android::os::IVoldListener> getListener() const;

private:
    std::string mId;
    std::string mDiskId;
    std::string mPartGuid;
    Type mType;
    int mMountFlags;
    int mMountUserId;
    bool mCreated;
    State mState;
    std::string mPath;
    std::string mInternalPath;
    bool mSilent;

    /* Stacked volumes (e.g. EmulatedVolume stacked atop PrivateVolume) */
    std::list<std::shared_ptr<VolumeBase>> mVolumes;

    void setState(State state);
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_VOLUME_BASE_H
