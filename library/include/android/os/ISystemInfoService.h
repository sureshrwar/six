/*
 * library/include/android/os/ISystemInfoService.h
 *
 * SIX SystemInfo Binder interface (service "sysinfo" [six.os.ISystemInfoService]).
 */

#ifndef _ANDROID_OS_ISYSTEMINFOSERVICE_H
#define _ANDROID_OS_ISYSTEMINFOSERVICE_H

#include <linux/binder.h>

#define ISYSTEMINFO_SERVICE_NAME	"sysinfo"
#define ISYSTEMINFO_DESCRIPTOR		"six.os.ISystemInfoService"

/* ISystemInfoService Binder transaction codes */
#define ISYSINFO_GET_OS_INFO		1

#endif /* _ANDROID_OS_ISYSTEMINFOSERVICE_H */
