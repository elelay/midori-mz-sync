/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
 
 Based on midori/extensions/feed-panel/feed-panel.c,
 Copyright (C) 2009 Dale Whittaker <dale@users.sf.net>
*/
#include "my-sync-panel.h"

#include <midori/midori.h>
#include <time.h>

#define STOCK_MY_SYNC_PANEL "my-sync-panel"

struct _MySyncPanel
{
    GtkVBox parent_instance;

    GtkWidget* toolbar;
    GtkWidget* treeview;
    GdkPixbuf* pixbuf;
};

struct _MySyncPanelClass
{
    GtkVBoxClass parent_class;
};

static void
my_sync_panel_viewable_iface_init (MidoriViewableIface* iface);

static void
my_sync_panel_insert_item (MySyncPanel*    panel,
                        GtkTreeStore* treestore,
                        GtkTreeIter*  parent,
                        KatzeItem*    item);

static void
my_sync_panel_disconnect_feed (MySyncPanel*  panel,
                            KatzeArray* feed);

G_DEFINE_TYPE_WITH_CODE (MySyncPanel, my_sync_panel, GTK_TYPE_VBOX,
                         G_IMPLEMENT_INTERFACE (MIDORI_TYPE_VIEWABLE,
                            my_sync_panel_viewable_iface_init));

enum
{
    ADD_FEED,
    REMOVE_FEED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
my_sync_panel_treeview_render_icon_cb (GtkTreeViewColumn* column,
                                    GtkCellRenderer*   renderer,
                                    GtkTreeModel*      model,
                                    GtkTreeIter*       iter,
                                    MySyncPanel*         panel)
{
    GdkPixbuf* pixbuf;
    KatzeItem* item;
    KatzeItem* pitem;
    const gchar* uri;

    gtk_tree_model_get (model, iter, 0, &item, -1);
    g_assert (KATZE_IS_ITEM (item));

    if (!KATZE_IS_ARRAY (item))
    {
        pitem = katze_item_get_parent (item);
        g_assert (KATZE_IS_ITEM (pitem));
    }
    else
        pitem = item;

    uri = katze_item_get_uri (item);
    if (uri)
    {
        pixbuf = katze_load_cached_icon (uri, NULL);
        if (!pixbuf)
            pixbuf = panel->pixbuf;
    }
    else
    {
        pixbuf = gtk_widget_render_icon (panel->treeview,
                     GTK_STOCK_DIRECTORY, GTK_ICON_SIZE_MENU, NULL);
    }

    g_object_set (renderer, "pixbuf", pixbuf, NULL);

    if (pixbuf != panel->pixbuf)
        g_object_unref (pixbuf);
}

static void
my_sync_panel_treeview_render_text_cb (GtkTreeViewColumn* column,
                                    GtkCellRenderer*   renderer,
                                    GtkTreeModel*      model,
                                    GtkTreeIter*       iter,
                                    GtkWidget*         treeview)
{
    KatzeItem* item;
    const gchar* title;

    gtk_tree_model_get (model, iter, 0, &item, -1);
    g_assert (KATZE_IS_ITEM (item));

    title = katze_item_get_text (item);
    if (!title || !*title || g_str_equal (title, " "))
        title = katze_item_get_name (item);
    if (!title || !*title || g_str_equal (title, " "))
        title = katze_item_get_uri (item);

    g_object_set (renderer, "text", title, NULL);
    g_object_unref (item);
}

/* get a path to the item in the tree, using KatzeArray indexes.
 * the returned path may be invalid if the tree is out of sync with the KatzeArrays structure
 */
static gboolean
my_sync_panel_get_tree_path(GtkTreeModel* model, KatzeItem* item, GtkTreePath* ret)
{
    KatzeArray* parent = katze_item_get_parent (item);
    if(parent == NULL)
    {
        return true;
    }
    else{
    	    int index = katze_array_get_item_index(parent, item);
    	    if(index < 0){
    	        return false;
    	    }else{
                gtk_tree_path_prepend_index(ret, index);
                return my_sync_panel_get_tree_path(model, KATZE_ITEM(parent), ret);
            }
    }
    
}

static void
my_sync_panel_add_item_cb (KatzeArray* parent,
                        KatzeItem*  child,
                        MySyncPanel*  panel)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    GtkTreeIter child_iter;

