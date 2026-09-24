/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/EmulatedVolume.cpp — AOSP system/vold/model/EmulatedVolume.cpp
 */

#include "EmulatedVolume.h"
#include "../Utils.h"
#include <android-base/logging.h>
#include <android-base/stringprintf.h>

using android::base::StringPrintf;

namespace android {
namespace vold {

EmulatedVolume::EmulatedVolume(const std::string& rawPath)
    : VolumeBase(Type::kEmulated), mRawPath(rawPath), mLabel("emulated") {
    setId("emulated;0");
}

EmulatedVolume::EmulatedVolume(const std::string& rawPath, dev_t device,
                               const std::string& fsUuid)
    : VolumeBase(Type::kEmulated), mRawPath(rawPath), mLabel(fsUuid) {
    setId(StringPrintf("emulated:%u,%u", major(device), minor(device)));
}

EmulatedVolume::~EmulatedVolume() {}

status_t EmulatedVolume::doMount() {
    PrepareDir(mRawPath, 0770, AID_ROOT, AID_MEDIA_RW);
    std::string userZeroPath = mRawPath + "/0";
    PrepareDir(userZeroPath, 0770, AID_ROOT, AID_MEDIA_RW);
    setInternalPath(mRawPath);
    setPath(userZeroPath);
    LOG(INFO) << getId() << " mounted emulated storage at " << userZeroPath;
    return OK;
}

status_t EmulatedVolume::doUnmount() {
    return OK;
}

}  // namespace vold
}  // namespace android
