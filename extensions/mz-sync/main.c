/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
 
 Based on midori/extensions/feed-panel/main.c,
 Copyright (C) 2009 Dale Whittaker <dale@users.sf.net>
*/

#include "my-sync-panel.h"
#include "sync.h"

#include <midori/midori.h>

#define EXTENSION_NAME "Mozillay Sync Panel"
#define UPDATE_FREQ 10

#define my_sync_get_flags(my_sync) \
    GPOINTER_TO_INT (g_object_get_data (G_OBJECT ((my_sync)), "flags"))

#define my_sync_set_flags(my_sync, flags) \
    g_object_set_data (G_OBJECT ((my_sync)), "flags", \
                       GINT_TO_POINTER ((flags)))

#define my_sync_has_flags(my_sync, flags) \
    ((flags) & my_sync_get_flags ((my_sync)))

#define my_sync_add_flags(my_sync, flags) \
    my_sync_set_flags ((my_sync), (my_sync_get_flags ((my_sync)) | (flags)))

#define my_sync_remove_flags(my_sync, flags) \
    my_sync_set_flags ((my_sync), (my_sync_get_flags ((my_sync)) & ~(flags)))

typedef struct
{
    MidoriBrowser* browser;
    MidoriExtension* extension;
    GtkWidget* panel;
    GPtrArray* bookmarks;
    SYNC_CTX ctx;

    guint source_id;
    gboolean is_running;

} MySyncPrivate;

typedef struct
{
    MidoriExtension* extension;
    GSList* parsers;
    KatzeArray* my_sync;

} FeedNetPrivate;

enum
{
    MY_SYNC_READ = 1,
    MY_SYNC_REMOVE
};

static void
my_sync_app_add_browser_cb (MidoriApp*       app,
                         MidoriBrowser*   browser,
                         MidoriExtension* extension);

static void
my_sync_deactivate_cb (MidoriExtension* extension,
                    MySyncPrivate*     priv)
{
    if (priv)
    {
        MidoriApp* app = midori_extension_get_app (extension);

        g_signal_handlers_disconnect_by_func (app,
                my_sync_app_add_browser_cb, extension);
        g_signal_handlers_disconnect_by_func (extension,
                my_sync_deactivate_cb, priv);

        if (priv->source_id)
            g_source_remove (priv->source_id);
        if (priv->bookmarks)
            g_object_unref (priv->bookmarks);
        gtk_widget_destroy (priv->panel);
        g_free (priv);
    }
}

static void
my_sync_dialog_response_cb (GtkWidget* dialog,
                         gint       response,
                         gpointer   data)
{
    gtk_widget_destroy (dialog);
}

static void
show_error_dialog (GError* e)
{
    GtkWidget* dialog;

    dialog = gtk_message_dialog_new (
        NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        _("Error"));
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
            _("Error: %s"), e->message);
    gtk_window_set_title (GTK_WINDOW (dialog), EXTENSION_NAME);
    gtk_widget_show (dialog);
    g_signal_connect (dialog, "response",
            G_CALLBACK (my_sync_dialog_response_cb), NULL);
}


static gboolean
update_my_syncs (MySyncPrivate* priv)
{

    if (!priv->is_running)
    {
        priv->is_running = TRUE;
        // TODO: sync
    }
    priv->is_running = FALSE;
    return TRUE;
}

static void
add_bookmarks_rec(KatzeArray* parent, SyncItem* itm, int depth)
{
	int i;
	
	for(i=0;i<depth; i++){
		printf(" ");
	}
	if(itm->type == SYNC_ITEM_FOLDER){
		SYNC_FOLDER* actual = itm->actual;
		if(actual == NULL){
			printf("(invalid folder)\n");
		}else{

            KatzeArray* feed;
            
            feed = katze_array_new (KATZE_TYPE_ARRAY);
            katze_item_set_token (KATZE_ITEM (feed), itm->id);
            katze_item_set_uri (KATZE_ITEM (feed), NULL);
            katze_item_set_name (KATZE_ITEM (feed), itm->id);
            katze_item_set_text (KATZE_ITEM (feed), actual->title);
            katze_array_add_item (parent, feed);

            // if(depth == 0){
            //     child = katze_array_new (KATZE_TYPE_ITEM);
            //     katze_item_set_token (KATZE_ITEM (child), itm->id);
            //     katze_item_set_uri (KATZE_ITEM (child), NULL);
            //     katze_item_set_name (KATZE_ITEM (child), itm->id);
            //     katze_item_set_text (KATZE_ITEM (child), actual->title);
            //     katze_array_add_item (feed, child);
            //     feed = child;
            // }

			printf("%s '%s' %f\n", itm->id, actual->title, itm->modified);
			for(i = 0 ; i < actual->children->len; i++){
				add_bookmarks_rec(feed, g_ptr_array_index(actual->children, i), depth+2);
			}
		}
	}else if(itm->type == SYNC_ITEM_BOOKMARK){
		SYNC_BOOKMARK* actual = itm->actual;
		if(actual == NULL){
			printf("(invalid bookmark)\n");
		}else{
			printf("%s %s %s %f\n", itm->id, actual->title, actual->bmkURI, itm->modified);
		}

            KatzeItem* child;
    
            child = katze_item_new ();
            katze_item_set_uri (child, actual->bmkURI);
            katze_item_set_token (KATZE_ITEM (child), actual->bmkURI);
            katze_item_set_name (KATZE_ITEM (child), itm->id);
            katze_item_set_text (KATZE_ITEM (child), actual->title);
            katze_array_add_item (parent, child);
		

	}else if(itm->type == SYNC_ITEM_SEPARATOR){
		printf("_______________________________\n");
	}else{
		g_error("new type of bookmark !");
	}
}


