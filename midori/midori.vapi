/* Copyright (C) 2010 Christian Dywan <christian@twotoasts.de>
   This file is licensed under the terms of the expat license, see the file EXPAT. */

[CCode (cprefix = "Midori", lower_case_cprefix = "midori_")]
namespace Midori {
    [CCode (cheader_filename = "midori/midori.h")]
    public class App : GLib.Object {
        public App ();
        public Browser create_browser ();
        public GLib.List<weak Browser> get_browsers ();

        [NoAccessorMethod]
        public string name { get; set; }
        [NoAccessorMethod]
        public Midori.WebSettings settings { get; set; }
        [NoAccessorMethod]
        public GLib.Object bookmarks { get; set; }
        [NoAccessorMethod]
        public GLib.Object trash { get; set; }
        [NoAccessorMethod]
        public GLib.Object search_engines { get; set; }
        [NoAccessorMethod]
        public GLib.Object history { get; set; }
        [NoAccessorMethod]
        public GLib.Object extensions { get; set; }
        [NoAccessorMethod]
        public GLib.Object browsers { get; }
        public Browser? browser { get; }

        [HasEmitter]
        public signal void add_browser (Browser browser);
        public signal void remove_browser (Browser browser);
        [HasEmitter]
        public signal void quit ();
    }
    public class Browser : Gtk.Window {
        public Browser ();
        public int add_item (GLib.Object item);
        public int add_uri (string uri);
        public unowned View get_nth_tab (int n);
        public GLib.List<weak View> get_tabs ();
        public void block_action (Gtk.Action action);
        public void unblock_action (Gtk.Action action);
        public unowned Gtk.ActionGroup get_action_group ();
        public unowned Browser get_for_widget (Gtk.Widget widget);
        public unowned string[] get_toolbar_actions ();
        public unowned GLib.Object get_proxy_items ();

        [NoAccessorMethod]
        public Gtk.MenuBar menubar { owned get; }
        [NoAccessorMethod]
        public Gtk.Toolbar navigationbar { owned get; }
        [NoAccessorMethod]
        public Gtk.Notebook notebook { owned get; }
        [NoAccessorMethod]
        public Gtk.Widget panel { owned get; }
        [NoAccessorMethod]
        public string uri { owned get; set; }
        public Gtk.Widget? tab { get; set; }
        [NoAccessorMethod]
        public uint load_status { get; }
        [NoAccessorMethod]
        public Gtk.Statusbar statusbar { owned get; }
        [NoAccessorMethod]
        public string statusbar_text { owned get; set; }
        public Midori.WebSettings settings { get; set; }
        [NoAccessorMethod]
        public GLib.Object bookmarks { owned get; set; }
        [NoAccessorMethod]
        public GLib.Object trash { owned get; set; }
        [NoAccessorMethod]
        public GLib.Object search_engines { owned get; set; }
        [NoAccessorMethod]
        public GLib.Object history { owned get; set; }
        [NoAccessorMethod]
        public bool show_tabs { get; set; }

        public signal Browser new_window (Browser? browser);
        [HasEmitter]
        public signal void add_tab (View tab);
        [HasEmitter]
        public signal void remove_tab (View tab);
        [HasEmitter]
        public signal void activate_action (string name);
        public signal void add_download (GLib.Object download);
        public signal void populate_tool_menu (Gtk.Menu menu);
        [HasEmitter]
        public signal void quit ();
    }

    public class Extension : GLib.Object {
        [CCode (has_construct_function = false)]
        public Extension ();
        public unowned Midori.App get_app ();

        public void install_boolean (string name, bool default_value);
        public void install_integer (string name, int default_value);
        public void install_string (string name, string default_value);

        public bool get_boolean (string name);
        public int get_integer (string name);
        public unowned string get_string (string name);

        public void set_boolean (string name, bool value);
        public void set_integer (string name, int value);
        public void set_string (string name, string value);

        [NoAccessorMethod]
        public string name { get; set; }
        [NoAccessorMethod]
        public string description { get; set; }
        [NoAccessorMethod]
        public string version { get; set; }
        [NoAccessorMethod]
        public string authors { get; set; }

        public signal void activate (Midori.App app);
        public signal void deactivate ();
        public signal void open_preferences ();
    }

    public class View : Gtk.VBox {
        [CCode (type = "GtkWidget*")]
        public View (GLib.Object net);
        public View.with_title (string? title=null, WebSettings? settings=null
            , bool append=false);
        public void set_uri (string uri);
        public bool is_blank ();
        public unowned string get_display_uri ();
        public unowned string get_display_title ();
        public unowned string get_icon_uri ();
        public unowned string get_link_uri ();
        public bool has_selection ();
        public string get_selected_text ();
        public Gtk.MenuItem get_proxy_menu_item ();
        public Gtk.Menu get_tab_menu ();
        public Pango.EllipsizeMode get_label_ellipsize ();
        public Gtk.Label get_proxy_tab_label ();
        public GLib.Object get_proxy_item ();
        public bool can_view_source ();
        public bool can_find ();
        public void search_text (string text, bool case_sensitive, bool forward);
        public void mark_text_matches (string text, bool case_sensitive);
        public void set_highlight_text_matches (bool highlight);
        public bool execute_script (string script, out string exception);
        public Gdk.Pixbuf get_snapshot (int width, int height);
        public unowned WebKit.WebView get_web_view ();
        public void populate_popup (Gtk.Menu menu, bool manual);

        public string uri { get; }
        public string title { get; }
        public int security { get; }
        public string mime_type { get; }
        public Gdk.Pixbuf icon { get; }
        public int load_status { get; }
        public double progress { get; set; }
        public bool minimized { get; }
        public float zoom_level { get; }
        public GLib.Object news_feeds { get; }
        public string statusbar_text { get; }
        public WebSettings settings { get; set; }
        public GLib.Object net { get; }

    }

    public class WebSettings : WebKit.WebSettings {
        public WebSettings ();
    }

    [CCode (cheader_filename = "midori/sokoke.h", lower_case_cprefix = "sokoke_")]
    namespace Sokoke {
        public static uint gtk_action_count_modifiers (Gtk.Action action);
    }
}

