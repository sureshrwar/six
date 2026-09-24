/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/fs/Ext2.cpp
 */

#include "Ext2.h"
#include <android-base/logging.h>

namespace android {
namespace vold {
namespace ext2 {

bool IsSupported() {
    return true;
}

status_t Check(const std::string& source, const std::string& /*target*/) {
    LOG(INFO) << "ext2::Check(" << source << ")";
    return OK;
}

status_t Mount(const std::string& source, const std::string& target, bool /*ro*/,
               bool /*remount*/, bool /*executable*/) {
    LOG(INFO) << "ext2::Mount(" << source << " -> " << target << ")";
    if (mount(source.c_str(), target.c_str(), "ext2", 0, 0) < 0) {
        PLOG(ERROR) << "ext2::Mount failed for " << source;
        return -errno;
    }
    return OK;
}

status_t Format(const std::string& source) {
    LOG(INFO) << "ext2::Format(" << source << ")";
    if (source.find("mapper") != std::string::npos) {
        std::vector<std::string> cmd;
        cmd.push_back("/bin/mkfs.ext2");
        cmd.push_back(source);
        return ForkExecvp(cmd);
    }
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
        strcpy(uev.fstype, "ext2");
        strcpy(uev.label, "SAN_DISK_USB");
        strcpy(uev.uuid, "4A8F-9C21");
        ioctl(bfd, BINDER_IOC_UEVENT_EMIT, &uev);
        close(bfd);
    }
    return OK;
}

}  // namespace ext2
}  // namespace vold
}  // namespace android
