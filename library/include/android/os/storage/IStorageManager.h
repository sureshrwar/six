/*
 * library/include/android/os/storage/IStorageManager.h
 *
 * Android StorageManagerService Binder interface
 * (service "mount" [android.os.storage.IStorageManager]).
 */

#ifndef _ANDROID_OS_STORAGE_ISTORAGEMANAGER_H
#define _ANDROID_OS_STORAGE_ISTORAGEMANAGER_H

#include <linux/binder.h>

#define ISTORAGEMANAGER_SERVICE_NAME	"mount"
#define ISTORAGEMANAGER_DESCRIPTOR	"android.os.storage.IStorageManager"

/* IStorageManager Binder transaction codes */
#define ISM_LIST_DISKS			1
#define ISM_LIST_VOLUMES		2
#define ISM_MOUNT			3
#define ISM_UNMOUNT			4
#define ISM_PARTITION			5

#endif /* _ANDROID_OS_STORAGE_ISTORAGEMANAGER_H */
