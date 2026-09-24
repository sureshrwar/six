/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * applications/vold/VoldNativeService.cpp — AOSP system/vold/VoldNativeService.cpp
 */

#include "VoldNativeService.h"
#include "VolumeManager.h"
#include "NetlinkManager.h"

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

using android::base::StringPrintf;

namespace android {
namespace vold {

namespace {

static binder::Status error(const std::string& msg) {
    LOG(ERROR) << msg;
    return binder::Status::fromServiceSpecificError(-1, msg.c_str());
}

static binder::Status translate(int status) {
    if (status == 0) {
        return binder::Status::ok();
    }
    return binder::Status::fromServiceSpecificError(status, "vold status_t error");
}

}  // namespace

VoldNativeService* VoldNativeService::sInstance = nullptr;

VoldNativeService::VoldNativeService() : mBinderFd(-1) {}

VoldNativeService* VoldNativeService::Instance() {
    if (!sInstance) {
        sInstance = new VoldNativeService();
    }
    return sInstance;
}

status_t VoldNativeService::start() {
    VoldNativeService* svc = Instance();
    svc->mBinderFd = open("/dev/binder", O_RDWR);
    if (svc->mBinderFd < 0) {
        PLOG(ERROR) << "VoldNativeService: unable to open /dev/binder";
        return -errno;
    }

    struct binder_service_info sinfo;
    memset(&sinfo, 0, sizeof(sinfo));
    strcpy(sinfo.name, "vold");
    strcpy(sinfo.descriptor, "android.os.IVold");
    if (ioctl(svc->mBinderFd, BINDER_IOC_REGISTER_SVC, &sinfo) < 0) {
        PLOG(ERROR) << "VoldNativeService: failed to register 'vold' [android.os.IVold]";
        close(svc->mBinderFd);
        svc->mBinderFd = -1;
        return -errno;
    }

    auto listener = std::make_shared<android::os::IVoldListener>(svc->mBinderFd);
    svc->setListener(listener);

    LOG(INFO) << "VoldNativeService registered 'vold' [android.os.IVold] (handle="
              << sinfo.handle << ")";
    return OK;
}

binder::Status VoldNativeService::setListener(
    const std::shared_ptr<android::os::IVoldListener>& listener) {
    VolumeManager::Instance()->setListener(listener);
    return binder::Status::ok();
}

binder::Status VoldNativeService::monitor() {
    return binder::Status::ok();
}

binder::Status VoldNativeService::reset() {
    return translate(VolumeManager::Instance()->reset());
}

binder::Status VoldNativeService::shutdown() {
    return translate(VolumeManager::Instance()->unmountAll());
}

binder::Status VoldNativeService::onUserAdded(int32_t /*userId*/, int32_t /*userSerial*/) {
    return binder::Status::ok();
}

binder::Status VoldNativeService::onUserRemoved(int32_t /*userId*/) {
    return binder::Status::ok();
}

binder::Status VoldNativeService::onUserStarted(int32_t /*userId*/) {
    return binder::Status::ok();
}

binder::Status VoldNativeService::onUserStopped(int32_t /*userId*/) {
    return binder::Status::ok();
}

binder::Status VoldNativeService::partition(const std::string& diskId, int32_t partitionType,
                                            int32_t ratio) {
    auto disk = VolumeManager::Instance()->findDisk(diskId);
    if (disk == nullptr) {
        return error("Failed to find disk " + diskId);
    }
    switch (partitionType) {
        case PARTITION_TYPE_PUBLIC:
            return translate(disk->partitionPublic());
        case PARTITION_TYPE_PRIVATE:
            return translate(disk->partitionPrivate());
        case PARTITION_TYPE_MIXED:
            return translate(disk->partitionMixed(ratio));
        default:
            return error("Unknown type " + std::to_string(partitionType));
    }
}

binder::Status VoldNativeService::partitionFs(const std::string& diskId, const std::string& mode) {
    auto disk = VolumeManager::Instance()->findDisk(diskId);
    if (disk == nullptr) {
        return error("Failed to find disk " + diskId);
    }
    if (mode == "private") {
        return translate(disk->partitionPrivate());
    }
    return translate(disk->partitionFs(mode));
}

binder::Status VoldNativeService::forgetPartition(const std::string& /*partGuid*/,
                                                  const std::string& /*fsUuid*/) {
    return binder::Status::ok();
}

binder::Status VoldNativeService::mount(const std::string& volId, int32_t mountFlags,
                                        int32_t mountUserId) {
    auto vol = VolumeManager::Instance()->findVolume(volId);
    if (vol == nullptr) {
        return error("Failed to find volume " + volId);
    }
    vol->setMountFlags(mountFlags);
    vol->setMountUserId(mountUserId);
    return translate(vol->mount());
}

binder::Status VoldNativeService::unmount(const std::string& volId) {
    auto vol = VolumeManager::Instance()->findVolume(volId);
    if (vol == nullptr) {
        return error("Failed to find volume " + volId);
    }
    return translate(vol->unmount());
}

binder::Status VoldNativeService::format(const std::string& volId, const std::string& fsType) {
    auto vol = VolumeManager::Instance()->findVolume(volId);
    if (vol == nullptr) {
        return error("Failed to find volume " + volId);
    }
    return translate(vol->format(fsType));
}

void VoldNativeService::handleBinderTransaction(struct binder_ipc_msg* msg) {
    msg->status = 0;
    std::string arg = msg->data;

    switch (msg->code) {
        case IVOLD_MOUNT: {
            std::string volId = arg.empty() ? "public:8,1" : arg;
            if (VolumeManager::Instance()->findVolume(volId) == nullptr &&
                VolumeManager::Instance()->findVolume("private:8,1") != nullptr) {
                volId = "private:8,1";
            }
            binder::Status st = mount(volId, MOUNT_FLAG_VISIBLE_FOR_WRITE, 0);
            if (st.isOk()) {
                auto vol = VolumeManager::Instance()->findVolume(volId);
                snprintf(msg->data, BINDER_MAX_DATA_SIZE,
                         "vold: mounted volume %s at %s",
                         volId.c_str(), vol ? vol->getPath().c_str() : "/mnt/media_rw/usb");
            } else {
                msg->status = -1;
                snprintf(msg->data, BINDER_MAX_DATA_SIZE,
                         "vold: mount(%s) failed", volId.c_str());
            }
            break;
        }
        case IVOLD_UNMOUNT: {
            std::string volId = arg.empty() ? "public:8,1" : arg;
            if (VolumeManager::Instance()->findVolume(volId) == nullptr &&
                VolumeManager::Instance()->findVolume("private:8,1") != nullptr) {
                volId = "private:8,1";
            }
            binder::Status st = unmount(volId);
            if (st.isOk()) {
                snprintf(msg->data, BINDER_MAX_DATA_SIZE,
                         "vold: unmounted volume %s", volId.c_str());
            } else {
                msg->status = -1;
                snprintf(msg->data, BINDER_MAX_DATA_SIZE,
                         "vold: unmount(%s) failed", volId.c_str());
            }
            break;
        }
        case IVOLD_PARTITION:
        case IVOLD_FORMAT: {
            std::string mode = arg.empty() ? "public" : arg;
            binder::Status st = partitionFs("disk:8,0", mode);
            if (st.isOk()) {
                auto disk = VolumeManager::Instance()->findDisk("disk:8,0");
                auto vol = disk ? (disk->findVolume("private:8,1")
                                       ? disk->findVolume("private:8,1")
                                       : disk->findVolume("public:8,1"))
                                : nullptr;
                snprintf(msg->data, BINDER_MAX_DATA_SIZE,
                         "vold: partitioned disk:8,0 as %s -> %s at %s",
                         mode.c_str(),
                         vol ? vol->getId().c_str() : "public:8,1",
                         vol ? vol->getPath().c_str() : "/mnt/media_rw/usb");
            } else {
                msg->status = -1;
                snprintf(msg->data, BINDER_MAX_DATA_SIZE,
                         "vold: partition(disk:8,0, %s) failed", mode.c_str());
            }
            break;
        }
        case IVOLD_RESET: {
            reset();
            snprintf(msg->data, BINDER_MAX_DATA_SIZE, "vold: VolumeManager reset");
            break;
        }
        default: {
            snprintf(msg->data, BINDER_MAX_DATA_SIZE, "vold: unknown IVold code %u", msg->code);
            break;
        }
    }
    msg->data_size = strlen(msg->data) + 1;
}

void VoldNativeService::joinThreadPool() {
    NetlinkHandler* nlHandler = NetlinkManager::Instance()->getHandler();

    while (true) {
        struct binder_wait_event ev;
        memset(&ev, 0, sizeof(ev));
        if (mBinderFd >= 0 && ioctl(mBinderFd, BINDER_IOC_WAIT_EVENT, &ev) == 0) {
            if (ev.event_type == BINDER_WAIT_UEVENT && nlHandler) {
                nlHandler->dispatchUevent(&ev.uevent);
                continue;
            }
            if (ev.event_type == BINDER_WAIT_TXN) {
                handleBinderTransaction(&ev.txn);
                if (!(ev.txn.flags & TF_ONE_WAY)) {
                    ioctl(mBinderFd, BINDER_IOC_REPLY, &ev.txn);
                }
                continue;
            }
        }
        if (nlHandler) {
            nlHandler->pollOnce();
        }
    }
}

}  // namespace vold
}  // namespace android
