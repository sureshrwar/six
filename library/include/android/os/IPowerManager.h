/*
 * library/include/android/os/IPowerManager.h
 *
 * Android PowerManager Binder interface (service "power" [android.os.IPowerManager]).
 */

#ifndef _ANDROID_OS_IPOWERMANAGER_H
#define _ANDROID_OS_IPOWERMANAGER_H

#include <linux/binder.h>

#define IPOWERMANAGER_SERVICE_NAME	"power"
#define IPOWERMANAGER_DESCRIPTOR	"android.os.IPowerManager"

/* IPowerManager Binder transaction codes */
#define IPM_GET_STATUS			1
#define IPM_ACQUIRE_WAKE_LOCK		2
#define IPM_RELEASE_WAKE_LOCK		3
#define IPM_GO_TO_SLEEP			4
#define IPM_WAKE_UP			5

#endif /* _ANDROID_OS_IPOWERMANAGER_H */