static void
my_sync_app_add_browser_cb (MidoriApp*       app,
                         MidoriBrowser*   browser,
                         MidoriExtension* extension)
{
    GtkWidget* panel;
    GtkWidget* addon;
    SYNC_CTX* ctx;
    MySyncPrivate* priv;
    KatzeArray* roots;
    gsize i;
    const gchar* user;
    const gchar* pass;
    const gchar* key;
    const gchar* server;
    GError* err;
    
    priv = g_new0 (MySyncPrivate, 1);
    ctx = &(priv->ctx);
    init_sync(ctx);
    
    panel = katze_object_get_object (browser, "panel");
    addon = my_sync_panel_new ();
    gtk_widget_show (addon);
    midori_panel_append_page (MIDORI_PANEL (panel), MIDORI_VIEWABLE (addon));
    g_object_unref (panel);

    priv->extension = extension;
    priv->browser = browser;
    priv->panel = addon;

    user = midori_extension_get_string (extension, "user");
    pass = midori_extension_get_string (extension, "pass");
    key = midori_extension_get_string (extension, "key");
    server = midori_extension_get_string (extension, "server");

    g_assert (user && pass && key && server);

    err = NULL;
    

    if(!set_user(ctx, server, user, pass, &err) ){
        if( err != NULL){
            fprintf (stderr, "Error: %s\n", err->message);
            show_error_dialog(err);
        }
        return ;
    }
    g_clear_error(&err);
    
    if(! verify_storage(ctx, &err) ){
        if( err != NULL){
            fprintf (stderr, "Error: %s\n", err->message);
            show_error_dialog(err);
        }
        return ;
    }
    g_clear_error(&err);
    
    if(! set_master_key(ctx, key, &err) ){
        if( err != NULL){
            fprintf (stderr, "Error: %s\n", err->message);
            show_error_dialog(err);
        }
        return ;
    }
    g_clear_error(&err);
    
    
    GPtrArray* bookmarks = get_bookmarks(ctx, &err);
	if(bookmarks == NULL){
		if( err != NULL){
			fprintf (stderr, "Error: %s\n", err->message);
			show_error_dialog(err);
		}else{
			fprintf (stderr, "no bookmarks !\n");
		}
		return ;
	}
	
    roots = katze_array_new (KATZE_TYPE_ARRAY);
    my_sync_panel_add_feeds (MY_SYNC_PANEL (addon), KATZE_ITEM (roots));

	for(i=0;i<bookmarks->len;i++){
		add_bookmarks_rec(roots, g_ptr_array_index(bookmarks, i), 0);
	}  
	
	g_ptr_array_unref(bookmarks);


    g_signal_connect (extension, "deactivate",
        G_CALLBACK (my_sync_deactivate_cb), priv); 

    priv->source_id = g_timeout_add_seconds (UPDATE_FREQ * 60,
                            (GSourceFunc) update_my_syncs, priv);
}

static void
my_sync_activate_cb (MidoriExtension* extension,
                  MidoriApp*       app)
{
    KatzeArray* browsers;
    MidoriBrowser* browser;

    browsers = katze_object_get_object (app, "browsers");
    KATZE_ARRAY_FOREACH_ITEM (browser, browsers)
        my_sync_app_add_browser_cb (app, browser, extension);
    g_object_unref (browsers);

    g_signal_connect (app, "add-browser",
        G_CALLBACK (my_sync_app_add_browser_cb), extension);
}

MidoriExtension*
extension_init (void)
{
    MidoriExtension* extension;

    extension = g_object_new (MIDORI_TYPE_EXTENSION,
        "name", _("MOZ SYNC"),
        "description", _("Display Mozilla Sync bookmarks"),
        "version", "0.1" MIDORI_VERSION_SUFFIX,
        "authors", "Eric Le Lay <neric27@wanadoo.fr>",
        NULL);

    midori_extension_install_string (extension, "user", "toto@titi.fr");
    midori_extension_install_string (extension, "pass", "azertyuiop");
    midori_extension_install_string (extension, "key", "h-r345c-69w4h-kqsfa-y8n6h-h5c8y");
    midori_extension_install_string (extension, "server", "http://localhost:5000");

    g_signal_connect (extension, "activate",
        G_CALLBACK (my_sync_activate_cb), NULL);

    return extension;
}
