/*
 * library/include/android/os/IServiceManager.h
 *
 * Android Binder Context Manager interface (android.os.IServiceManager, handle 0).
 */

#ifndef _ANDROID_OS_ISERVICEMANAGER_H
#define _ANDROID_OS_ISERVICEMANAGER_H

#include <linux/binder.h>

#define ISERVICEMANAGER_DESCRIPTOR	"android.os.IServiceManager"
#define ISERVICEMANAGER_HANDLE		BINDER_CONTEXT_MGR_HANDLE

/* IServiceManager transaction codes (also exposed in <linux/binder.h>) */
#ifndef SVC_MGR_GET_SERVICE
#define SVC_MGR_GET_SERVICE		1
#define SVC_MGR_CHECK_SERVICE		2
#define SVC_MGR_ADD_SERVICE		3
#define SVC_MGR_LIST_SERVICES		4
#endif

#endif /* _ANDROID_OS_ISERVICEMANAGER_H */
