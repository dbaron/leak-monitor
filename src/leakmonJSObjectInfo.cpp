/* vim: set shiftwidth=4 tabstop=4 autoindent cindent noexpandtab copyindent: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the leak-monitor extension.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   L. David Baron <dbaron@dbaron.org> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/* Information about a JavaScript object that can be displayed in the UI. */

// Internal includes
#include "leakmonJSObjectInfo.h"
#include "leakmonService.h"

// Frozen APIs that require linking against JS
#include "jsapi.h"
#include "jsdbgapi.h"

// Frozen APIs that require linking against NSPR
#include "prprf.h"

// Private JS APIs (for JSScopeProperty)
#include "jsinterp.h" // prerequisite for jsscope.h (ugh!)
#include "jscntxt.h" // prerequisite for jsscope.h (ugh!)
#include "jsscope.h"

leakmonJSObjectInfo::leakmonJSObjectInfo(jsval aJSValue)
	: mJSValue(aJSValue)
	, mIsInitialized(PR_FALSE)
	, mLineStart(0)
	, mLineEnd(0)
{
}

leakmonJSObjectInfo::~leakmonJSObjectInfo()
{
}

NS_IMPL_ISUPPORTS1(leakmonJSObjectInfo, leakmonIJSObjectInfo)

static const PRUnichar kUndefined[] = {'u','n','d','e','f','i','n','e','d',0};
static const PRUnichar kNull[] = {'n','u','l','l',0};
static const PRUnichar kTrue[] = {'t','r','u','e',0};
static const PRUnichar kFalse[] = {'f','a','l','s','e',0};

#define STRLEN_ARRAY(a_) (sizeof(a_)/sizeof((a_)[0]) - 1)

static void
ValueToString(JSContext *cx, jsval aJSValue, nsString& aString)
{
	if (JSVAL_IS_VOID(aJSValue)) {
		aString.Assign(kUndefined, STRLEN_ARRAY(kUndefined));
	} else if (JSVAL_IS_NULL(aJSValue)) {
		aString.Assign(kNull, STRLEN_ARRAY(kNull));
	} else if (JSVAL_IS_INT(aJSValue)) {
		jsint i = JSVAL_TO_INT(aJSValue);
		char buf[20];
		PR_snprintf(buf, sizeof(buf), "%d", i);
		aString.Assign(NS_ConvertASCIItoUTF16(buf));
	} else if (JSVAL_IS_DOUBLE(aJSValue)) {
		jsdouble *d = JSVAL_TO_DOUBLE(aJSValue);
		char buf[50];
		PR_snprintf(buf, sizeof(buf), "%f", *d);
		aString.Assign(NS_ConvertASCIItoUTF16(buf));
	} else if (JSVAL_IS_BOOLEAN(aJSValue)) {
		JSBool b = JSVAL_TO_BOOLEAN(aJSValue);
		if (b)
			aString.Assign(kTrue, STRLEN_ARRAY(kTrue));
		else
			aString.Assign(kFalse, STRLEN_ARRAY(kFalse));
	} else if (JSVAL_IS_STRING(aJSValue)) {
		JSString *str = JSVAL_TO_STRING(aJSValue);
		jschar *chars = JS_GetStringChars(str);
		NS_ASSERTION(chars, "out of memory");
		if (chars) {
			size_t len = JS_GetStringLength(str);

			NS_ASSERTION(sizeof(jschar) == sizeof(PRUnichar),
						 "char size mismatch");
			aString.Assign(reinterpret_cast<PRUnichar*>(chars), len);
		}
	} else {
		JSObject *obj = JSVAL_TO_OBJECT(aJSValue);
		JSClass *clazz = JS_GetClass(cx, obj);
		aString.Assign(PRUnichar('['));
		aString.Append(NS_ConvertASCIItoUTF16(clazz->name));
		aString.Append(PRUnichar(']'));
	}
}

nsresult
leakmonJSObjectInfo::AppendProperty(jsid aID, JSContext *aCx,
                                    leakmonObjectsInReportTable &aObjectsInReport)
{
	jsval n;
	JSBool ok = JS_IdToValue(aCx, aID, &n);
	NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

	// n should be an integer, a string, or an XML QName,
	// AttributeName, or AnyName

	// XXX This can execute JS code!  How bad is that?
	// shaver didn't seem too scared when I described it to him.
	// XXX Could I avoid JS_ValueToString and still handle XML objects
	// correctly?
	JSString *nstr = JS_ValueToString(aCx, n);
	NS_ENSURE_TRUE(nstr, NS_ERROR_OUT_OF_MEMORY);

	const jschar *propname = JS_GetStringChars(nstr);
	NS_ENSURE_TRUE(propname, NS_ERROR_OUT_OF_MEMORY);

	// XXX JS_GetUCProperty can execute JS code!  How bad is that?
	// shaver didn't seem too scared when I described it to him.
	// Since js_GetProperty starts with a call to js_LookupProperty,
	// it's clear that JS_LookupUCProperty does less than
	// JS_GetUCProperty, so prefer Lookup over Get (although it's not
	// clear to me exactly what the differences are).
	jsval v;
	ok = JS_LookupUCProperty(aCx, JSVAL_TO_OBJECT(mJSValue),
	                         propname, JS_GetStringLength(nstr), &v);
	NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

	leakmonJSObjectInfo *info;
	void *key = reinterpret_cast<void*>(v);
	if (!aObjectsInReport.Get(key, &info)) {
		info = new leakmonJSObjectInfo(v);
		NS_ENSURE_TRUE(info, NS_ERROR_OUT_OF_MEMORY);
		aObjectsInReport.Put(key, info);
	}

	PropertyStruct *ps = mProperties.AppendElement();
	ps->mName.Assign(reinterpret_cast<const PRUnichar*>(propname));
	ps->mValue = info;

	return NS_OK;
}

