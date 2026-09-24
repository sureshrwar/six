/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 *
 * applications/vold/Utils.h — AOSP system/vold Utils interface.
 */

#ifndef ANDROID_VOLD_UTILS_H
#define ANDROID_VOLD_UTILS_H

#include "compat/six_cxx_compat.h"

namespace android {
namespace vold {

static constexpr uid_t AID_ROOT = 0;
static constexpr gid_t AID_MEDIA_RW = 1023;

status_t CreateDeviceNode(const std::string& path, dev_t dev);
status_t DestroyDeviceNode(const std::string& path);

status_t PrepareDir(const std::string& path, mode_t mode, uid_t uid, gid_t gid);
status_t ForceUnmount(const std::string& path);

status_t ForkExecvp(const std::vector<std::string>& args,
                    std::vector<std::string>* output = nullptr);

status_t ReadMetadataUntrusted(const std::string& path, std::string* fsType,
                               std::string* fsUuid, std::string* fsLabel);

status_t SetupDmCryptDevice(const std::string& dmName, const std::string& blkDevPath,
                            const std::string& keyHex, std::string* outDmDevPath);
status_t TeardownDmCryptDevice(const std::string& dmName);

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_UTILS_H
