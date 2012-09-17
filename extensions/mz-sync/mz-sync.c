/* :noTabs=true:mode=c:tabSize=4:indentSize=4:folding=explicit:
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
 
 Based on midori/extensions/feed-panel/main.c,
 Copyright (C) 2009 Dale Whittaker <dale@users.sf.net>
*/

#include "mz-sync-panel.h"
#include "mz-sync-extension.h"
#include "sync.h"
#include "decode.h"

#include <midori/midori.h>

#define mz_sync_get_flags(mz_sync) \
    GPOINTER_TO_INT (g_object_get_data (G_OBJECT ((mz_sync)), "flags"))

#define mz_sync_set_flags(mz_sync, flags) \
    g_object_set_data (G_OBJECT ((mz_sync)), "flags", \
                       GINT_TO_POINTER ((flags)))

#define mz_sync_has_flags(mz_sync, flags) \
    ((flags) & mz_sync_get_flags ((mz_sync)))

#define mz_sync_add_flags(mz_sync, flags) \
    mz_sync_set_flags ((mz_sync), (mz_sync_get_flags ((mz_sync)) | (flags)))

#define mz_sync_remove_flags(mz_sync, flags) \
    mz_sync_set_flags ((mz_sync), (mz_sync_get_flags ((mz_sync)) & ~(flags)))

typedef struct
{
    MidoriBrowser* browser;
    MzSyncExtension* extension;
    MzSyncPanel* panel;

    guint source_id;
    gboolean is_running;

} MySyncPrivate;


/* {{{ bridge panel signals -> extension */
static void 
mz_sync_show_prefs_cb (MzSyncPanel* instance, MySyncPrivate*     priv)
{
	g_signal_emit_by_name(priv->extension, "open-preferences");
}
static void 
mz_sync_sync_now_cb (MzSyncPanel* instance, MySyncPrivate*     priv)
{
	mz_sync_extension_sync(priv->extension);
}
/* }}} */

/* {{{ manage added/removed browser **/
static void
mz_sync_app_add_browser_cb (MidoriApp*       app,
                         MidoriBrowser*   browser,
                         MzSyncExtension* extension);

static void
mz_sync_deactivate_browser_cb (MidoriExtension* extension,
                    MySyncPrivate*     priv)
{
    if (priv)
    {
        MidoriApp* app = midori_extension_get_app (extension);

        g_signal_handlers_disconnect_by_func (app,
                mz_sync_app_add_browser_cb, extension);
        g_signal_handlers_disconnect_by_func (extension,
                mz_sync_deactivate_browser_cb, priv);

        gtk_widget_destroy (GTK_WIDGET(priv->panel));
        g_free (priv);
    }
}

static void
mz_sync_app_add_browser_cb (MidoriApp*       app,
                         MidoriBrowser*   browser,
                         MzSyncExtension* extension)
{
    GtkWidget* panel;
    MzSyncPanel* addon;
    MySyncPrivate* priv;
    KatzeArray* roots;
    
    priv = g_new0 (MySyncPrivate, 1);
    panel = katze_object_get_object (browser, "panel");
    addon = mz_sync_panel_new ();
    gtk_widget_show (GTK_WIDGET(addon));
    midori_panel_append_page (MIDORI_PANEL (panel), MIDORI_VIEWABLE (addon));
    g_object_unref (panel);


    priv->extension = extension;
    priv->browser = browser;
    priv->panel = addon;

	g_signal_connect (addon, "preferences",
		G_CALLBACK (mz_sync_show_prefs_cb), priv); 
	g_signal_connect (addon, "sync-now",
		G_CALLBACK (mz_sync_sync_now_cb), priv); 


    roots = mz_sync_extension_get_roots(extension);
    mz_sync_panel_add_feeds (MZ_SYNC_PANEL (addon), KATZE_ITEM (roots));

    g_signal_connect (extension, "deactivate",
        G_CALLBACK (mz_sync_deactivate_browser_cb), priv); 

    g_signal_connect (extension, "sync-started",
        G_CALLBACK (mz_sync_panel_sync_started_cb), addon); 

    g_signal_connect (extension, "sync-ended",
        G_CALLBACK (mz_sync_panel_sync_ended_cb), addon); 
}
/* }}} */

