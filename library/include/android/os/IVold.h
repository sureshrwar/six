/*
 * library/include/android/os/IVold.h
 *
 * AOSP AIDL android::os::IVold interface constants and C++ base class
 * (service "vold" [android.os.IVold]).
 *
 * Usable from both C (storaged) and C++ (vold).
 */

#ifndef _ANDROID_OS_IVOLD_H
#define _ANDROID_OS_IVOLD_H

#include <linux/binder.h>
#include <android/os/IVoldListener.h>

#define IVOLD_SERVICE_NAME	"vold"
#define IVOLD_DESCRIPTOR	"android.os.IVold"

/* IVold Binder transaction codes */
#define IVOLD_GET_STATUS	1
#define IVOLD_MOUNT		2
#define IVOLD_UNMOUNT		3
#define IVOLD_PARTITION		4
#define IVOLD_FORMAT		5
#define IVOLD_RESET		6
#define IVOLD_MONITOR		7

#ifdef __cplusplus
namespace android {
namespace os {

class IVold {
public:
    enum {
        PARTITION_TYPE_PUBLIC = 0,
        PARTITION_TYPE_PRIVATE = 1,
        PARTITION_TYPE_MIXED = 2,

        MOUNT_FLAG_PRIMARY = 1,
        MOUNT_FLAG_VISIBLE_FOR_READ = 2,
        MOUNT_FLAG_VISIBLE_FOR_WRITE = 4,

        VOLUME_STATE_UNMOUNTED = 0,
        VOLUME_STATE_CHECKING = 1,
        VOLUME_STATE_MOUNTED = 2,
        VOLUME_STATE_MOUNTED_READ_ONLY = 3,
        VOLUME_STATE_FORMATTING = 4,
        VOLUME_STATE_EJECTING = 5,
        VOLUME_STATE_UNMOUNTABLE = 6,
        VOLUME_STATE_REMOVED = 7,
        VOLUME_STATE_BAD_REMOVAL = 8,

        VOLUME_TYPE_PUBLIC = 0,
        VOLUME_TYPE_PRIVATE = 1,
        VOLUME_TYPE_EMULATED = 2,
        VOLUME_TYPE_ASEC = 3,
        VOLUME_TYPE_OBB = 4,
        VOLUME_TYPE_STUB = 5,
    };

    virtual ~IVold() {}
};

class BnVold : public IVold {
public:
    virtual ~BnVold() {}
};

} // namespace os
} // namespace android
#endif /* __cplusplus */

#endif /* _ANDROID_OS_IVOLD_H */
