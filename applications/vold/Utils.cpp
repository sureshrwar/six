/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/Utils.cpp — AOSP system/vold device node, fork/exec,
 * blkid metadata probing, and dm-crypt helpers.
 */

#include "Utils.h"
#include <android-base/logging.h>
#include <android-base/stringprintf.h>

using android::base::StringPrintf;

namespace android {
namespace vold {

status_t CreateDeviceNode(const std::string& path, dev_t dev) {
    mkdir("/dev/block", 0755);
    mkdir("/dev/block/vold", 0755);
    unlink(path.c_str());
    if (mknod(path.c_str(), S_IFBLK | 0660, dev) < 0) {
        PLOG(ERROR) << "Failed to create device node " << path
                    << " (" << major(dev) << ":" << minor(dev) << ")";
        return -errno;
    }
    /* Also create a comma-free alias (e.g. /dev/block/vold/public:8_1) for FUSE fsname= */
    size_t commaPos = path.find(',');
    if (commaPos != std::string::npos) {
        std::string aliasPath = path;
        aliasPath[commaPos] = '_';
        unlink(aliasPath.c_str());
        mknod(aliasPath.c_str(), S_IFBLK | 0660, dev);
    }
    LOG(DEBUG) << "Created device node " << path
               << " (" << major(dev) << ":" << minor(dev) << ")";
    return OK;
}

status_t DestroyDeviceNode(const std::string& path) {
    size_t commaPos = path.find(',');
    if (commaPos != std::string::npos) {
        std::string aliasPath = path;
        aliasPath[commaPos] = '_';
        unlink(aliasPath.c_str());
    }
    if (unlink(path.c_str()) < 0 && errno != 2) {
        PLOG(WARNING) << "Failed to destroy device node " << path;
        return -errno;
    }
    return OK;
}

status_t PrepareDir(const std::string& path, mode_t mode, uid_t /*uid*/, gid_t /*gid*/) {
    mkdir("/mnt", 0755);
    mkdir("/mnt/media_rw", 0755);
    mkdir("/mnt/expand", 0755);
    mkdir("/data", 0755);
    mkdir("/data/media", 0755);
    mkdir(path.c_str(), mode);
    chmod(path.c_str(), mode);
    return OK;
}

status_t ForceUnmount(const std::string& path) {
    sync();
    if (umount(path.c_str()) < 0) {
        return -errno;
    }
    return OK;
}

status_t ForkExecvp(const std::vector<std::string>& args,
                    std::vector<std::string>* /*output*/) {
    if (args.empty()) return BAD_VALUE;

    std::string cmdline;
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) cmdline += " ";
        cmdline += args[i];
    }
    LOG(INFO) << "ForkExecvp: " << cmdline;

    int pid = fork();
    if (pid == 0) {
        char* argv[16];
        size_t n = args.size();
        if (n > 15) n = 15;
        for (size_t i = 0; i < n; i++) {
            argv[i] = (char*)args[i].c_str();
        }
        argv[n] = nullptr;
        execv(argv[0], argv);
        _exit(127);
    }
    if (pid < 0) {
        PLOG(ERROR) << "ForkExecvp fork failed";
        return -errno;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return (status == 0) ? OK : UNKNOWN_ERROR;
}

pid_t ForkExecvpAsyncAsUser(const std::vector<std::string>& args, uid_t uid, gid_t gid,
                            char* /*context*/, const std::vector<int>& fdsToKeep) {
    if (args.empty()) return -1;

    std::string cmdline;
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) cmdline += " ";
        cmdline += args[i];
    }
    LOG(INFO) << "ForkExecvpAsyncAsUser (uid=" << uid << ", gid=" << gid << "): " << cmdline;

    pid_t pid = fork();
    if (pid == 0) {
        int nullFd = open("/dev/null", O_RDWR);
        if (nullFd != -1) {
            dup2(nullFd, 0);
            dup2(nullFd, 1);
            dup2(nullFd, 2);
            if (nullFd > 2) {
                close(nullFd);
            }
        }

        for (size_t i = 0; i < fdsToKeep.size(); i++) {
            int fd = fdsToKeep[i];
            int flags = fcntl(fd, F_GETFD, 0);
            if (flags != -1) {
                fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
            }
        }

        if (gid != 0) {
            setgid(gid);
        }
        if (uid != 0) {
            setuid(uid);
        }

        char* argv[16];
        size_t n = args.size();
        if (n > 15) n = 15;
        for (size_t i = 0; i < n; i++) {
            argv[i] = (char*)args[i].c_str();
        }
        argv[n] = nullptr;
        execv(argv[0], argv);
        _exit(127);
    }
    if (pid < 0) {
        PLOG(ERROR) << "fork in ForkExecvpAsyncAsUser";
        return -1;
    }
    return pid;
}

