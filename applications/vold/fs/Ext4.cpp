/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * applications/vold/fs/Ext4.cpp — AOSP system/vold/fs/Ext4.cpp
 */

#include "Ext4.h"
#include <android-base/logging.h>

namespace android {
namespace vold {
namespace ext4 {

bool IsSupported() {
    return true;
}

status_t Check(const std::string& source, const std::string& /*target*/) {
    LOG(INFO) << "ext4::Check(" << source << ")";
    return OK;
}

status_t Mount(const std::string& source, const std::string& target, bool /*ro*/,
               bool /*remount*/, bool /*executable*/) {
    LOG(INFO) << "ext4::Mount(" << source << " -> " << target << ")";
    if (mount(source.c_str(), target.c_str(), "ext4", 0, 0) < 0) {
        PLOG(ERROR) << "ext4::Mount failed for " << source;
        return -errno;
    }
    return OK;
}

status_t Format(const std::string& source, unsigned int /*numSectors*/,
                const std::string& /*target*/) {
    LOG(INFO) << "ext4::Format(" << source << ")";
    int bfd = open("/dev/binder", O_RDWR);
    if (bfd >= 0) {
        struct binder_uevent_msg uev;
        memset(&uev, 0, sizeof(uev));
        strcpy(uev.action, "prepare");
        strcpy(uev.subsystem, "block");
        strcpy(uev.devpath, "/devices/pci0000:00/usb1/1-1/block/sda/sda1");
        strcpy(uev.devname, "sda1");
        uev.major = 8;
        uev.minor = 1;
        strcpy(uev.fstype, "ext4");
        strcpy(uev.label, "SANDISK_EXT4");
        strcpy(uev.uuid, "5B9E-7D31");
        ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev);
        close(bfd);
    }
    return OK;
}

}  // namespace ext4
}  // namespace vold
}  // namespace android