/* {{{ preferences */
static void
on_register_cb (GtkInfoBar* info, gint response_id, GtkDialog* dialog)
{
	if (response_id == GTK_RESPONSE_OK) {
		GtkEntry* entry;
		GtkWidget* label;
		const char* user;
		const gchar* pass;
		const gchar* server;
		const gchar* key;
		const gchar* sync_name;
		GError* err = NULL;
		SYNC_CTX ctx = {0};
		
        entry = g_object_get_data (G_OBJECT (dialog), "user-entry");
        user = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "pass-entry");
        pass = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "server-entry");
        server = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "key-entry");
        key = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "sync_name-entry");
        sync_name = gtk_entry_get_text (GTK_ENTRY(entry));
		
		label = g_object_get_data(G_OBJECT (info), "label");
		
		init_sync(&ctx, MZ_SYNC_USER_AGENT);
		if(!register_user(&ctx, server, user, pass, &err)){
			g_assert(err != NULL);
			gtk_label_set_text(GTK_LABEL(label), err->message);
			gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
				GTK_MESSAGE_ERROR);
			g_clear_error(&err);
		} else {
			if(!set_user(&ctx, server, user, pass, &err) ){
				g_assert( err != NULL);
				gtk_label_set_text(GTK_LABEL(label), err->message);
				gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
					GTK_MESSAGE_ERROR);
			}else{
				if(!create_storage(&ctx, key, sync_name, &err)){
					g_assert( err != NULL);
					gtk_label_set_text(GTK_LABEL(label), err->message);
					gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
						GTK_MESSAGE_ERROR);
				} else { 
					gtk_label_set_text(GTK_LABEL(label), "Hooray: the account has been created !");
					gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
						GTK_MESSAGE_INFO);
					GtkWidget* action = gtk_info_bar_get_action_area(GTK_INFO_BAR(info));
					GList * children = gtk_container_get_children (GTK_CONTAINER(action));
					g_assert(children != NULL);
					g_assert(children->next == NULL);
					gtk_container_remove(GTK_CONTAINER(action), children->data);
					g_list_free(children);
				}
			}
		}
	}
	
}

static void
on_create_sync_name_cb (GtkInfoBar* info, gint response_id, GtkDialog* dialog)
{
	if (response_id == GTK_RESPONSE_OK) {
		GtkEntry* entry;
		GtkWidget* label;
		const char* user;
		const gchar* pass;
		const gchar* server;
		const gchar* key;
		const gchar* sync_name;
		GError* err = NULL;
		SYNC_CTX ctx = {0};
		
        entry = g_object_get_data (G_OBJECT (dialog), "user-entry");
        user = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "pass-entry");
        pass = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "server-entry");
        server = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "key-entry");
        key = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "sync_name-entry");
        sync_name = gtk_entry_get_text (GTK_ENTRY(entry));
		
		label = g_object_get_data(G_OBJECT (info), "label");
		
		init_sync(&ctx, MZ_SYNC_USER_AGENT);
		if(!set_user(&ctx, server, user, pass, &err) ){
			g_assert( err != NULL);
			gtk_label_set_text(GTK_LABEL(label), err->message);
			gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
				GTK_MESSAGE_ERROR);
		}else if(!verify_storage(&ctx, &err)){
			g_assert( err != NULL);
			gtk_label_set_text(GTK_LABEL(label), err->message);
			gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
				GTK_MESSAGE_ERROR);
		} else if(!set_master_key(&ctx, key, &err) ){
			g_assert(err != NULL);
			gtk_label_set_text(GTK_LABEL(label), err->message);
			gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
				GTK_MESSAGE_ERROR);
			g_clear_error(&err);
		} else if(!create_client(&ctx, sync_name, "desktop", &err)){
			g_assert(err != NULL);
			gtk_label_set_text(GTK_LABEL(label), err->message);
			gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
				GTK_MESSAGE_ERROR);
			g_clear_error(&err);
		}else{
			gtk_label_set_text(GTK_LABEL(label), "Hooray: the browser has been registered !");
			gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
				GTK_MESSAGE_INFO);
			GtkWidget* action = gtk_info_bar_get_action_area(GTK_INFO_BAR(info));
			GList * children = gtk_container_get_children (GTK_CONTAINER(action));
			g_assert(children != NULL);
			g_assert(children->next == NULL);
			gtk_container_remove(GTK_CONTAINER(action), children->data);
			g_list_free(children);
		}
		
		free_sync(&ctx);
	}
}

