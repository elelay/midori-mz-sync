/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
 
 Based on midori/extensions/feed-panel/feed-panel.h,
 Copyright (C) 2009 Dale Whittaker <dale@users.sf.net>
*/

#ifndef __MZ_SYNC_PANEL_H__
#define __MZ_SYNC_PANEL_H__
                             
#include <midori/midori.h>
#include "mz-sync-extension.h"

G_BEGIN_DECLS

#define MZ_SYNC_TYPE_PANEL \
    (mz_sync_panel_get_type ())
#define MZ_SYNC_PANEL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), MZ_SYNC_TYPE_PANEL, MzSyncPanel))
#define MZ_SYNC_PANEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), MZ_SYNC_TYPE_PANEL, MzSyncPanelClass))
#define MZ_SYNC_IS_PANEL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MZ_SYNC_TYPE_PANEL))
#define MZ_SYNC_IS_PANEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), MZ_SYNC_TYPE_PANEL))
#define MZ_SYNC_PANEL_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), MZ_SYNC_TYPE_PANEL, MzSyncPanelClass))

typedef struct _MzSyncPanel                MzSyncPanel;
typedef struct _MzSyncPanelClass           MzSyncPanelClass;

void
mz_sync_panel_add_feeds                     (MzSyncPanel*  panel,
                                          KatzeItem* feed);

void
mz_sync_panel_set_editable                  (MzSyncPanel* panel,
                                          gboolean   editable);

void
mz_sync_panel_sync_started_cb                     (MzSyncExtension* instance, MzSyncPanel*  panel);

void
mz_sync_panel_sync_ended_cb                     (MzSyncExtension* instance,
                                         MzSyncStatus* status,
                                          MzSyncPanel*  panel);

GType
mz_sync_panel_get_type                      (void);

MzSyncPanel*
mz_sync_panel_new                           (void);

G_END_DECLS

#endif /* __MZ_SYNC_PANEL_H__ */