    g_return_if_fail (MY_SYNC_IS_PANEL (panel));
    g_return_if_fail (KATZE_IS_ARRAY (parent));
    g_return_if_fail (KATZE_IS_ITEM (child));

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (panel->treeview));

    // the top level array is not in the tree, so add directly items that don't have a grandparent.
    if (katze_item_get_parent (KATZE_ITEM (parent))) 
    {
        // otherwise, add them under the path to the parent
	    GtkTreePath* path = gtk_tree_path_new ();
	    if(my_sync_panel_get_tree_path(model, KATZE_ITEM(parent), path)){
	        g_assert(gtk_tree_model_get_iter(model, &iter, path));
	        
	        // TODO: extra safety, but costly: remove when I'm confident
	        g_assert(gtk_tree_store_iter_is_valid(GTK_TREE_STORE (model), &iter));
	        
	        gtk_tree_store_insert_with_values (GTK_TREE_STORE (model), &child_iter,
	            &iter, G_MAXINT, 0, child, -1);
	        g_object_unref (child);
	    }else{
            gtk_tree_store_insert_with_values (GTK_TREE_STORE (model), &child_iter,
                NULL, G_MAXINT, 0, child, -1);
	    }
	    gtk_tree_path_free(path);
        
    }
    else
    {
            gtk_tree_store_insert_with_values (GTK_TREE_STORE (model), &child_iter,
                NULL, G_MAXINT, 0, child, -1);
    }
    my_sync_panel_insert_item (panel, GTK_TREE_STORE (model), &child_iter, child);
}

static void
my_sync_panel_remove_iter (GtkTreeModel* model,
                        KatzeItem*    removed_item)
{
    guint i;
    GtkTreeIter iter;

    i = 0;
    while (gtk_tree_model_iter_nth_child (model, &iter, NULL, i))
    {
        KatzeItem* item;

        gtk_tree_model_get (model, &iter, 0, &item, -1);

        if (item == removed_item)
        {
            gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);
            g_object_unref (item);
            break;
        }
        g_object_unref (item);
        i++;
    }
}

static void
my_sync_panel_remove_item_cb (KatzeArray* item,
                           KatzeItem*  child,
                           MySyncPanel*  panel)
{
    GtkTreeModel* model;
    KatzeItem* pitem;

    g_return_if_fail (MY_SYNC_IS_PANEL (panel));
    g_return_if_fail (KATZE_IS_ARRAY (item));
    g_return_if_fail (KATZE_IS_ITEM (child));

    if (KATZE_IS_ARRAY (child))
        my_sync_panel_disconnect_feed (panel, KATZE_ARRAY (child));

    if (!katze_item_get_parent (KATZE_ITEM (item)))
    {
        gint n;

        n = katze_array_get_length (KATZE_ARRAY (child));
        g_assert (n == 1);
        pitem = katze_array_get_nth_item (KATZE_ARRAY (child), 0);
    }
    else
        pitem = child;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (panel->treeview));
    my_sync_panel_remove_iter (model, pitem);
    g_object_unref (pitem);
}

static void
my_sync_panel_move_item_cb (KatzeArray* feed,
                         KatzeItem*  child,
                         gint        position,
                         MySyncPanel*  panel)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    guint i;

    g_return_if_fail (MY_SYNC_IS_PANEL (panel));
    g_return_if_fail (KATZE_IS_ARRAY (feed));
    g_return_if_fail (KATZE_IS_ITEM (child));

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (panel->treeview));

    i = 0;
    while (gtk_tree_model_iter_nth_child (model, &iter, NULL, i))
    {
        KatzeItem* item;

        gtk_tree_model_get (model, &iter, 0, &item, -1);

        if (item == child)
        {
            gtk_tree_store_move_after (GTK_TREE_STORE (model), &iter, NULL);
            g_object_unref (item);
            break;
        }
        g_object_unref (item);
        i++;
    }
}

static void
my_sync_panel_disconnect_feed (MySyncPanel*  panel,
                            KatzeArray* feed)
{
    KatzeItem* item;

    g_return_if_fail (KATZE_IS_ARRAY (feed));

    g_signal_handlers_disconnect_by_func (feed,
            my_sync_panel_add_item_cb, panel);
    g_signal_handlers_disconnect_by_func (feed,
            my_sync_panel_remove_item_cb, panel);
    g_signal_handlers_disconnect_by_func (feed,
            my_sync_panel_move_item_cb, panel);

    KATZE_ARRAY_FOREACH_ITEM (item, feed)
    {
        if (KATZE_IS_ARRAY (item))
            my_sync_panel_disconnect_feed (panel, KATZE_ARRAY (item));
        g_object_unref (item);
    }
}

