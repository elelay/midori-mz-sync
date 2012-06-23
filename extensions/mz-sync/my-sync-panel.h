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

#ifndef __MY_SYNC_PANEL_H__
#define __MY_SYNC_PANEL_H__

#include <midori/midori.h>

G_BEGIN_DECLS

#define MY_SYNC_TYPE_PANEL \
    (my_sync_panel_get_type ())
#define MY_SYNC_PANEL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), MY_SYNC_TYPE_PANEL, MySyncPanel))
#define MY_SYNC_PANEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), MY_SYNC_TYPE_PANEL, MySyncPanelClass))
#define MY_SYNC_IS_PANEL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MY_SYNC_TYPE_PANEL))
#define MY_SYNC_IS_PANEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), MY_SYNC_TYPE_PANEL))
#define MY_SYNC_PANEL_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), MY_SYNC_TYPE_PANEL, MySyncPanelClass))

typedef struct _MySyncPanel                MySyncPanel;
typedef struct _MySyncPanelClass           MySyncPanelClass;

void
my_sync_panel_add_feeds                     (MySyncPanel*  panel,
                                          KatzeItem* feed);

void
my_sync_panel_set_editable                  (MySyncPanel* panel,
                                          gboolean   editable);

GType
my_sync_panel_get_type                      (void);

GtkWidget*
my_sync_panel_new                           (void);

G_END_DECLS

#endif /* __MY_SYNC_PANEL_H__ */