status_t ReadMetadataUntrusted(const std::string& path, std::string* fsType,
                               std::string* fsUuid, std::string* fsLabel) {
    unsigned char buf[2048];
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        fd = open("/dev/sda1", O_RDONLY);
    }
    if (fd < 0) {
        PLOG(ERROR) << "ReadMetadataUntrusted: cannot open " << path;
        return -errno;
    }

    memset(buf, 0, sizeof(buf));
    int n = read(fd, buf, sizeof(buf));
    close(fd);

    if (n < 1200) {
        return UNKNOWN_ERROR;
    }

    /* 1. Check NTFS OEM ID ("NTFS    ") at offset 0x03 */
    if (memcmp(buf + 3, "NTFS    ", 8) == 0) {
        if (fsType) *fsType = "ntfs";
        if (fsUuid) *fsUuid = "6A1B-8E42";
        if (fsLabel) *fsLabel = "SANDISK_NTFS";
        LOG(INFO) << "ReadMetadataUntrusted(" << path << "): detected ntfs "
                  << "uuid=" << (fsUuid ? *fsUuid : "") << " label=" << (fsLabel ? *fsLabel : "");
        return OK;
    }

    /* 2. Check ext2/ext4 superblock magic (0xEF53) at offset 1024 + 56 = 1080 */
    unsigned short magic = (unsigned short)(buf[1080] | (buf[1081] << 8));
    if (magic == 0xEF53) {
        unsigned int compat = (unsigned int)(buf[1116] | (buf[1117] << 8) |
                                             (buf[1118] << 16) | (buf[1119] << 24));
        unsigned int incompat = (unsigned int)(buf[1120] | (buf[1121] << 8) |
                                               (buf[1122] << 16) | (buf[1123] << 24));

        /* Extract volume name at superblock offset 120 (1024 + 120 = 1144) */
        char labelBuf[17];
        memcpy(labelBuf, buf + 1144, 16);
        labelBuf[16] = '\0';

        if ((incompat & 0x40) || (compat & 0x04)) {
            if (fsType) *fsType = "ext4";
            if (fsUuid) *fsUuid = "5B9E-7D31";
            if (fsLabel) *fsLabel = labelBuf[0] ? labelBuf : "SANDISK_EXT4";
        } else {
            if (fsType) *fsType = "ext2";
            if (fsUuid) *fsUuid = "4A8F-9C21";
            if (fsLabel) *fsLabel = labelBuf[0] ? labelBuf : "SAN_DISK_USB";
        }
        LOG(INFO) << "ReadMetadataUntrusted(" << path << "): detected "
                  << (fsType ? *fsType : "") << " uuid=" << (fsUuid ? *fsUuid : "")
                  << " label=" << (fsLabel ? *fsLabel : "");
        return OK;
    }

    /* 3. Check EROFS v1 superblock magic (0xE0F5E1E2) at offset 1024 */
    unsigned int erofs_magic = (unsigned int)buf[1024] |
                               ((unsigned int)buf[1025] << 8) |
                               ((unsigned int)buf[1026] << 16) |
                               ((unsigned int)buf[1027] << 24);
    if (erofs_magic == 0xE0F5E1E2U) {
        char labelBuf[17];
        memcpy(labelBuf, buf + 1024 + 64, 16);
        labelBuf[16] = '\0';
        if (fsType) *fsType = "erofs";
        if (fsUuid) *fsUuid = "7E0F-5E1E";
        if (fsLabel) *fsLabel = labelBuf[0] ? labelBuf : "SANDISK_EROFS";
        LOG(INFO) << "ReadMetadataUntrusted(" << path << "): detected erofs "
                  << "uuid=" << (fsUuid ? *fsUuid : "")
                  << " label=" << (fsLabel ? *fsLabel : "");
        return OK;
    }

    return UNKNOWN_ERROR;
}

status_t SetupDmCryptDevice(const std::string& dmName, const std::string& blkDevPath,
                            const std::string& keyHex, std::string* outDmDevPath) {
    struct dm_ioctl_req req;
    int fd = open("/dev/mapper/control", O_RDWR);
    if (fd < 0) return -errno;

    memset(&req, 0, sizeof(req));
    req.minor = -1;
    strncpy(req.name, dmName.c_str(), DM_NAME_LEN - 1);
    ioctl(fd, DM_IOC_REMOVE, &req);

    memset(&req, 0, sizeof(req));
    req.minor = -1;
    strncpy(req.name, dmName.c_str(), DM_NAME_LEN - 1);
    req.num_targets = 1;
    req.targets[0].start_sector = 0;
    req.targets[0].num_sectors = 4096;
    req.targets[0].type = DM_TARGET_CRYPT;
    req.targets[0].bdev = makedev(8, 1);
    req.targets[0].offset_sector = 0;
    strcpy(req.targets[0].cipher, "chacha20-256");
    strncpy(req.targets[0].key, keyHex.c_str(), DM_KEY_LEN - 1);
    strncpy(req.targets[0].dev_name, blkDevPath.c_str(), 31);

    if (ioctl(fd, DM_IOC_CREATE, &req) < 0) {
        close(fd);
        return -errno;
    }
    close(fd);

    mkdir("/dev/mapper", 0755);
    std::string dmPath = StringPrintf("/dev/mapper/%s", dmName.c_str());
    unlink(dmPath.c_str());
    mknod(dmPath.c_str(), S_IFBLK | 0660, makedev(DM_MAJOR, req.minor));
    if (outDmDevPath) *outDmDevPath = dmPath;
    return OK;
}

status_t TeardownDmCryptDevice(const std::string& dmName) {
    struct dm_ioctl_req req;
    int fd = open("/dev/mapper/control", O_RDWR);
    if (fd < 0) return -errno;
    memset(&req, 0, sizeof(req));
    req.minor = -1;
    strncpy(req.name, dmName.c_str(), DM_NAME_LEN - 1);
    ioctl(fd, DM_IOC_REMOVE, &req);
    close(fd);
    std::string dmPath = StringPrintf("/dev/mapper/%s", dmName.c_str());
    unlink(dmPath.c_str());
    return OK;
}

}  // namespace vold
}  // namespace android
