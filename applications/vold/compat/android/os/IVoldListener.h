/*
 * applications/vold/compat/android/os/IVoldListener.h
 *
 * AOSP AIDL-generated android::os::IVoldListener interface & Binder proxy
 * to StorageManagerService ("mount" [android.os.storage.IStorageManager]).
 */

#ifndef _ANDROID_OS_IVOLDLISTENER_H
#define _ANDROID_OS_IVOLDLISTENER_H

#include "../../binder/Status.h"

#define IVOLD_LISTENER_ON_DISK_CREATED          101
#define IVOLD_LISTENER_ON_VOLUME_CREATED        102
#define IVOLD_LISTENER_ON_VOLUME_STATE_CHANGED  103
#define IVOLD_LISTENER_ON_DISK_DESTROYED        104
#define IVOLD_LISTENER_ON_VOLUME_DESTROYED      104

namespace android {
namespace os {

class IVoldListener {
public:
    explicit IVoldListener(int binderFd = -1) : mBinderFd(binderFd) {}
    virtual ~IVoldListener() {}

    void setBinderFd(int fd) { mBinderFd = fd; }

    binder::Status onDiskCreated(const std::string& diskId, int32_t flags) {
        mLastDiskId = diskId;
        mLastDiskFlags = flags;
        return binder::Status::ok();
    }

    binder::Status onDiskScanned(const std::string& /*diskId*/) {
        return binder::Status::ok();
    }

    binder::Status onDiskMetadataChanged(const std::string& diskId, uint64_t /*sizeBytes*/,
                                         const std::string& label, const std::string& /*sysPath*/) {
        char payload[256];
        snprintf(payload, sizeof(payload), "%s|%s|%s",
                 diskId.c_str(),
                 label.empty() ? "SanDisk Ultra USB 3.0" : label.c_str(),
                 (mLastDiskFlags & 8) ? "USB" : "SD");
        sendToStorageManager(IVOLD_LISTENER_ON_DISK_CREATED, payload);
        return binder::Status::ok();
    }

    binder::Status onDiskDestroyed(const std::string& diskId) {
        sendToStorageManager(IVOLD_LISTENER_ON_DISK_DESTROYED, diskId.c_str());
        return binder::Status::ok();
    }

    binder::Status onVolumeCreated(const std::string& volId, int32_t type,
                                   const std::string& diskId, const std::string& /*partGuid*/,
                                   int32_t /*userId*/) {
        mLastVolId = volId;
        mLastVolType = type;
        mLastVolDiskId = diskId;
        return binder::Status::ok();
    }

    binder::Status onVolumeMetadataChanged(const std::string& volId, const std::string& fsType,
                                           const std::string& fsUuid, const std::string& fsLabel) {
        mLastVolId = volId;
        mLastFsType = fsType;
        mLastFsUuid = fsUuid;
        mLastFsLabel = fsLabel;
        return binder::Status::ok();
    }

    binder::Status onVolumePathChanged(const std::string& volId, const std::string& path) {
        mLastVolId = volId;
        mLastPath = path;
        return binder::Status::ok();
    }

    binder::Status onVolumeInternalPathChanged(const std::string& /*volId*/,
                                               const std::string& /*internalPath*/) {
        return binder::Status::ok();
    }

    binder::Status onVolumeStateChanged(const std::string& volId, int32_t state) {
        /* Skip EmulatedVolume internal state transitions unless external volume */
        if (volId.find("emulated") == 0) return binder::Status::ok();

        const char* stateStr = "UNMOUNTED";
        if (state == 1) stateStr = "CHECKING";
        else if (state == 2) stateStr = "MOUNTED";
        else if (state == 3) stateStr = "MOUNTED_RO";
        else if (state == 4) stateStr = "FORMATTING";
        else if (state == 5) stateStr = "EJECTING";
        else if (state == 6) stateStr = "UNMOUNTABLE";
        else if (state == 7) stateStr = "REMOVED";
        else if (state == 8) stateStr = "BAD_REMOVAL";

        std::string volTypeStr = "PUBLIC";
        if (volId.find("private:") == 0) {
            volTypeStr = "PRIVATE";
        } else if (mLastFsType == "ntfs") {
            volTypeStr = "PUBLIC(NTFS)";
        } else if (mLastFsType == "ext4") {
            volTypeStr = "PUBLIC(EXT4)";
        } else if (mLastFsType == "ext2") {
            volTypeStr = "PUBLIC(EXT2)";
        }

        std::string mountPath = (state == 2) ? mLastPath : "none";
        if (mountPath.empty()) mountPath = "none";

        char payload[256];
        snprintf(payload, sizeof(payload), "%s|%s|%s|%s|%s|%s",
                 volId.c_str(),
                 volTypeStr.c_str(),
                 stateStr,
                 mountPath.c_str(),
                 mLastFsLabel.empty() ? "USB_DRIVE" : mLastFsLabel.c_str(),
                 mLastFsUuid.empty() ? "0000-0000" : mLastFsUuid.c_str());
        sendToStorageManager(IVOLD_LISTENER_ON_VOLUME_STATE_CHANGED, payload);
        return binder::Status::ok();
    }

    binder::Status onVolumeDestroyed(const std::string& volId) {
        if (volId.find("emulated") == 0) return binder::Status::ok();
        sendToStorageManager(IVOLD_LISTENER_ON_VOLUME_DESTROYED, volId.c_str());
        return binder::Status::ok();
    }

private:
    int mBinderFd;
    std::string mLastDiskId;
    int32_t mLastDiskFlags = 0;
    std::string mLastVolId;
    int32_t mLastVolType = 0;
    std::string mLastVolDiskId;
    std::string mLastFsType;
    std::string mLastFsUuid;
    std::string mLastFsLabel;
    std::string mLastPath;

    void sendToStorageManager(int code, const char* payload) {
        if (mBinderFd < 0) return;
        struct binder_service_info sinfo;
        memset(&sinfo, 0, sizeof(sinfo));
        strcpy(sinfo.name, "mount");
        if (ioctl(mBinderFd, BINDER_IOC_LOOKUP_SVC, &sinfo) < 0 || sinfo.handle <= 0)
            return;

        struct binder_ipc_msg msg;
        memset(&msg, 0, sizeof(msg));
        msg.target_handle = sinfo.handle;
        msg.code = code;
        msg.flags = TF_ONE_WAY;
        strcpy(msg.interface_token, "android.os.IVoldListener");
        strncpy(msg.data, payload, BINDER_MAX_DATA_SIZE - 1);
        msg.data_size = strlen(msg.data) + 1;
        ioctl(mBinderFd, BINDER_IOC_TRANSACT, &msg);
    }
};

} // namespace os
} // namespace android

#endif /* _ANDROID_OS_IVOLDLISTENER_H */
