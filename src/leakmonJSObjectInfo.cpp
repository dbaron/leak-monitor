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

// Frozen APIs
#include "jsapi.h"
#include "jsdbgapi.h"

leakmonJSObjectInfo::leakmonJSObjectInfo()
	: mProperties(nsnull)
	, mLineStart(0)
	, mLineEnd(0)
{
	mJSValue = JSVAL_NULL;
}

leakmonJSObjectInfo::~leakmonJSObjectInfo()
{
	JSContext *cx = leakmonService::GetJSContext();
	NS_ASSERTION(cx, "can't shutdown properly");

	if (cx) {
		JS_DestroyIdArray(cx, mProperties);
	}
}

NS_IMPL_ISUPPORTS1(leakmonJSObjectInfo, leakmonIJSObjectInfo)

nsresult
leakmonJSObjectInfo::Init(jsval aJSValue, const PRUnichar *aName)
{
	mName.Assign(aName);
	mJSValue = aJSValue;

	JSContext *cx = leakmonService::GetJSContext();
	NS_ENSURE_TRUE(cx, NS_ERROR_UNEXPECTED);

	if (JSVAL_IS_OBJECT(mJSValue)) {
		JSObject *obj = JSVAL_TO_OBJECT(mJSValue);
		mProperties = JS_Enumerate(cx, obj);
		NS_ENSURE_TRUE(mProperties, NS_ERROR_OUT_OF_MEMORY);

		if (JS_ObjectIsFunction(cx, obj)) {
			JSFunction *fun = JS_ValueToFunction(cx, mJSValue);
			NS_ENSURE_TRUE(fun, NS_ERROR_UNEXPECTED);
			JSScript *script = JS_GetFunctionScript(cx, fun);
			NS_ENSURE_TRUE(script, NS_ERROR_UNEXPECTED);
			
			const char *fname = JS_GetScriptFilename(cx, script);
			// XXX Do we know the encoding of this file name?
			mFileName = NS_ConvertUTF8toUTF16(fname);

			mLineStart = JS_GetScriptBaseLineNumber(cx, script);
			mLineEnd = mLineStart + JS_GetScriptLineExtent(cx, script) - 1;
		}
	}

	// XXX This can execute JS code!  How bad is that?
	// shaver didn't seem too scared when I described it to him.
	JSString *str = JS_ValueToString(cx, mJSValue);
	NS_ENSURE_TRUE(str, NS_ERROR_OUT_OF_MEMORY);

	jschar *chars = JS_GetStringChars(str);
	NS_ENSURE_TRUE(chars, NS_ERROR_OUT_OF_MEMORY);
	size_t len = JS_GetStringLength(str);

	NS_ASSERTION(sizeof(jschar) == sizeof(PRUnichar), "char size mismatch");
	mString.Assign(NS_REINTERPRET_CAST(PRUnichar*, chars), len);

	return NS_OK;
}

NS_IMETHODIMP
leakmonJSObjectInfo::GetName(PRUnichar **aResult)
{
	PRUnichar *buf = NS_StringCloneData(mName);
	NS_ENSURE_TRUE(buf, NS_ERROR_OUT_OF_MEMORY);
	*aResult = buf;
	return NS_OK;
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
	*aResult = mProperties ? mProperties->length : 0;
	return NS_OK;
}

NS_IMETHODIMP
leakmonJSObjectInfo::GetPropertyAt(PRUint32 aIndex,
                                   leakmonIJSObjectInfo **aResult)
{
	NS_ENSURE_TRUE(mProperties && aIndex < PRUint32(mProperties->length),
	               NS_ERROR_INVALID_ARG);
	NS_ASSERTION(JSVAL_IS_OBJECT(mJSValue), "shouldn't have set mProperties");

	JSContext *cx = leakmonService::GetJSContext();
	NS_ENSURE_TRUE(cx, NS_ERROR_UNEXPECTED);

	jsval n;
	JSBool ok = JS_IdToValue(cx, mProperties->vector[aIndex], &n);
	NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

	// XXX This can execute JS code!  How bad is that?
	// shaver didn't seem too scared when I described it to him.
	JSString *nstr = JS_ValueToString(cx, n);
	NS_ENSURE_TRUE(nstr, NS_ERROR_OUT_OF_MEMORY);

	const char *propname = JS_GetStringBytes(nstr);
	NS_ENSURE_TRUE(propname, NS_ERROR_OUT_OF_MEMORY);

	const PRUnichar *upropname = JS_GetStringChars(nstr);
	NS_ENSURE_TRUE(upropname, NS_ERROR_OUT_OF_MEMORY);

	// XXX This can execute JS code!  How bad is that?
	// shaver didn't seem too scared when I described it to him.
	jsval v;
	ok = JS_GetProperty(cx, JSVAL_TO_OBJECT(mJSValue), propname, &v);
	NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

	leakmonJSObjectInfo *result = new leakmonJSObjectInfo;
	NS_ENSURE_TRUE(result, NS_ERROR_OUT_OF_MEMORY);

	nsCOMPtr<leakmonIJSObjectInfo> iresult = result;

	nsresult rv = result->Init(v, NS_REINTERPRET_CAST(const PRUnichar*,
	                                                  upropname));
	NS_ENSURE_SUCCESS(rv, rv);

	*aResult = nsnull;
	iresult.swap(*aResult);
	return NS_OK;
}

