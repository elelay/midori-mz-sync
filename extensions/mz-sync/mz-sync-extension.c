/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/
#include "mz-sync-extension.h"

#include <midori/midori.h>
#include <time.h>

#include "sync.h"

#define EXTENSION_NAME "Mozilla Sync Panel"

#define MZ_SYNC_EXTENSION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), MZ_SYNC_TYPE_EXTENSION, MzSyncExtensionPrivate))

typedef struct {
    GPtrArray* bookmarks;
    SYNC_CTX ctx;
    KatzeArray* roots;

    guint source_id;
    gboolean is_running;
    gboolean is_stopped;
	
} MzSyncExtensionPrivate;


struct _MzSyncExtension
{
    MidoriExtension parent_instance;

    MzSyncExtensionPrivate* priv;
};

struct _MzSyncExtensionClass
{
    MidoriExtensionClass parent_class;
};

G_DEFINE_TYPE (MzSyncExtension, mz_sync_extension, MIDORI_TYPE_EXTENSION);


enum
{
    SYNC_STARTED,
    SYNC_ENDED,
    
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
mz_sync_extension_init (MzSyncExtension* self)
{
	MzSyncExtensionPrivate* priv;
	
    self->priv = priv = MZ_SYNC_EXTENSION_GET_PRIVATE(self);
    
    priv->bookmarks = NULL;
    memset(&(priv->ctx),0,sizeof(SYNC_CTX));
    priv->roots = katze_array_new (KATZE_TYPE_ARRAY);
    priv->source_id = 0;
    priv->is_running = FALSE;
    priv->is_stopped = FALSE;
}

static void
mz_sync_extension_finalize (GObject* object)
{
    MzSyncExtension* self = MZ_SYNC_EXTENSION (object);

	MzSyncExtensionPrivate* priv = self->priv;
    
    //TODO: unref any priv member here 
    g_object_unref (priv->roots);
    g_ptr_array_unref(priv->bookmarks);
}

static void
mz_sync_extension_class_init (MzSyncExtensionClass* class)
{
    GObjectClass* gobject_class;

    signals[SYNC_STARTED] = g_signal_new (
        "sync-started",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        NULL,
        NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

    signals[SYNC_ENDED] = g_signal_new (
        "sync-ended",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        NULL,
        NULL,
        g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1,
        G_TYPE_POINTER);

    gobject_class = G_OBJECT_CLASS (class);
    gobject_class->finalize = mz_sync_extension_finalize;

    g_type_class_add_private (class, sizeof (MzSyncExtensionPrivate));
}

/* access to private */
KatzeArray*
mz_sync_extension_get_roots (MzSyncExtension* extension)
{
    g_return_val_if_fail (MZ_SYNC_IS_EXTENSION (extension), NULL);
	
    return extension->priv->roots;
}

/* implementation */
static void
mz_sync_dialog_response_cb (GtkWidget* dialog,
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
            G_CALLBACK (mz_sync_dialog_response_cb), NULL);
    g_clear_error(&e);
}


static void
add_bookmarks_rec(KatzeArray* parent, SyncItem* itm, int depth);

gboolean
mz_sync_extension_sync (MzSyncExtension* extension)
{
	
	MzSyncExtensionPrivate* priv = extension->priv;
	
	SYNC_CTX* ctx;
	MzSyncStatus status = {0};

	printf("update_mz_syncs(%p)\n", extension);
	
    if (!priv->is_running && !priv->is_stopped)
    {
		priv->is_running = TRUE;
		ctx = &(priv->ctx);

		g_signal_emit_by_name(extension, "sync-started");
		if(ctx->bulk_key == NULL){
			g_set_error(&(status.error), MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED, "not set correctly...");
			status.success = FALSE;
		}else{
		
			katze_array_clear(priv->roots);
			
			GPtrArray* bookmarks = get_bookmarks(ctx, &(status.error));
			if(bookmarks == NULL){
				status.success = FALSE;
				if( status.error != NULL){
					fprintf (stderr, "Error: %s\n", status.error->message);
					show_error_dialog(status.error);
				}else{
					fprintf (stderr, "no bookmarks !\n");
				}
			}else{
				int i;
				for(i=0;i<bookmarks->len;i++){
					add_bookmarks_rec(priv->roots, g_ptr_array_index(bookmarks, i), 0);
				}  
				
				g_ptr_array_unref(bookmarks);
				status.success = TRUE;
			}
		
		}
		g_signal_emit_by_name(extension, "sync-ended", &status);
		g_clear_error(&(status.error));
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
			g_error("invalid folder: '%s'\n", itm->id);
		}else{

            KatzeArray* feed;
            
            feed = katze_array_new (KATZE_TYPE_ARRAY);
            katze_item_set_token (KATZE_ITEM (feed), itm->id);
            katze_item_set_uri (KATZE_ITEM (feed), NULL);
            katze_item_set_name (KATZE_ITEM (feed), itm->id);
            katze_item_set_text (KATZE_ITEM (feed), actual->title);
            katze_array_add_item (parent, feed);

			//printf("%s '%s' %f\n", itm->id, actual->title, itm->modified);
			for(i = 0 ; i < actual->children->len; i++){
				add_bookmarks_rec(feed, g_ptr_array_index(actual->children, i), depth+2);
			}
		}
	}else if(itm->type == SYNC_ITEM_BOOKMARK){
		SYNC_BOOKMARK* actual = itm->actual;
		if(actual == NULL){
			g_error("invalid bookmark: '%s'\n", itm->id);
		}else{
			//printf("%s %s %s %f\n", itm->id, actual->title, actual->bmkURI, itm->modified);
		}
		// ignore place:* bookmarks as they are not real bookmarks but ff specific
		if(strncmp(actual->bmkURI, "place:", 6)){

            KatzeItem* child;
    
            child = katze_item_new ();
            katze_item_set_uri (child, actual->bmkURI);
            katze_item_set_token (KATZE_ITEM (child), itm->id);
            katze_item_set_name (KATZE_ITEM (child), itm->id);
            katze_item_set_text (KATZE_ITEM (child), actual->title);
            katze_array_add_item (parent, child);
        }

	}else if(itm->type == SYNC_ITEM_SEPARATOR){
		//printf("_______________________________\n");
	}else{
		g_error("new type of bookmark !");
	}
}

