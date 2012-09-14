/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/
#include "js.h"

#include <math.h>

GQuark
my_js_error_quark (void)
{
  return g_quark_from_static_string ("my-js-error-quark");
}


JSClassDefinition EmptyObject_definition = {
	0,
	kJSClassAttributeNone,
	
	NULL,
	NULL,
	
	NULL,
	NULL,
	
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

static JSClassRef EmptyObject_class()
{
	static JSClassRef jsClass;
	if (!jsClass)
	        jsClass = JSClassCreate(&EmptyObject_definition);
	
	return jsClass;
}

JSObjectRef js_create_empty_object(JSGlobalContextRef ctx){
	return JSObjectMake(ctx, EmptyObject_class(), NULL);
}

char* get_string(JSStringRef string){
	gsize s = JSStringGetMaximumUTF8CStringSize(string);
	char* k = g_strnfill(s,'\0');
	JSStringGetUTF8CString(string,k,s);
	return k;
}

JSStringRef to_string(const char* s, GError** err){
	
	JSStringRef string;
	const char* end;
	
	if(!g_utf8_validate(s, -1, &end)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"string is not utf8 (invalid char at index %lu)",end-s);
		return NULL;
	}
	
	string = JSStringCreateWithUTF8CString(s);
	
	g_assert(string != NULL);

	return string;
}

char* get_string_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err){
	JSValueRef jsv;
	JSStringRef jsname;
	JSStringRef jstrv;
	char* v;
	
	
	v = NULL;
	
	jsname = JSStringCreateWithUTF8CString(name);
	
	g_assert(jsname != NULL); // otherwise an invalid name was passed
	
	jsv = JSObjectGetProperty(ctx, o, jsname, (JSValueRef*)NULL);
	
	if(jsv == NULL || JSValueIsUndefined(ctx,jsv) || JSValueIsNull(ctx,jsv)){
		// don't log the error: client can test for NULL
		v = NULL;
	}else if(JSValueGetType(ctx,jsv) != kJSTypeString){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"property %s is not a string but %i", name, JSValueGetType(ctx,jsv)); // could jsonify object for debugging
	}else{
		jstrv = JSValueToStringCopy(ctx,jsv,NULL);
		g_assert(jstrv!=NULL);
		v = get_string(jstrv);
		g_assert(v!=NULL);
		JSStringRelease(jstrv);
	}
	
	JSStringRelease(jsname);
	
	return v;
}

JSObjectRef get_object_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err){
	JSValueRef jsv;
	JSStringRef jsname;
	JSObjectRef ret = NULL;
	
	jsname = JSStringCreateWithUTF8CString(name);

	g_assert(jsname != NULL); // otherwise an invalid name was passed

	jsv = JSObjectGetProperty(ctx, o, jsname, (JSValueRef*)NULL);

	g_assert(jsv != NULL); // it's rather undefined...

	if(JSValueIsUndefined(ctx,jsv) || JSValueIsNull(ctx,jsv)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_NO_SUCH_VALUE,
			"property %s not found in object", name); // could jsonify object for debugging
	}else if(!JSValueIsObject(ctx, jsv)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"property %s is not an object but %i", name, JSValueGetType(ctx,jsv)); // could jsonify object for debugging
	}else{
		ret = JSValueToObject(ctx, jsv,NULL);
		
		g_assert(ret != NULL);
	}
	
	JSStringRelease(jsname);

	return ret;
}

gboolean has_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name){
	JSStringRef jsname;
	gboolean ret;
	
	jsname = JSStringCreateWithUTF8CString(name);

	g_assert(jsname != NULL); // otherwise an invalid name was passed

	ret = JSObjectHasProperty(ctx, o, jsname);
	
	JSStringRelease(jsname);

	return ret;
}

GPtrArray* get_array_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err){
	JSValueRef jsv;
	JSStringRef jsname;
	GPtrArray* res;
	GError* tmp_error;
	
	res = NULL;
	tmp_error = NULL;
	
	jsname = JSStringCreateWithUTF8CString(name);

	g_assert(jsname != NULL); // otherwise an invalid name was passed

	jsv = JSObjectGetProperty(ctx, o, jsname, (JSValueRef*)NULL);

	g_assert(jsv != NULL); // it's rather undefined...

	if(JSValueIsUndefined(ctx,jsv) || JSValueIsNull(ctx,jsv)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_NO_SUCH_VALUE,
			"property %s not found in object", name); // could jsonify object for debugging
	}else if(!JSValueIsObject(ctx, jsv)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"property %s is not an array but %i", name, JSValueGetType(ctx,jsv)); // could jsonify object for debugging
	}else{
		o = JSValueToObject(ctx, jsv,NULL);
		g_assert(o != NULL);
		
		tmp_error = NULL;
		gsize pcnt = get_int_prop(ctx, o, "length", &tmp_error);
		
		if(tmp_error != NULL){
			g_clear_error(&tmp_error);
			g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"property %s is not an array (no length)", name);
			res = NULL;
		}else{
		
			res = g_ptr_array_new_full(pcnt,((GDestroyNotify)g_free));
			int i;
			for(i=0;i<pcnt;i++){
				jsv = JSObjectGetPropertyAtIndex(ctx, o, i, NULL);
				if(jsv==NULL){
					g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
						"null property in array %s[%i]", name, i);
					g_ptr_array_unref(res);
					return NULL;
				} else {
					g_ptr_array_add(res, (gpointer)jsv);
				}
			}
		}
	}
	
	JSStringRelease(jsname);
	return res;
}