static void
my_sync_panel_insert_item (MySyncPanel*    panel,
                        GtkTreeStore* treestore,
                        GtkTreeIter*  parent,
                        KatzeItem*    item)
{
    g_return_if_fail (KATZE_IS_ITEM (item));

    if (KATZE_IS_ARRAY (item))
    {
        g_signal_connect_after (item, "add-item",
            G_CALLBACK (my_sync_panel_add_item_cb), panel);
        g_signal_connect_after (item, "move-item",
            G_CALLBACK (my_sync_panel_move_item_cb), panel);

        if (!parent)
        {
            g_signal_connect (item, "remove-item",
                G_CALLBACK (my_sync_panel_remove_item_cb), panel);
        }
    }
}

static void
my_sync_panel_row_activated_cb (GtkTreeView*       treeview,
                             GtkTreePath*       path,
                             GtkTreeViewColumn* column,
                             MySyncPanel*         panel)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    KatzeItem* item;
    const gchar* uri;

    model = gtk_tree_view_get_model (treeview);

    if (gtk_tree_model_get_iter (model, &iter, path))
    {
        gtk_tree_model_get (model, &iter, 0, &item, -1);
        uri = katze_item_get_uri (item);
        if (uri && *uri)
        {
            MidoriWebSettings* settings;
            MidoriBrowser* browser;
            gint n;
            browser = midori_browser_get_for_widget (GTK_WIDGET (panel));
            n = midori_browser_add_item (browser, item);
            settings = midori_browser_get_settings (browser);
            if (!katze_object_get_boolean (settings, "open-tabs-in-the-background"))
                midori_browser_set_current_page (browser, n);
        }
        g_object_unref (item);
    }
}

static void
my_sync_panel_cursor_or_row_changed_cb (GtkTreeView* treeview,
                                     MySyncPanel*   panel)
{
}

static void
my_sync_panel_popup_item (GtkWidget*     menu,
                       const gchar*   stock_id,
                       const gchar*   label,
                       KatzeItem*     item,
                       gpointer       callback,
                       MySyncPanel*     panel)
{
    GtkWidget* menuitem;

    menuitem = gtk_image_menu_item_new_from_stock (stock_id, NULL);
    if (label)
        gtk_label_set_text_with_mnemonic (GTK_LABEL (gtk_bin_get_child (
        GTK_BIN (menuitem))), label);
    g_object_set_data (G_OBJECT (menuitem), "KatzeItem", item);
    g_signal_connect (menuitem, "activate", G_CALLBACK (callback), panel);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
    gtk_widget_show (menuitem);
}

static void
my_sync_panel_open_activate_cb (GtkWidget* menuitem,
                             MySyncPanel* panel)
{
    KatzeItem* item;
    const gchar* uri;

    item = (KatzeItem*)g_object_get_data (G_OBJECT (menuitem), "KatzeItem");
    uri = katze_item_get_uri (item);

    if (uri && *uri)
    {
        MidoriBrowser* browser;

        browser = midori_browser_get_for_widget (GTK_WIDGET (panel));
        midori_browser_set_current_uri (browser, uri);
    }
}

static void
my_sync_panel_open_in_tab_activate_cb (GtkWidget* menuitem,
                                    MySyncPanel* panel)
{
    KatzeItem* item;
    const gchar* uri;
    guint n;

    item = (KatzeItem*)g_object_get_data (G_OBJECT (menuitem), "KatzeItem");

    if ((uri = katze_item_get_uri (item)) && *uri)
    {
        MidoriWebSettings* settings;
        MidoriBrowser* browser;

        browser = midori_browser_get_for_widget (GTK_WIDGET (panel));
        n = midori_browser_add_item (browser, item);
        settings = midori_browser_get_settings (browser);
        if (!katze_object_get_boolean (settings, "open-tabs-in-the-background"))
            midori_browser_set_current_page (browser, n);
    }
}

