/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * applications/vold/VoldNativeService.h — AOSP system/vold/VoldNativeService.h
 */

#ifndef ANDROID_VOLD_VOLD_NATIVE_SERVICE_H
#define ANDROID_VOLD_VOLD_NATIVE_SERVICE_H

#include <android/os/IVold.h>
#include <binder/Status.h>

namespace android {
namespace vold {

class VoldNativeService : public os::BnVold {
public:
    static status_t start();
    static VoldNativeService* Instance();

    binder::Status setListener(const std::shared_ptr<android::os::IVoldListener>& listener);

    binder::Status monitor();
    binder::Status reset();
    binder::Status shutdown();

    binder::Status onUserAdded(int32_t userId, int32_t userSerial);
    binder::Status onUserRemoved(int32_t userId);
    binder::Status onUserStarted(int32_t userId);
    binder::Status onUserStopped(int32_t userId);

    binder::Status partition(const std::string& diskId, int32_t partitionType, int32_t ratio);
    binder::Status partitionFs(const std::string& diskId, const std::string& mode);
    binder::Status forgetPartition(const std::string& partGuid, const std::string& fsUuid);

    binder::Status mount(const std::string& volId, int32_t mountFlags, int32_t mountUserId);
    binder::Status unmount(const std::string& volId);
    binder::Status format(const std::string& volId, const std::string& fsType);

    void joinThreadPool();

private:
    VoldNativeService();
    static VoldNativeService* sInstance;

    int mBinderFd;
    void handleBinderTransaction(struct binder_ipc_msg* msg);
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_VOLD_NATIVE_SERVICE_H