GPtrArray* get_string_array_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err){
	JSValueRef jsv;
	JSStringRef jsname;
	JSStringRef jstrv;
	GPtrArray* res;
	GError* tmp_error;
	
	res = NULL;
	tmp_error = NULL;
	
	jsname = JSStringCreateWithUTF8CString(name);

	g_assert(jsname != NULL); // otherwise an invalid name was passed

	jsv = JSObjectGetProperty(ctx, o, jsname, (JSValueRef*)NULL);

	g_assert(jsv != NULL); // it's rather undefined...

	if(JSValueIsUndefined(ctx,jsv) || JSValueIsNull(ctx,jsv)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_NO_SUCH_VALUE,
			"property %s not found in object", name); // could jsonify object for debugging
	}else if(!JSValueIsObject(ctx, jsv)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"property %s is not an array but %i", name, JSValueGetType(ctx,jsv)); // could jsonify object for debugging
	}else{
		o = JSValueToObject(ctx, jsv,NULL);
		g_assert(o != NULL);
		
		tmp_error = NULL;
		gsize pcnt = get_int_prop(ctx, o, "length", &tmp_error);
		
		if(tmp_error != NULL){
			g_clear_error(&tmp_error);
			g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"property %s is not an array (no length)", name);
			res = NULL;
		}else{
		
			res = g_ptr_array_new_full(pcnt,((GDestroyNotify)g_free));
			int i;
			for(i=0;i<pcnt;i++){
				jsv = JSObjectGetPropertyAtIndex(ctx, o, i, NULL);
				if(jsv==NULL){
					g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
						"null property in array %s[%i]", name, i);
					g_ptr_array_unref(res);
					return NULL;
				} else if(JSValueGetType(ctx,jsv) == kJSTypeString){
					jstrv = JSValueToStringCopy(ctx,jsv,NULL);
					g_assert(jstrv != NULL);
					g_ptr_array_add(res, get_string(jstrv));
					JSStringRelease(jstrv);
				}else{
					g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
						"property in array %s[%i] is not a string but %i", name, i, JSValueGetType(ctx,jsv));
				}
			}
		}
	}
	
	JSStringRelease(jsname);
	return res;
}


double get_double_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err){
	JSValueRef jsv;
	JSStringRef jsname;
	double res;
	jsname = JSStringCreateWithUTF8CString(name);
	
	g_assert(jsname != NULL); // otherwise an invalid name was passed
	
	jsv = JSObjectGetProperty(ctx, o, jsname, (JSValueRef*)NULL);

	if(JSValueIsUndefined(ctx,jsv) || JSValueIsNull(ctx,jsv)){
		res = NAN;
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_NO_SUCH_VALUE,
			"property %s not found in object", name); // could jsonify object for debugging
	}else if(JSValueGetType(ctx,jsv) != kJSTypeNumber){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"property %s is not a number but %i", name, JSValueGetType(ctx,jsv)); // could jsonify object for debugging
		res = NAN;
	}else{
		res = JSValueToNumber(ctx,jsv,NULL);
	}
	
	JSStringRelease(jsname);
	
	return res;
}

int get_int_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err){
	return (int)get_double_prop(ctx, o,name, err);
}


gboolean get_boolean_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GError** err){
	JSValueRef jsv;
	JSStringRef jsname;
	gboolean res;
	
	jsname = JSStringCreateWithUTF8CString(name);
	
	g_assert(jsname != NULL); // otherwise an invalid name was passed
	
	jsv = JSObjectGetProperty(ctx, o, jsname, (JSValueRef*)NULL);

	if(JSValueIsUndefined(ctx,jsv) || JSValueIsNull(ctx,jsv)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_NO_SUCH_VALUE,
			"property %s not found in object", name); // could jsonify object for debugging
		res=FALSE;
	}else if(!JSValueIsBoolean(ctx,jsv)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"property %s is not a number but %i", name, JSValueGetType(ctx,jsv)); // could jsonify object for debugging
		res=FALSE;
	}else{
		res=JSValueToBoolean(ctx,jsv);
	}
	
	JSStringRelease(jsname);
	
	return res;
}