void
mz_sync_deactivate_cb (MzSyncExtension* extension, gpointer ignored)
{
	g_signal_handlers_disconnect_by_func (extension,
		mz_sync_deactivate_cb, NULL);
	
	
	MzSyncExtensionPrivate* priv = extension->priv;
	
	if (priv->source_id){
		g_source_remove (priv->source_id);
		priv->source_id = 0;
	}
	priv->is_stopped = TRUE;
	if(!priv->is_running){
		free_sync(&(priv->ctx));
	}
}

void
mz_sync_extension_activate_cb (MzSyncExtension* extension)
{

    mz_sync_extension_reset (extension);
    
    g_signal_connect (extension, "deactivate",
        G_CALLBACK (mz_sync_deactivate_cb), NULL); 
    
}

void
mz_sync_extension_reset (MzSyncExtension* extension)
{
    SYNC_CTX* ctx;
    KatzeArray* roots;
    gsize i;
    const gchar* user;
    const gchar* pass;
    const gchar* key;
    const gchar* server;
    const gchar* sync_name;
    MzSyncExtensionPrivate* priv;
    int interval;
    GError* err;
    
    priv = extension->priv;
    
    ctx = &(priv->ctx);
    free_sync(ctx);
    init_sync(ctx);
    
    user = midori_extension_get_string (MIDORI_EXTENSION(extension), "user");
    pass = midori_extension_get_string (MIDORI_EXTENSION(extension), "pass");
    key = midori_extension_get_string (MIDORI_EXTENSION(extension), "key");
    server = midori_extension_get_string (MIDORI_EXTENSION(extension), "server");
    sync_name = midori_extension_get_string (MIDORI_EXTENSION(extension), "sync_name");
    interval = midori_extension_get_integer (MIDORI_EXTENSION(extension), "interval");
    
    g_assert (user && pass && key && server);
    
    err = NULL;
    
	roots = priv->roots;
	if(!katze_array_is_empty(roots)){
		katze_array_clear(roots);
	}
    if(!set_user(ctx, server, user, pass, &err) ){
        if( err != NULL){
            fprintf (stderr, "Error: %s\n", err->message);
            show_error_dialog(err);
        }
    } else {
		g_clear_error(&err);
		
		if(! verify_storage(ctx, &err) ){
			if( err != NULL){
				fprintf (stderr, "Error: %s\n", err->message);
				show_error_dialog(err);
			}
		} else {
			g_clear_error(&err);
			
			if(! set_master_key(ctx, key, &err) ){
				if( err != NULL){
					fprintf (stderr, "Error: %s\n", err->message);
					show_error_dialog(err);
				}
			} else {
				g_clear_error(&err);
				
				
				// TODO: load cached copy for fast display...
				GPtrArray* bookmarks = get_bookmarks(ctx, &err);
				if(bookmarks == NULL){
					if( err != NULL){
						fprintf (stderr, "Error: %s\n", err->message);
						show_error_dialog(err);
					}else{
						fprintf (stderr, "no bookmarks !\n");
					}
				} else {
					g_clear_error(&err);
					
					for(i=0;i<bookmarks->len;i++){
						add_bookmarks_rec(roots, g_ptr_array_index(bookmarks, i), 0);
					}  
					
					g_ptr_array_unref(bookmarks);
				}
			}
		}
	}
    
	if (priv->source_id){
		g_source_remove (priv->source_id);
		priv->source_id = 0;
	}
    if(interval > 0){
		priv->source_id = g_timeout_add_seconds (interval * 60,
			(GSourceFunc) mz_sync_extension_sync, extension);
	}
}
