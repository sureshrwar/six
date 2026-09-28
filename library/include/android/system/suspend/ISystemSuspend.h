/*
 * library/include/android/system/suspend/ISystemSuspend.h
 *
 * Android SystemSuspend Binder interface
 * (service "suspend" [android.system.suspend.ISystemSuspend]).
 */

#ifndef _ANDROID_SYSTEM_SUSPEND_ISYSTEMSUSPEND_H
#define _ANDROID_SYSTEM_SUSPEND_ISYSTEMSUSPEND_H

#include <linux/binder.h>

#define ISYSTEMSUSPEND_SERVICE_NAME	"suspend"
#define ISYSTEMSUSPEND_DESCRIPTOR	"android.system.suspend.ISystemSuspend"

/* ISystemSuspend Binder transaction codes */
#define ISUSPEND_ACQUIRE_WAKE_LOCK	1
#define ISUSPEND_GET_STATUS		2

#endif /* _ANDROID_SYSTEM_SUSPEND_ISYSTEMSUSPEND_H */