static void
mz_sync_preferences_response_cb (GtkWidget*       dialog,
                                     gint             response_id,
                                     MidoriExtension* extension)
{
    GtkWidget* entry;
    GtkWidget* info;
    GtkWidget* label;
    const gchar* old_state;
    const gchar* new_state;
    const gchar* user;
    const gchar* pass;
    const gchar* server;
    const gchar* key;
    const gchar* sync_name;
    int interval;
    int old_interval;
    char* enc_user;
    GError* err;
    gboolean exists;
    
    gboolean state_changed;
    gboolean vetoed;
    SYNC_CTX ctx = {0};

    vetoed = FALSE;
    
    if (response_id == GTK_RESPONSE_APPLY || response_id == GTK_RESPONSE_OK)
    {
    	state_changed = FALSE;
    	
        entry = g_object_get_data (G_OBJECT (dialog), "user-entry");
        user = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "pass-entry");
        pass = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "server-entry");
        server = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "key-entry");
        key = gtk_entry_get_text (GTK_ENTRY (entry));
        entry = g_object_get_data (G_OBJECT (dialog), "sync_name-entry");
        sync_name = gtk_entry_get_text (GTK_ENTRY(entry));
        entry = g_object_get_data (G_OBJECT (dialog), "interval-entry");
        interval = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(entry));

        info = g_object_get_data (G_OBJECT (dialog), "info-bar");
        
        label = g_object_get_data(G_OBJECT (info), "label");
        // gtk_widget_show (label);
        // content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info));
        // gtk_container_add (GTK_CONTAINER (content_area), label);
        
        gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
        	GTK_MESSAGE_INFO);
        gtk_widget_show (info);
        
        init_sync(&ctx, MZ_SYNC_USER_AGENT);
        
        enc_user = encode_sha1_base32(user);
        err = NULL;
        exists = user_exists(&ctx, enc_user, server, &err);
        if(err != NULL){
        	gtk_label_set_text(GTK_LABEL(label), err->message);
        	vetoed = TRUE;
			gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
				GTK_MESSAGE_ERROR);
			g_clear_error(&err);
        }else if(!exists){
        	gtk_label_set_text(GTK_LABEL(label), "User doesn't exist!");
			gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
				GTK_MESSAGE_WARNING);
			gtk_info_bar_add_button(GTK_INFO_BAR(info), "Register ?", GTK_RESPONSE_OK);
			g_signal_connect (GTK_INFO_BAR(info), "response", G_CALLBACK (on_register_cb), dialog);
        	vetoed = TRUE;
        }else{
        	if(!set_user(&ctx, server, user, pass, &err)){
				g_assert(err != NULL);
				gtk_label_set_text(GTK_LABEL(label), err->message);
				vetoed = TRUE;
				gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
					GTK_MESSAGE_ERROR);
				g_clear_error(&err);
        	}else{
				if(!verify_storage(&ctx, &err) ){
					g_assert(err != NULL);
					gtk_label_set_text(GTK_LABEL(label), err->message);
					vetoed = TRUE;
					gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
						GTK_MESSAGE_ERROR);
					g_clear_error(&err);
				} else if(! set_master_key(&ctx, key, &err) ){
					g_assert(err != NULL);
					gtk_label_set_text(GTK_LABEL(label), err->message);
					vetoed = TRUE;
					gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
						GTK_MESSAGE_ERROR);
					g_clear_error(&err);
				} else{
					exists = client_exists(&ctx, sync_name, &err);
					if(err!=NULL){
						gtk_label_set_text(GTK_LABEL(label), err->message);
						vetoed = TRUE;
						gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
							GTK_MESSAGE_ERROR);
						g_clear_error(&err);
					}else{
						if(!exists){
							gtk_label_set_text(GTK_LABEL(label), "This Browser Id doesn't exist on server. You should create it, so other clients know they aren't alone!");
							gtk_info_bar_set_message_type (GTK_INFO_BAR (info),
								GTK_MESSAGE_WARNING);
							gtk_info_bar_add_button(GTK_INFO_BAR(info), "Create ?", GTK_RESPONSE_OK);
							g_signal_connect (GTK_INFO_BAR(info), "response", G_CALLBACK (on_create_sync_name_cb), dialog);
							vetoed = TRUE;
						}else{
							gtk_label_set_text(GTK_LABEL(label), "Seems all good :-)");
							vetoed = FALSE;
						}
					}
				}
        	}
        	
        }
        free_sync(&ctx);

        
        if(!vetoed){

			old_state = mz_sync_extension_get_user (extension);
			new_state = user;
			if (strcmp(old_state,new_state))
			{
				state_changed = TRUE;
				mz_sync_extension_set_user (extension, new_state);
			}
			
			old_state = mz_sync_extension_get_pass (extension);
			new_state = pass;
			if (strcmp(old_state,new_state))
			{
				state_changed = TRUE;
				mz_sync_extension_set_pass (extension, new_state);
			}
	
			old_state = mz_sync_extension_get_server (extension);
			new_state = server;
			if (strcmp(old_state,new_state))
			{
				state_changed = TRUE;
				mz_sync_extension_set_server (extension, new_state);
			}
	
			old_state = mz_sync_extension_get_key (extension);
			new_state = key;
			if (strcmp(old_state,new_state))
			{
				state_changed = TRUE;
				mz_sync_extension_set_key (extension, new_state);
			}
	
			old_state = mz_sync_extension_get_sync_name (extension);
			new_state = sync_name;
			if (strcmp(old_state,new_state))
			{
				state_changed = TRUE;
				mz_sync_extension_set_sync_name (extension, new_state);
			}
	
			old_interval = mz_sync_extension_get_interval (extension);
			if (old_interval != interval)
			{
				state_changed = TRUE;
				mz_sync_extension_set_interval (extension, interval);
			}

			if(state_changed
			    || mz_sync_extension_get_last_status_was_error(MZ_SYNC_EXTENSION(extension)) )
			{
				mz_sync_extension_reset (MZ_SYNC_EXTENSION(extension));
			}
        }
    } 
    if(response_id != GTK_RESPONSE_APPLY && !vetoed){
    	    gtk_widget_destroy (dialog);
    }
}


