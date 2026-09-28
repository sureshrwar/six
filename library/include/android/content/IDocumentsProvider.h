/*
 * library/include/android/content/IDocumentsProvider.h
 *
 * Android Storage Access Framework (SAF) DocumentsProvider / ExternalStorageProvider
 * Binder interface (service "externalstorage" [android.content.IDocumentsProvider],
 * authority "com.android.externalstorage.documents").
 */

#ifndef _ANDROID_CONTENT_IDOCUMENTSPROVIDER_H
#define _ANDROID_CONTENT_IDOCUMENTSPROVIDER_H

#include <linux/binder.h>

#define IDOCUMENTSPROVIDER_SERVICE_NAME		"externalstorage"
#define IDOCUMENTSPROVIDER_DESCRIPTOR		"android.content.IDocumentsProvider"
#define EXTERNALSTORAGE_AUTHORITY		"com.android.externalstorage.documents"

/* IDocumentsProvider Binder transaction codes */
#define IESP_QUERY_ROOTS			1
#define IESP_QUERY_DOCUMENT			2
#define IESP_QUERY_CHILD_DOCUMENTS		3
#define IESP_CREATE_DOCUMENT			4
#define IESP_DELETE_DOCUMENT			5
#define IESP_RENAME_DOCUMENT			6
#define IESP_IS_CHILD_DOCUMENT			7

/* DocumentsContract.Root flag constants */
#define ROOT_FLAG_SUPPORTS_CREATE		0x01
#define ROOT_FLAG_LOCAL_ONLY			0x02
#define ROOT_FLAG_SUPPORTS_RECENTS		0x04
#define ROOT_FLAG_SUPPORTS_IS_CHILD		0x10

/* DocumentsContract.Document flag constants */
#define DOCUMENT_FLAG_SUPPORTS_WRITE		0x02
#define DOCUMENT_FLAG_SUPPORTS_DELETE		0x04
#define DOCUMENT_FLAG_DIR_SUPPORTS_CREATE	0x08
#define DOCUMENT_FLAG_SUPPORTS_RENAME		0x40

#endif /* _ANDROID_CONTENT_IDOCUMENTSPROVIDER_H */
