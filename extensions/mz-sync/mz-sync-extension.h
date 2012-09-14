/*
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

G_BEGIN_DECLS

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

G_END_DECLS

typedef struct {
	gboolean success;
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

#endif /* __MZ_SYNC_EXTENSION_H__ */
