/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * applications/vold/VolumeManager.cpp — AOSP system/vold/VolumeManager.cpp
 */

#include "VolumeManager.h"
#include "model/EmulatedVolume.h"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
}

using android::base::StringPrintf;

namespace android {
namespace vold {

static bool glob_match(const char* pat, const char* str) {
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return true;
            while (*str) {
                if (glob_match(pat, str)) return true;
                str++;
            }
            return false;
        } else if (*pat == '?' && *str) {
            pat++;
            str++;
        } else if (*pat == *str) {
            pat++;
            str++;
        } else {
            return false;
        }
    }
    return *str == '\0';
}

bool VolumeManager::DiskSource::matches(const std::string& sysPath) const {
    const char* pat = mSysPattern.c_str();
    const char* str = sysPath.c_str();
    if (glob_match(pat, str)) {
        return true;
    }
    size_t patLen = strlen(pat);
    if (patLen > 0 && pat[patLen - 1] != '*' && strncmp(str, pat, patLen) == 0) {
        return true;
    }
    return false;
}

static int next_token(const char** cursor, char* out, int maxLen) {
    const char* p = *cursor;
    int i = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') {
        *cursor = p;
        return 0;
    }
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && i < maxLen - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    *cursor = p;
    return 1;
}

VolumeManager* VolumeManager::sInstance = nullptr;

VolumeManager* VolumeManager::Instance() {
    if (!sInstance) {
        sInstance = new VolumeManager();
    }
    return sInstance;
}

VolumeManager::VolumeManager() {}

VolumeManager::~VolumeManager() {}

void VolumeManager::addDiskSource(const std::shared_ptr<DiskSource>& diskSource) {
    mDiskSources.push_back(diskSource);
}

int VolumeManager::loadFstabConfig(const char* fstabPath) {
    FILE* fp = fopen(fstabPath ? fstabPath : "/etc/fstab", "r");
    if (!fp) {
        PLOG(WARNING) << "Unable to open " << (fstabPath ? fstabPath : "/etc/fstab");
        return -1;
    }

    mDiskSources.clear();
    bool hasAdoptable = false;
    char line[512];

    while (fgets(line, sizeof(line), fp)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') continue;

        char src[128] = {0}, mnt[128] = {0}, fstype[64] = {0}, mntflags[128] = {0}, fsmgr[256] = {0};
        if (!next_token(&p, src, sizeof(src)) ||
            !next_token(&p, mnt, sizeof(mnt)) ||
            !next_token(&p, fstype, sizeof(fstype)) ||
            !next_token(&p, mntflags, sizeof(mntflags)) ||
            !next_token(&p, fsmgr, sizeof(fsmgr))) {
            continue;
        }

        const char* vmTag = strstr(fsmgr, "voldmanaged=");
        if (!vmTag) continue;

        vmTag += strlen("voldmanaged=");
        char vmVal[128] = {0};
        int idx = 0;
        while (vmTag[idx] && vmTag[idx] != ',' && vmTag[idx] != ' ' &&
               vmTag[idx] != '\t' && vmTag[idx] != '\n' && vmTag[idx] != '\r' &&
               idx < (int)sizeof(vmVal) - 1) {
            vmVal[idx] = vmTag[idx];
            idx++;
        }
        vmVal[idx] = '\0';

        std::string nickname = "usb";
        int partnum = -1;
        char* colon = strchr(vmVal, ':');
        if (colon) {
            *colon = '\0';
            if (vmVal[0]) nickname = vmVal;
            const char* partStr = colon + 1;
            if (strcmp(partStr, "auto") != 0 && partStr[0] != '\0') {
                partnum = atoi(partStr);
            }
        } else if (vmVal[0]) {
            nickname = vmVal;
        }

        int flags = 0;
        if (strstr(fsmgr, "encryptable=") || strstr(fsmgr, "forceencrypt=") ||
            strstr(fsmgr, "fileencryption=")) {
            flags |= Disk::Flags::kAdoptable;
            hasAdoptable = true;
        }
        if (strstr(fsmgr, "noemulatedsd") || strstr(fsmgr, "defaultprimary")) {
            flags |= Disk::Flags::kDefaultPrimary;
        }
        if (strstr(src, "usb") || nickname.find("usb") != std::string::npos) {
            flags |= Disk::Flags::kUsb;
        } else if (strstr(src, "mmc") || strstr(src, "sd") ||
                   nickname.find("sd") != std::string::npos) {
            flags |= Disk::Flags::kSd;
        } else {
            flags |= Disk::Flags::kUsb;
        }

        std::string fsStr = (strcmp(fstype, "auto") == 0) ? "" : fstype;
        std::string optsStr = (strcmp(mntflags, "defaults") == 0) ? "" : mntflags;

        auto diskSource = std::make_shared<DiskSource>(
            src, nickname, partnum, flags, fsStr, optsStr);
        addDiskSource(diskSource);
        LOG(INFO) << "fstab DiskSource registered: pattern=" << src
                  << " label=" << nickname << " part=" << partnum
                  << " flags=0x" << StringPrintf("%x", flags);
    }

    fclose(fp);
    android::base::SetProperty("vold.has_adoptable", hasAdoptable ? "1" : "0");
    return 0;
}

