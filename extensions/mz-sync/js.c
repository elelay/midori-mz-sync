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

void addProperty(JSGlobalContextRef ctx, gpointer key, GVariant* value, JSObjectRef emptyObject){
	JSStringRef pname = JSStringCreateWithUTF8CString(g_strdup(key));
	JSValueRef pval = NULL;
	if(g_variant_is_of_type(value,G_VARIANT_TYPE_BOOLEAN)){
		pval = JSValueMakeBoolean(ctx, g_variant_get_boolean(value));
	}else if(g_variant_is_of_type(value,G_VARIANT_TYPE_INT32)){
		double val = (double)g_variant_get_int32(value);
		pval = JSValueMakeNumber(ctx,val);
	}else if(g_variant_is_of_type(value,G_VARIANT_TYPE_STRING)){
		JSStringRef pstr = JSStringCreateWithUTF8CString(g_strdup(g_variant_get_string(value, NULL)));
		pval = JSValueMakeString(ctx, pstr);
	}else{
		g_warning("what's this type: %s",g_variant_get_type_string(value));
	}
	if(pval != NULL){
		JSObjectSetProperty(ctx, emptyObject, pname, pval, kJSPropertyAttributeNone, NULL);
	}

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


const char* to_json(JSGlobalContextRef ctx, GHashTable* dict){
	JSObjectRef EmptyObject = JSObjectMake(ctx, EmptyObject_class(), NULL);
	JSStringRef str;
	
	g_hash_table_foreach(dict, (GHFunc)addProperty, EmptyObject);
	
	str = JSValueCreateJSONString(ctx, EmptyObject, 2, NULL);
	
	if(str == NULL){
		return NULL;
	}else{
		return get_string(str);
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


GHashTable* table_from_js(JSGlobalContextRef ctx, JSObjectRef oref){

	GHashTable* res = NULL;
	
	JSPropertyNameArrayRef propnames = JSObjectCopyPropertyNames(ctx, oref);
	gsize pcnt = JSPropertyNameArrayGetCount(propnames);
	res = g_hash_table_new(g_str_hash, g_str_equal);
	int i;
	for(i=0;i<pcnt;i++){
		JSStringRef jsn = JSPropertyNameArrayGetNameAtIndex(propnames, i);
		JSValueRef jsv = JSObjectGetProperty(ctx, oref, jsn, NULL);
		JSStringRef jstrv;
		const char* k = get_string(jsn);
		GVariant* v = NULL;
		if(JSValueIsUndefined(ctx,jsv) || JSValueIsNull(ctx,jsv)){
			g_warning("null Value for %s",k);
		}else{
			switch(JSValueGetType(ctx,jsv)){
			case kJSTypeString:
				jstrv = JSValueToStringCopy(ctx,jsv,NULL);
				if(jstrv!=NULL){
					v = g_variant_new_string(get_string(jstrv));
					JSStringRelease(jstrv);
				}
				break;
			case kJSTypeBoolean:
				v = g_variant_new_boolean(JSValueToBoolean(ctx,jsv));
				break;
			case kJSTypeNumber:
				v = g_variant_new_double(JSValueToNumber(ctx,jsv,NULL));
				break;
			default:
				g_warning("unhandled property type in JSON:%i for %s",JSValueGetType(ctx,jsv),k);
			}
		}
		g_hash_table_insert(res,(gpointer)k,v);
	}
	
	JSPropertyNameArrayRelease(propnames);
	
	return res;
}

GHashTable* table_from_json(JSGlobalContextRef ctx, const char* body, gsize len){
	
	JSObjectRef oref = js_from_json(ctx, body, len, NULL);
	
	if(oref==NULL){
		return NULL;
	}else{
		return table_from_js(ctx, oref);
	}
}
