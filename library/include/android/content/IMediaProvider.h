/*
 * library/include/android/content/IMediaProvider.h
 *
 * Android MediaProvider / MediaStore Binder interface
 * (service "media.provider" [android.content.IMediaProvider], authority "media").
 */

#ifndef _ANDROID_CONTENT_IMEDIAPROVIDER_H
#define _ANDROID_CONTENT_IMEDIAPROVIDER_H

#include <linux/binder.h>

#define IMEDIAPROVIDER_SERVICE_NAME	"media.provider"
#define IMEDIAPROVIDER_DESCRIPTOR	"android.content.IMediaProvider"
#define IMEDIAPROVIDER_AUTHORITY	"media"

/* IMediaProvider Binder transaction codes */
#define IMP_QUERY			1
#define IMP_INSERT			2
#define IMP_DELETE			3
#define IMP_SCAN			4
#define IMP_STATUS			5
#define IMP_MOUNT_VOLUME		10
#define IMP_UNMOUNT_VOLUME		11

/* MediaStore media_type constants */
#define MEDIA_TYPE_NONE			0
#define MEDIA_TYPE_IMAGE		1
#define MEDIA_TYPE_AUDIO		2
#define MEDIA_TYPE_VIDEO		3
#define MEDIA_TYPE_DOCUMENT		4

#endif /* _ANDROID_CONTENT_IMEDIAPROVIDER_H */