int VolumeManager::start() {
    unmountAll();

    auto vol = std::make_shared<EmulatedVolume>("/data/media");
    vol->setMountUserId(0);
    vol->create();
    vol->mount();
    mInternalEmulatedVolumes.push_back(vol);
    return 0;
}

int VolumeManager::stop() {
    for (auto it = mInternalEmulatedVolumes.begin(); it != mInternalEmulatedVolumes.end(); ++it) {
        auto vol = *it;
        vol->destroy();
    }
    mInternalEmulatedVolumes.clear();
    return 0;
}

void VolumeManager::handleBlockEvent(NetlinkEvent* evt) {
    {
        int wfd = open("/sys/power/wake_lock", 1);
        if (wfd >= 0) {
            write(wfd, "vold.block_event\n", 17);
            close(wfd);
        }
    }
    const char* eventPathStr = evt->findParam("DEVPATH");
    const char* devTypeStr = evt->findParam("DEVTYPE");
    const char* majorStr = evt->findParam("MAJOR");
    const char* minorStr = evt->findParam("MINOR");

    std::string eventPath = eventPathStr ? eventPathStr : "";
    std::string devType = devTypeStr ? devTypeStr : "disk";
    int maj = majorStr ? atoi(majorStr) : 8;
    int min = minorStr ? atoi(minorStr) : 0;
    (void)min;

    /* Disk device in AOSP is always whole-disk minor 0 (e.g. 8:0 for sda) */
    dev_t diskDevice = makedev(maj, 0);

    LOG(DEBUG) << "handleBlockEvent: action=" << static_cast<int>(evt->getAction())
               << " devPath=" << eventPath << " devType=" << devType
               << " disk=" << maj << ":0";

    switch (evt->getAction()) {
        case NetlinkEvent::Action::kAdd: {
            for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
                auto existing = *it;
                if (existing->getDevice() == diskDevice) {
                    existing->destroy();
                    mDisks.erase(it);
                    break;
                }
            }

            bool matched = false;
            for (const auto& source : mDiskSources) {
                if (source->matches(eventPath)) {
                    int flags = source->getFlags();
                    auto disk = std::make_shared<android::vold::Disk>(
                        eventPath, diskDevice, source->getNickname(), flags);
                    mDisks.push_back(disk);
                    disk->create();
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                LOG(WARNING) << "Ignoring block event for non-voldmanaged path " << eventPath;
            }
            break;
        }
        case NetlinkEvent::Action::kChange: {
            for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
                auto disk = *it;
                if (disk->getDevice() == diskDevice) {
                    disk->readMetadata();
                    disk->readPartitions();
                }
            }
            break;
        }
        case NetlinkEvent::Action::kRemove: {
            for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
                auto disk = *it;
                if (disk->getDevice() == diskDevice) {
                    disk->destroy();
                    mDisks.erase(it);
                    break;
                }
            }
            break;
        }
        default: {
            LOG(WARNING) << "Unexpected block event action " << static_cast<int>(evt->getAction());
            break;
        }
    }
    {
        int ufd = open("/sys/power/wake_unlock", 1);
        if (ufd >= 0) {
            write(ufd, "vold.block_event\n", 17);
            close(ufd);
        }
    }
}

std::shared_ptr<Disk> VolumeManager::findDisk(const std::string& id) {
    for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
        auto disk = *it;
        if (disk->getId() == id) {
            return disk;
        }
    }
    return nullptr;
}

std::shared_ptr<VolumeBase> VolumeManager::findVolume(const std::string& id) {
    for (auto it = mInternalEmulatedVolumes.begin(); it != mInternalEmulatedVolumes.end(); ++it) {
        auto vol = *it;
        if (vol->getId() == id) {
            return vol;
        }
    }
    for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
        auto disk = *it;
        auto vol = disk->findVolume(id);
        if (vol != nullptr) {
            return vol;
        }
    }
    return nullptr;
}

int VolumeManager::unmountAll() {
    for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
        auto disk = *it;
        disk->unmountAll();
    }
    return 0;
}

int VolumeManager::reset() {
    loadFstabConfig("/etc/fstab");
    for (auto it = mDisks.begin(); it != mDisks.end(); ++it) {
        auto disk = *it;
        disk->destroy();
        disk->create();
    }
    return 0;
}

}  // namespace vold
}  // namespace android
