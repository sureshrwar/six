/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/EmulatedVolume.h — AOSP system/vold/model/EmulatedVolume.h
 */

#ifndef ANDROID_VOLD_EMULATED_VOLUME_H
#define ANDROID_VOLD_EMULATED_VOLUME_H

#include "VolumeBase.h"

namespace android {
namespace vold {

class EmulatedVolume : public VolumeBase {
public:
    explicit EmulatedVolume(const std::string& rawPath);
    EmulatedVolume(const std::string& rawPath, dev_t device, const std::string& fsUuid);
    virtual ~EmulatedVolume();

protected:
    status_t doMount() override;
    status_t doUnmount() override;

private:
    std::string mRawPath;
    std::string mLabel;
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_EMULATED_VOLUME_H
