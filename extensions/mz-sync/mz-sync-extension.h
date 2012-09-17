/* :noTabs=true:mode=c:tabSize=4:indentSize=4:folding=explicit:
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/

#ifndef __MZ_SYNC_EXTENSION_H__
#define __MZ_SYNC_EXTENSION_H__
                             
#include <midori/midori.h>

#define MZ_SYNC_VERSION "0.1"
#define MZ_SYNC_USER_AGENT "MZ-SYNC/" MZ_SYNC_VERSION MIDORI_VERSION_SUFFIX
G_BEGIN_DECLS

/* {{{ gobject stuff */

#define MZ_SYNC_TYPE_EXTENSION \
    (mz_sync_extension_get_type ())
#define MZ_SYNC_EXTENSION(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), MZ_SYNC_TYPE_EXTENSION, MzSyncExtension))
#define MZ_SYNC_EXTENSION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), MZ_SYNC_TYPE_EXTENSION, MzSyncExtensionClass))
#define MZ_SYNC_IS_EXTENSION(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MZ_SYNC_TYPE_EXTENSION))
#define MZ_SYNC_IS_EXTENSION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), MZ_SYNC_TYPE_EXTENSION))
#define MZ_SYNC_EXTENSION_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), MZ_SYNC_TYPE_EXTENSION, MySyncExtensionClass))

typedef struct _MzSyncExtension                MzSyncExtension;
typedef struct _MzSyncExtensionClass           MzSyncExtensionClass;

GType
mz_sync_extension_get_type                      (void);

MzSyncExtension*
mz_sync_extension_new                           (void);

/* }}} */

/* {{{ methods */
typedef struct {
	gboolean success;
	int deleted;
	int added;
	int modified;
	GError* error;
} MzSyncStatus;

gboolean
mz_sync_extension_sync (MzSyncExtension* extension);

void
mz_sync_extension_activate_cb (MzSyncExtension* extension);

void
mz_sync_extension_reset (MzSyncExtension* extension);

KatzeArray*
mz_sync_extension_get_roots (MzSyncExtension* extension);
/* }}} */

/* {{{ preferences */
void
mz_sync_extension_install_prefs(MzSyncExtension * self);
    
#define mz_sync_extension_set_user(extension, user) \
    midori_extension_set_string (MIDORI_EXTENSION (extension), "user", \
                       user)
#define mz_sync_extension_get_user(extension) \
    midori_extension_get_string (MIDORI_EXTENSION (extension), "user")

#define mz_sync_extension_set_pass(extension, pass) \
    midori_extension_set_string (MIDORI_EXTENSION (extension), "pass", \
                       pass)
#define mz_sync_extension_get_pass(extension) \
    midori_extension_get_string (MIDORI_EXTENSION (extension), "pass")

#define mz_sync_extension_set_server(extension, server) \
    midori_extension_set_string (MIDORI_EXTENSION (extension), "server", \
                       server)
#define mz_sync_extension_get_server(extension) \
    midori_extension_get_string (MIDORI_EXTENSION (extension), "server")

#define mz_sync_extension_set_key(extension, key) \
    midori_extension_set_string (MIDORI_EXTENSION (extension), "key", \
                       key)
#define mz_sync_extension_get_key(extension) \
    midori_extension_get_string (MIDORI_EXTENSION (extension), "key")

#define mz_sync_extension_set_sync_name(extension, sync_name) \
    midori_extension_set_string (MIDORI_EXTENSION (extension), "sync_name", \
                       sync_name)
#define mz_sync_extension_get_sync_name(extension) \
    midori_extension_get_string (MIDORI_EXTENSION (extension), "sync_name")

#define mz_sync_extension_set_interval(extension, interval) \
    midori_extension_set_integer (MIDORI_EXTENSION (extension), "interval", \
                       interval)
#define mz_sync_extension_get_interval(extension) \
    midori_extension_get_integer (MIDORI_EXTENSION (extension), "interval")

#define mz_sync_extension_set_sandboxed(extension, sandboxed) \
    midori_extension_set_boolean (MIDORI_EXTENSION (extension), "sandboxed", \
                       sandboxed)
#define mz_sync_extension_get_sandboxed(extension) \
    midori_extension_get_boolean (MIDORI_EXTENSION (extension), "sandboxed")

#define mz_sync_extension_set_sync_bookmarks(extension, sync_bookmarks) \
    midori_extension_set_boolean (MIDORI_EXTENSION (extension), "sync_bookmarks", \
                       sync_bookmarks)
#define mz_sync_extension_get_sync_bookmarks(extension) \
    midori_extension_get_boolean (MIDORI_EXTENSION (extension), "sync_bookmarks")

#define mz_sync_extension_set_upload_delete(extension, upload_delete) \
    midori_extension_set_boolean (MIDORI_EXTENSION (extension), "upload_delete", \
                       upload_delete)
#define mz_sync_extension_get_upload_delete(extension) \
    midori_extension_get_boolean (MIDORI_EXTENSION (extension), "upload_delete")

#define mz_sync_extension_set_upload_add(extension, upload_add) \
    midori_extension_set_boolean (MIDORI_EXTENSION (extension), "upload_add", \
                       upload_add)
#define mz_sync_extension_get_upload_add(extension) \
    midori_extension_get_boolean (MIDORI_EXTENSION (extension), "upload_add")

#define mz_sync_extension_set_upload_modify(extension, upload_modify) \
    midori_extension_set_boolean (MIDORI_EXTENSION (extension), "upload_modify", \
                       upload_modify)
#define mz_sync_extension_get_upload_modify(extension) \
    midori_extension_get_boolean (MIDORI_EXTENSION (extension), "upload_modify")

#define mz_sync_extension_set_download_delete(extension, download_delete) \
    midori_extension_set_boolean (MIDORI_EXTENSION (extension), "download_delete", \
                       download_delete)
#define mz_sync_extension_get_download_delete(extension) \
    midori_extension_get_boolean (MIDORI_EXTENSION (extension), "download_delete")

#define mz_sync_extension_set_download_add(extension, download_add) \
    midori_extension_set_boolean (MIDORI_EXTENSION (extension), "download_add", \
                       download_add)
#define mz_sync_extension_get_download_add(extension) \
    midori_extension_get_boolean (MIDORI_EXTENSION (extension), "download_add")

#define mz_sync_extension_set_download_modify(extension, download_modify) \
    midori_extension_set_boolean (MIDORI_EXTENSION (extension), "download_modify", \
                       download_modify)
#define mz_sync_extension_get_download_modify(extension) \
    midori_extension_get_boolean (MIDORI_EXTENSION (extension), "download_modify")

/* }}} */
G_END_DECLS

#endif /* __MZ_SYNC_EXTENSION_H__ */

