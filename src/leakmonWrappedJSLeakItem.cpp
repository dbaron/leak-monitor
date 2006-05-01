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

/* A report of the leaks in a JavaScript scope. */

// Internal includes
#include "leakmonWrappedJSLeakItem.h"
#include "leakmonService.h"

// Frozen APIs
#include "jsapi.h"

leakmonWrappedJSLeakItem::leakmonWrappedJSLeakItem()
{
}

leakmonWrappedJSLeakItem::~leakmonWrappedJSLeakItem()
{
}

NS_IMPL_ISUPPORTS1(leakmonWrappedJSLeakItem, leakmonIWrappedJSLeakItem)

nsresult
leakmonWrappedJSLeakItem::Init(JSObject* aJSObject)
{
	mJSObject = aJSObject;
	return NS_OK;
}

NS_IMETHODIMP
leakmonWrappedJSLeakItem::GetDescription(PRUnichar **aDescription)
{
	JSContext *cx = leakmonService::GetJSContext();
	NS_ENSURE_TRUE(cx, NS_ERROR_UNEXPECTED);

	// XXX This can execute JS code!  How bad is that?
	JSString *str = JS_ValueToString(cx, OBJECT_TO_JSVAL(mJSObject));
	NS_ENSURE_TRUE(str, NS_ERROR_OUT_OF_MEMORY);

	jschar *chars = JS_GetStringChars(str);
	NS_ENSURE_TRUE(chars, NS_ERROR_OUT_OF_MEMORY);
	size_t len = JS_GetStringLength(str);

	PRUnichar *result = NS_STATIC_CAST(PRUnichar*,
	                        NS_Alloc((len + 1) * sizeof(PRUnichar)));
	NS_ENSURE_TRUE(result, NS_ERROR_OUT_OF_MEMORY);

	NS_ASSERTION(sizeof(jschar) == sizeof(PRUnichar), "char size mismatch");
	memcpy(result, chars, len * sizeof(PRUnichar));
	result[len] = PRUnichar(0);

	*aDescription = result;
	return NS_OK;
}

