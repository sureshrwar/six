/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 *
 * applications/vold/main.cpp — AOSP system/vold/main.cpp entry point.
 */

#include "VolumeManager.h"
#include "NetlinkManager.h"
#include "VoldNativeService.h"

#include <android-base/logging.h>
#include <android-base/properties.h>

using android::vold::NetlinkManager;
using android::vold::VoldNativeService;
using android::vold::VolumeManager;

static int process_config(VolumeManager* vm) {
    return vm->loadFstabConfig("/etc/fstab");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    LOG(INFO) << "Vold 3.0 (the awakening) firing up";

    VolumeManager* vm;
    NetlinkManager* nm;

    if (!(vm = VolumeManager::Instance())) {
        LOG(ERROR) << "Unable to create VolumeManager";
        exit(1);
    }

    if (!(nm = NetlinkManager::Instance())) {
        LOG(ERROR) << "Unable to create NetlinkManager";
        exit(1);
    }

    if (vm->start()) {
        PLOG(ERROR) << "Unable to start VolumeManager";
        exit(1);
    }

    if (process_config(vm) != 0) {
        PLOG(WARNING) << "Error reading configuration (/etc/fstab)... continuing anyways";
    }

    if (VoldNativeService::start() != android::OK) {
        LOG(ERROR) << "Unable to start VoldNativeService";
        exit(1);
    }

    if (nm->start()) {
        PLOG(ERROR) << "Unable to start NetlinkManager";
        exit(1);
    }

    VoldNativeService::Instance()->joinThreadPool();
    return 0;
}
