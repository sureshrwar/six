/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/PrivateVolume.h — AOSP system/vold/model/PrivateVolume.h
 */

#ifndef ANDROID_VOLD_PRIVATE_VOLUME_H
#define ANDROID_VOLD_PRIVATE_VOLUME_H

#include "VolumeBase.h"

namespace android {
namespace vold {

class PrivateVolume : public VolumeBase {
public:
    PrivateVolume(dev_t device, const std::string& keyRaw);
    virtual ~PrivateVolume();

    const std::string& getFsUuid() const { return mFsUuid; }

protected:
    status_t doCreate() override;
    status_t doDestroy() override;
    status_t doMount() override;
    void doPostMount() override;
    status_t doUnmount() override;
    status_t doFormat(const std::string& fsType) override;

private:
    dev_t mDevice;
    std::string mRawDevPath;
    std::string mDmDevPath;
    std::string mKeyRaw;
    std::string mPath;
    std::string mFsType;
    std::string mFsUuid;
    std::string mFsLabel;
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_PRIVATE_VOLUME_H