const char* js_to_json(JSGlobalContextRef ctx, JSObjectRef dict){
	JSStringRef str;
	str = JSValueCreateJSONString(ctx, dict, 2, NULL);
	
	if(str == NULL){
		return NULL;
	}else{
		const char* ret;
		ret = get_string(str);
		JSStringRelease(str);
		return ret;
	}
}

JSObjectRef js_from_json(JSGlobalContextRef ctx, const char* body, gsize len, GError** err){
	
	JSStringRef string;
	JSValueRef dref;
	JSObjectRef ret;
	const char* end;
	char* content;
	
	if(!g_utf8_validate(body, len, &end)){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"JSON string is not utf8 (invalid char at index %lu)",end-body);
		return NULL;
	}
	
	content = g_strndup(body,len);
	string = JSStringCreateWithUTF8CString(content);
	g_free(content);
	
	g_assert(string != NULL);
	
	
	dref = JSValueMakeFromJSONString(ctx, string);
	
	JSStringRelease(string);
	
	if(dref==NULL){
		g_set_error(err, MY_JS_ERROR, MY_JS_ERROR_WRONG_TYPE,
			"invalid JSON: %s\n",body);
		ret = NULL;
	}else{
		g_assert(JSValueIsObject(ctx,dref)); // always returns an object
	
		ret = JSValueToObject(ctx, dref,NULL);
		
		g_assert(ret != NULL); // an object is an object; can't return null
	}
	
	return ret;
}


GPtrArray* get_prop_names(JSGlobalContextRef ctx, JSObjectRef o){
	JSPropertyNameArrayRef ppnames = JSObjectCopyPropertyNames(ctx, o);
	gsize ppcnt = JSPropertyNameArrayGetCount(ppnames);
	GPtrArray* arr = g_ptr_array_new_full(ppcnt, ((GDestroyNotify)g_free));
	int j;
	for(j=0;j<ppcnt;j++){
		JSStringRef jjsn = JSPropertyNameArrayGetNameAtIndex(ppnames, j);
		g_ptr_array_add(arr, get_string(jjsn));
	}
	JSPropertyNameArrayRelease(ppnames);
	return arr;
}


static void set_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, JSValueRef pval, GError** err){
	JSStringRef pname = JSStringCreateWithUTF8CString(name);
	JSObjectSetProperty(ctx, o, pname, pval, kJSPropertyAttributeNone, NULL);
}

gboolean set_string_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, const char* value, GError** err){
	JSStringRef pstr;
	JSValueRef pval;
	pstr = JSStringCreateWithUTF8CString(value);
	pval = JSValueMakeString(ctx, pstr);
	JSStringRelease(pstr);
	set_prop(ctx, o, name, pval, err);
	return TRUE;
}


gboolean set_int_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, int value, GError** err){
	JSValueRef pval = JSValueMakeNumber(ctx,value);
	set_prop(ctx, o, name, pval, err);
	return TRUE;
}

gboolean set_double_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, double value, GError** err){
	JSValueRef pval = JSValueMakeNumber(ctx,value);
	set_prop(ctx, o, name, pval, err);
	return TRUE;
}

gboolean set_boolean_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, gboolean value, GError** err){
	JSValueRef pval = JSValueMakeBoolean(ctx,value);
	set_prop(ctx, o, name, pval, err);
	return TRUE;
}

gboolean set_object_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, JSObjectRef value, GError** err){
	set_prop(ctx, o, name, value, err);
	return TRUE;
}	


gboolean set_string_array_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, GPtrArray* value, GError** err){
	JSValueRef* strings = g_malloc(sizeof(JSValueRef) * (value->len));
	int i;
	GError* tmp_error = NULL;
	for(i=0;i<value->len;i++){
		JSStringRef sr = to_string((const char*)g_ptr_array_index(value, i), &tmp_error);
		if(tmp_error!=NULL){
			g_propagate_error(err, tmp_error);
			return FALSE;
		}
		strings[i] = JSValueMakeString(ctx,sr);
	}
	JSObjectRef pval = JSObjectMakeArray(ctx, value->len, strings, NULL);
	set_prop(ctx, o, name, pval, err);
	return TRUE;
}

gboolean set_array_prop(JSGlobalContextRef ctx, JSObjectRef o, const char* name, JSValueRef* values, int len, GError** err){
	JSObjectRef pval = JSObjectMakeArray(ctx, len, values, NULL);
	set_prop(ctx, o, name, pval, err);
	return TRUE;
}