static void
my_sync_panel_open_in_window_activate_cb (GtkWidget* menuitem,
                                       MySyncPanel* panel)
{
    KatzeItem* item;
    const gchar* uri;

    item = (KatzeItem*)g_object_get_data (G_OBJECT (menuitem), "KatzeItem");
    uri = katze_item_get_uri (item);

    if (uri && *uri)
    {
        MidoriBrowser* browser;
        MidoriBrowser* new_browser;

        browser = midori_browser_get_for_widget (GTK_WIDGET (panel));
        g_signal_emit_by_name (browser, "new-window", NULL, &new_browser);
        midori_browser_add_uri (new_browser, uri);
    }
}

static void
my_sync_panel_delete_activate_cb (GtkWidget* menuitem,
                               MySyncPanel* panel)
{
    KatzeItem* item;

    g_return_if_fail (MY_SYNC_IS_PANEL (panel));

    item = (KatzeItem*)g_object_get_data (G_OBJECT (menuitem), "KatzeItem");
    g_signal_emit (panel, signals[REMOVE_FEED], 0, item);
}

static void
my_sync_panel_popup (GtkWidget*      widget,
                  GdkEventButton* event,
                  KatzeItem*      item,
                  MySyncPanel*      panel)
{
    GtkWidget* menu;

    menu = gtk_menu_new ();
    if (!KATZE_IS_ARRAY (item))
    {
        my_sync_panel_popup_item (menu, GTK_STOCK_OPEN, NULL,
            item, my_sync_panel_open_activate_cb, panel);
        my_sync_panel_popup_item (menu, STOCK_TAB_NEW, _("Open in New _Tab"),
            item, my_sync_panel_open_in_tab_activate_cb, panel);
        my_sync_panel_popup_item (menu, STOCK_WINDOW_NEW, _("Open in New _Window"),
            item, my_sync_panel_open_in_window_activate_cb, panel);
    }
    else
    {
        my_sync_panel_popup_item (menu, GTK_STOCK_DELETE, NULL,
            item, my_sync_panel_delete_activate_cb, panel);
    }

    katze_widget_popup (widget, GTK_MENU (menu),
                        event, KATZE_MENU_POSITION_CURSOR);
}

static gboolean
my_sync_panel_button_release_event_cb (GtkWidget*      widget,
                                    GdkEventButton* event,
                                    MySyncPanel*      panel)
{
    GtkTreeModel* model;
    GtkTreeIter iter;

    if (event->button != 2 && event->button != 3)
        return FALSE;

    if (katze_tree_view_get_selected_iter (GTK_TREE_VIEW (widget), &model, &iter))
    {
        KatzeItem* item;

        gtk_tree_model_get (model, &iter, 0, &item, -1);

        if (event->button == 2)
        {
            const gchar* uri = katze_item_get_uri (item);

            if (uri && *uri)
            {
                MidoriWebSettings* settings;
                MidoriBrowser* browser;
                gint n;

                browser = midori_browser_get_for_widget (GTK_WIDGET (panel));
                n = midori_browser_add_item (browser, item);

                settings = midori_browser_get_settings (browser);
                if (!katze_object_get_boolean (settings, "open-tabs-in-the-background"))
                    midori_browser_set_current_page (browser, n);
            }
        }
        else
            my_sync_panel_popup (widget, event, item, panel);

        g_object_unref (item);
        return TRUE;
    }
    return FALSE;
}

static void
my_sync_panel_popup_menu_cb (GtkWidget* widget,
                          MySyncPanel* panel)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    KatzeItem* item;

    if (katze_tree_view_get_selected_iter (GTK_TREE_VIEW (widget), &model, &iter))
    {
        gtk_tree_model_get (model, &iter, 0, &item, -1);
        my_sync_panel_popup (widget, NULL, item, panel);
        g_object_unref (item);
    }
}

void
my_sync_panel_add_feeds (MySyncPanel* panel,
                      KatzeItem* feed)
{
    GtkTreeModel* model;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (panel->treeview));
    g_assert (GTK_IS_TREE_MODEL (model));

    my_sync_panel_insert_item (panel, GTK_TREE_STORE (model), NULL, feed);
}

static const gchar*
my_sync_panel_get_label (MidoriViewable* viewable)
{
    return _("MOZ SYNC");
}

static const gchar*
my_sync_panel_get_stock_id (MidoriViewable* viewable)
{
    return STOCK_MY_SYNC_PANEL;
}