nsresult
leakmonJSObjectInfo::Init(leakmonObjectsInReportTable &aObjectsInReport)
{
	mIsInitialized = PR_TRUE;

	JSContext *cx = leakmonService::GetJSContext();
	NS_ENSURE_TRUE(cx, NS_ERROR_UNEXPECTED);

	JSAutoRequest ar(cx);

	if (!JSVAL_IS_PRIMITIVE(mJSValue)) {
		JSObject *obj = JSVAL_TO_OBJECT(mJSValue);
		JSObject *p;

		for (p = obj; p; p = JS_GetPrototype(cx, p)) {
			// Use JS_PropertyIterator instead of JS_Enumerate since
			// some enumerate hooks (e.g., on wrapped natives) execute
			// code.  But we can only do this is OBJ_IS_NATIVE(p).
			if (OBJ_IS_NATIVE(p)) {
				JSScopeProperty *sprop = nsnull;
				while (JS_PropertyIterator(p, &sprop)) {
					if (!(sprop->attrs & JSPROP_ENUMERATE) ||
						(sprop->flags & SPROP_IS_ALIAS))
						continue;

					nsresult rv = AppendProperty(sprop->id, cx,
					                             aObjectsInReport);
					NS_ENSURE_SUCCESS(rv, rv);
				}
			} else {
				JSIdArray* properties = JS_Enumerate(cx, p);
				if (!properties)
					continue;

				for (jsint i = properties->length - 1; i >= 0; --i) {
					nsresult rv = AppendProperty(properties->vector[i], cx,
					                             aObjectsInReport);
					NS_ENSURE_SUCCESS(rv, rv);
				}
			}
		}

		if (JS_ObjectIsFunction(cx, obj)) {
			JSFunction *fun = JS_ValueToFunction(cx, mJSValue);
			NS_ENSURE_TRUE(fun, NS_ERROR_UNEXPECTED);
			JSScript *script = JS_GetFunctionScript(cx, fun);
			if (script) { // null for native code
				const char *fname = JS_GetScriptFilename(cx, script);
				// XXX Do we know the encoding of this file name?
				mFileName = NS_ConvertUTF8toUTF16(fname);

				mLineStart = JS_GetScriptBaseLineNumber(cx, script);
				mLineEnd = mLineStart + JS_GetScriptLineExtent(cx, script) - 1;
			}
		}
	}

	ValueToString(cx, mJSValue, mString);

	return NS_OK;
}

NS_HIDDEN_(void)
leakmonJSObjectInfo::AppendSelfToString(nsString& aString)
{
	if (!JSVAL_IS_PRIMITIVE(mJSValue)) {
		char buf[30];
		PR_snprintf(buf, sizeof(buf), " (%p",
		            static_cast<void*>(JSVAL_TO_OBJECT(mJSValue)));
		aString.Append(NS_ConvertASCIItoUTF16(buf));
		if (!mFileName.IsEmpty()) {
			aString.Append(PRUnichar(','));
			aString.Append(PRUnichar(' '));
			aString.Append(mFileName);
			PR_snprintf(buf, sizeof(buf), ", %d-%d", mLineStart, mLineEnd);
			aString.Append(NS_ConvertASCIItoUTF16(buf));
		}
		aString.Append(PRUnichar(')'));
	}

	aString.Append(PRUnichar(' '));
	aString.Append(PRUnichar('='));
	aString.Append(PRUnichar(' '));
	aString.Append(mString);
}

NS_IMETHODIMP
leakmonJSObjectInfo::GetFileName(PRUnichar **aResult)
{
	PRUnichar *buf = NS_StringCloneData(mFileName);
	NS_ENSURE_TRUE(buf, NS_ERROR_OUT_OF_MEMORY);
	*aResult = buf;
	return NS_OK;
}

NS_IMETHODIMP
leakmonJSObjectInfo::GetLineStart(PRUint32 *aResult)
{
	*aResult = mLineStart;
	return NS_OK;
}

NS_IMETHODIMP
leakmonJSObjectInfo::GetLineEnd(PRUint32 *aResult)
{
	*aResult = mLineEnd;
	return NS_OK;
}

NS_IMETHODIMP
leakmonJSObjectInfo::GetStringRep(PRUnichar **aResult)
{
	PRUnichar *buf = NS_StringCloneData(mString);
	NS_ENSURE_TRUE(buf, NS_ERROR_OUT_OF_MEMORY);
	*aResult = buf;
	return NS_OK;
}

NS_IMETHODIMP
leakmonJSObjectInfo::GetNumProperties(PRUint32 *aResult)
{
	*aResult = mProperties.Length();
	return NS_OK;
}

NS_IMETHODIMP
leakmonJSObjectInfo::GetPropertyNameAt(PRUint32 aIndex,
		                               PRUnichar **aResult)
{
	PRUnichar *buf =
		NS_StringCloneData(mProperties[PropertiesIndex(aIndex)].mName);
	NS_ENSURE_TRUE(buf, NS_ERROR_OUT_OF_MEMORY);
	*aResult = buf;
	return NS_OK;
}

NS_IMETHODIMP
leakmonJSObjectInfo::GetPropertyValueAt(PRUint32 aIndex,
                                        leakmonIJSObjectInfo **aResult)
{
	NS_ADDREF(*aResult = mProperties[PropertiesIndex(aIndex)].mValue);
	return NS_OK;
}