static void
mz_sync_preferences_cb (MidoriExtension* extension)
{
    GtkWidget* dialog;
    GtkWidget* content_area;
    GtkWidget* entry;
    GtkWidget* table;
    GtkWidget* info;
    GtkWidget* info_area;
    
    dialog = gtk_dialog_new ();

    //gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

    gtk_dialog_add_buttons (GTK_DIALOG (dialog), GTK_STOCK_APPLY, GTK_RESPONSE_APPLY, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL
    	, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

    content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

    info = gtk_info_bar_new ();
    gtk_widget_set_no_show_all (info, TRUE);
    gtk_box_pack_start (GTK_BOX (content_area), info, TRUE, FALSE, 0);
    g_object_set_data (G_OBJECT (dialog), "info-bar", info);

    entry = gtk_label_new("");
	gtk_widget_show (entry);
	info_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info));
	gtk_container_add (GTK_CONTAINER (info_area), entry);
    g_object_set_data (G_OBJECT (info), "label", entry);
	
    table = gtk_table_new(17, 2,FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (table), 10);
    gtk_table_set_col_spacings (GTK_TABLE (table), 3);
    gtk_box_pack_start (GTK_BOX (content_area), table, TRUE, TRUE, 5);

    /* {{{ server, login info */
    
    entry = gtk_label_new (_("User (email address):"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 0, 1, GTK_FILL, GTK_SHRINK, 0, 0);
    entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (entry),
        mz_sync_extension_get_user (extension));
    g_object_set_data (G_OBJECT (dialog), "user-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 0, 1, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);

    entry = gtk_label_new (_("Password:"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 1, 2, GTK_FILL, GTK_SHRINK, 0, 0);
    entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (entry),
        mz_sync_extension_get_pass (extension));
    g_object_set_data (G_OBJECT (dialog), "pass-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 1, 2, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);

    entry = gtk_label_new (_("Server:"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 2, 3, GTK_FILL, GTK_SHRINK, 0, 0);
    entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (entry),
        mz_sync_extension_get_server (extension));
    g_object_set_data (G_OBJECT (dialog), "server-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 2, 3, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);

    entry = gtk_label_new (_("Secret Key:"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 3, 4, GTK_FILL, GTK_SHRINK, 0, 0);
    entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (entry),
        mz_sync_extension_get_key (extension));
    gtk_entry_set_width_chars (GTK_ENTRY (entry), 32);
    g_object_set_data (G_OBJECT (dialog), "key-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 3, 4, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);

    entry = gtk_label_new (_("This browser Id:"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 4, 5, GTK_FILL, GTK_SHRINK, 0, 0);
    entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (entry),
        mz_sync_extension_get_sync_name (extension));
    gtk_entry_set_width_chars (GTK_ENTRY (entry), 32);
    g_object_set_data (G_OBJECT (dialog), "sync_name-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 4, 5, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    /* }}} */
    
    /* {{{ what to sync */
    entry = gtk_label_new (_("What to sync:"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 5, 6, GTK_FILL, GTK_SHRINK, 0, 0);

    entry = gtk_check_button_new_with_label (_("Sync Bookmarks"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(entry), 
    	mz_sync_extension_get_sync_bookmarks (extension) );
    g_object_set_data (G_OBJECT (dialog), "sync_bookmarks-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 6, 7, GTK_FILL, GTK_SHRINK, 0, 0);
    
    /* }}} */

    /* {{{ how to sync */
    entry = gtk_label_new (_("How to sync:"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 7, 8, GTK_FILL, GTK_SHRINK, 0, 0);

    entry = gtk_check_button_new_with_label (_("Use Sandbox mode (safer)"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(entry), 
    	mz_sync_extension_get_sandboxed (extension) );
    g_object_set_data (G_OBJECT (dialog), "sandboxed-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 8, 9, GTK_FILL, GTK_SHRINK, 0, 0);

    entry = gtk_label_new (_("When changes are made locally:"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 9, 10, GTK_FILL, GTK_SHRINK, 0, 0);
    
    entry = gtk_check_button_new_with_label (_("DELETE on Server"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(entry), 
    	mz_sync_extension_get_upload_delete (extension) );
    g_object_set_data (G_OBJECT (dialog), "upload_delete-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 9, 10, GTK_FILL, GTK_SHRINK, 0, 0);

    entry = gtk_check_button_new_with_label (_("ADD on Server"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(entry), 
    	mz_sync_extension_get_upload_add (extension) );
    g_object_set_data (G_OBJECT (dialog), "upload_add-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 10, 11, GTK_FILL, GTK_SHRINK, 0, 0);

    entry = gtk_check_button_new_with_label (_("MODIFY on Server"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(entry), 
    	mz_sync_extension_get_upload_modify (extension) );
    g_object_set_data (G_OBJECT (dialog), "upload_modify-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 11, 12, GTK_FILL, GTK_SHRINK, 0, 0);

    entry = gtk_label_new (_("When changes are detected on Server:"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 12, 13, GTK_FILL, GTK_SHRINK, 0, 0);

    entry = gtk_check_button_new_with_label (_("DELETE in Client"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(entry), 
    	mz_sync_extension_get_download_delete (extension) );
    g_object_set_data (G_OBJECT (dialog), "download_delete-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 12, 13, GTK_FILL, GTK_SHRINK, 0, 0);

    entry = gtk_check_button_new_with_label (_("ADD in Client"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(entry), 
    	mz_sync_extension_get_download_add (extension) );
    g_object_set_data (G_OBJECT (dialog), "download_add-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 13, 14, GTK_FILL, GTK_SHRINK, 0, 0);

    entry = gtk_check_button_new_with_label (_("MODIFY in Client"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(entry), 
    	mz_sync_extension_get_download_modify (extension) );
    g_object_set_data (G_OBJECT (dialog), "download_modify-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 14, 15, GTK_FILL, GTK_SHRINK, 0, 0);
    /* }}} */
    
    /* {{{ when to sync */
    entry = gtk_label_new (_("When to sync:"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 15, 16, GTK_FILL, GTK_SHRINK, 0, 0);
    entry = gtk_label_new (_("Sync interval (min):"));
    gtk_misc_set_alignment (GTK_MISC(entry), 1, 1);
    gtk_table_attach (GTK_TABLE (table), entry, 0, 1, 16, 17, GTK_FILL, GTK_SHRINK, 0, 0);
    // max once a day...
    GtkAdjustment *spinner_adj =  (GtkAdjustment *) gtk_adjustment_new (
    	mz_sync_extension_get_interval (extension), 0.0, 1440.0, 1.0, 1.0, 0);
    entry = gtk_spin_button_new (spinner_adj, 1.0, 0);
    g_object_set_data (G_OBJECT (dialog), "interval-entry", entry);
    gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 16, 17, GTK_FILL | GTK_EXPAND, GTK_SHRINK, 0, 0);
    
    /* }}} */
    
    g_signal_connect (dialog,
            "response",
            G_CALLBACK (mz_sync_preferences_response_cb),
            extension);
    gtk_widget_show_all (dialog);
}
/* }}} */


static void
mz_sync_activate_cb (MidoriExtension* extension, MidoriApp* app)
{
	mz_sync_extension_activate_cb (MZ_SYNC_EXTENSION(extension));

    KatzeArray* browsers;
    MidoriBrowser* browser;  
    browsers = katze_object_get_object (app, "browsers");
    KATZE_ARRAY_FOREACH_ITEM (browser, browsers)
    mz_sync_app_add_browser_cb (app, browser, MZ_SYNC_EXTENSION(extension));
    g_object_unref (browsers);
    
    g_signal_connect (app, "add-browser",
        G_CALLBACK (mz_sync_app_add_browser_cb), extension);
    
}

MidoriExtension*
extension_init (void)
{
    MzSyncExtension* extension;

    extension = g_object_new (MZ_SYNC_TYPE_EXTENSION,
        "name", _("MOZ SYNC"),
        "description", _("Display Mozilla Sync bookmarks"),
        "version", MZ_SYNC_VERSION MIDORI_VERSION_SUFFIX,
        "authors", "Eric Le Lay <elelay@macports.org>",
        NULL);
    
    mz_sync_extension_install_prefs(extension);

    g_signal_connect (extension, "activate",
        G_CALLBACK (mz_sync_activate_cb), NULL);
    g_signal_connect (extension, "open-preferences",
        G_CALLBACK (mz_sync_preferences_cb), NULL);

    return MIDORI_EXTENSION(extension);
}
