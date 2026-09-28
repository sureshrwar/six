/*
 * library/include/android/os/IFwupd.h
 *
 * Android Desktop Vendor Firmware Update Service AIDL/Binder Interface
 * (service "fwupd" [org.freedesktop.fwupd.IFwupd], domain u:r:fwupd:s0).
 */

#ifndef _ANDROID_OS_IFWUPD_H
#define _ANDROID_OS_IFWUPD_H

#include <linux/binder.h>

#define IFWUPD_SERVICE_NAME	"fwupd"
#define IFWUPD_DESCRIPTOR	"org.freedesktop.fwupd.IFwupd"

/* IFwupd Binder transaction codes */
#define IFWUPD_GET_PLUGINS	1
#define IFWUPD_GET_DEVICES	2
#define IFWUPD_REFRESH		3
#define IFWUPD_GET_UPDATES	4
#define IFWUPD_UPDATE		5
#define IFWUPD_INSTALL		6
#define IFWUPD_ACTIVATE		7
#define IFWUPD_VERIFY		8
#define IFWUPD_EXAMINE		9
#define IFWUPD_GET_HISTORY	10
#define IFWUPD_CLEAR_HISTORY	11
#define IFWUPD_EXEC_CMD		12

#endif /* _ANDROID_OS_IFWUPD_H */
