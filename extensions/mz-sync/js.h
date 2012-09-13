/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/
#ifndef JS_H
#define JS_H

#include <JavaScriptCore/JavaScript.h>

#include <glib.h>

#define MY_JS_ERROR my_js_error_quark ()

GQuark my_js_error_quark (void);

typedef enum {
	MY_JS_ERROR_NO_SUCH_VALUE,	/* no such property in object */
	MY_JS_ERROR_WRONG_TYPE,		/* property is of wrong type, encoding errors, etc. */
	MY_JS_ERROR_FAILED		/* other... */
} MyJSError;

void init_js();

char* get_string(JSStringRef string);

char* get_string_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err);

int get_int_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err);

double get_double_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err);

gboolean get_boolean_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err);

JSObjectRef get_object_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err);


GPtrArray* get_string_array_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err);
GPtrArray* get_array_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err);
gboolean has_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name);

JSObjectRef js_from_json(JSGlobalContextRef ctx, const char* body, gsize len, GError** err);
GPtrArray* get_prop_names(JSGlobalContextRef ctx, JSObjectRef o);


/* from/to JSON */
const char* js_to_json(JSGlobalContextRef ctx, JSObjectRef dict);
//const char* to_json(JSGlobalContextRef ctx, GHashTable* dict);
// GHashTable* table_from_js(JSGlobalContextRef ctx, JSObjectRef oref);
// GHashTable* table_from_json(JSGlobalContextRef ctx, const char* body, gsize len);

JSObjectRef js_create_empty_object(JSGlobalContextRef ctx);
void add_property(JSGlobalContextRef ctx, JSObjectRef emptyObject, const gchar* key, GVariant* value);

gboolean set_string_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, const char* value, GError** err);

gboolean set_int_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, int value, GError** err);

gboolean set_double_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, double value, GError** err);

gboolean set_boolean_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, gboolean value, GError** err);

gboolean set_object_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, JSObjectRef value, GError** err);


gboolean set_string_array_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GPtrArray* value, GError** err);

#endif