static GtkWidget*
my_sync_panel_get_toolbar (MidoriViewable* viewable)
{
    MySyncPanel* panel = MY_SYNC_PANEL (viewable);

    if (!panel->toolbar)
    {
        GtkWidget* toolbar;

        toolbar = gtk_toolbar_new ();
        gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH_HORIZ);
        gtk_toolbar_set_icon_size (GTK_TOOLBAR (toolbar), GTK_ICON_SIZE_BUTTON);
        panel->toolbar = toolbar;
    }

    return panel->toolbar;
}

static void
my_sync_panel_finalize (GObject* object)
{
    MySyncPanel* panel = MY_SYNC_PANEL (object);

    g_object_unref (panel->pixbuf);
}

static void
my_sync_panel_viewable_iface_init (MidoriViewableIface* iface)
{
    iface->get_stock_id = my_sync_panel_get_stock_id;
    iface->get_label = my_sync_panel_get_label;
    iface->get_toolbar = my_sync_panel_get_toolbar;
}

static void
my_sync_panel_class_init (MySyncPanelClass* class)
{
    GObjectClass* gobject_class;

    signals[ADD_FEED] = g_signal_new (
        "add-my_sync",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

    signals[REMOVE_FEED] = g_signal_new (
        "remove-my_sync",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1,
        G_TYPE_POINTER);

    gobject_class = G_OBJECT_CLASS (class);
    gobject_class->finalize = my_sync_panel_finalize;
}

static void
my_sync_panel_init (MySyncPanel* panel)
{
    GtkTreeStore* model;
    GtkWidget* treewin;
    GtkWidget* treeview;
    GtkTreeViewColumn* column;
    GtkCellRenderer* renderer_pixbuf;
    GtkCellRenderer* renderer_text;
    GtkIconFactory *factory;
    GtkIconSource *icon_source;
    GtkIconSet *icon_set;
    GtkStockItem items[] =
    {
        { STOCK_MY_SYNC_PANEL, N_("_MOZ_SYNC"), 0, 0, NULL }
    };

    factory = gtk_icon_factory_new ();
    gtk_stock_add (items, G_N_ELEMENTS (items));
    icon_set = gtk_icon_set_new ();
    icon_source = gtk_icon_source_new ();
    gtk_icon_source_set_icon_name (icon_source, STOCK_NEWS_FEED);
    gtk_icon_set_add_source (icon_set, icon_source);
    gtk_icon_source_free (icon_source);
    gtk_icon_factory_add (factory, STOCK_MY_SYNC_PANEL, icon_set);
    gtk_icon_set_unref (icon_set);
    gtk_icon_factory_add_default (factory);
    g_object_unref (factory);

    model = gtk_tree_store_new (1, KATZE_TYPE_ITEM);
    treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
    panel->treeview = treeview;
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);
    column = gtk_tree_view_column_new ();
    renderer_pixbuf = gtk_cell_renderer_pixbuf_new ();
    gtk_tree_view_column_pack_start (column, renderer_pixbuf, FALSE);
    gtk_tree_view_column_set_cell_data_func (column, renderer_pixbuf,
            (GtkTreeCellDataFunc)my_sync_panel_treeview_render_icon_cb,
            panel, NULL);
    renderer_text = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (column, renderer_text, FALSE);
    gtk_tree_view_column_set_cell_data_func (column, renderer_text,
            (GtkTreeCellDataFunc)my_sync_panel_treeview_render_text_cb,
            treeview, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
    g_object_unref (model);
    g_object_connect (treeview,
                      "signal::row-activated",
                      my_sync_panel_row_activated_cb, panel,
                      "signal::cursor-changed",
                      my_sync_panel_cursor_or_row_changed_cb, panel,
                      "signal::columns-changed",
                      my_sync_panel_cursor_or_row_changed_cb, panel,
                      "signal::button-release-event",
                      my_sync_panel_button_release_event_cb, panel,
                      "signal::popup-menu",
                      my_sync_panel_popup_menu_cb, panel,
                      NULL);
    gtk_widget_show (treeview);

    treewin = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (treewin),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (treewin),
            GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER (treewin), treeview);
    gtk_widget_show (treewin);

    gtk_box_pack_start (GTK_BOX (panel), treewin, TRUE, TRUE, 0);

    panel->pixbuf = gtk_widget_render_icon (treeview,
                     STOCK_NEWS_FEED, GTK_ICON_SIZE_MENU, NULL);
}

GtkWidget*
my_sync_panel_new (void)
{
    MySyncPanel* panel = g_object_new (MY_SYNC_TYPE_PANEL, NULL);

    return GTK_WIDGET (panel);
}

