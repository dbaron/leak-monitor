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
#include "leakmonReport.h"
#include "leakmonJSObjectInfo.h"
#include "leakmonService.h"

// XPCOM glue APIs
#include "nsCOMPtr.h"
#include "nsMemory.h"
#include "pldhash.h"
#include "nsTPtrArray.h"

// Frozen APIs that require linking against JS
#include "jsapi.h"

// Frozen APIs that require linking against NSPR
#include "prprf.h"

leakmonReport::leakmonReport()
{
}

leakmonReport::~leakmonReport()
{
}

NS_IMPL_ISUPPORTS1(leakmonReport, leakmonIReport)

nsresult
leakmonReport::Init(void *aIdent, PRUint32 aReason,
		            const nsVoidArray &aLeakedWrappedJSObjects)
{
	nsresult rv;

	mIdent = aIdent;
	mReason = aReason;

	JSContext *cx = leakmonService::GetJSContext();
	NS_ENSURE_TRUE(cx, NS_ERROR_UNEXPECTED);

	JSAutoRequest ar(cx);

	/*
	 * This rooting isn't quite sufficient, and can't be, since we
	 * gather these during garbage collection but root them after (when
	 * GC could, in theory, be running on another thread
	 *
	 * But we may as well root for the duration of this function, since
	 * we can.
	 */
	for (PRInt32 i = 0, i_end = aLeakedWrappedJSObjects.Count();
	     i < i_end; ++i) {
		JSObject *obj = static_cast<JSObject*>(aLeakedWrappedJSObjects[i]);
		JSBool ok = JS_LockGCThing(cx, obj);
		if (!ok) {
			NS_NOTREACHED("JS_LockGCThing failed");
			return NS_ERROR_FAILURE;
		}
	}

	/* build mReportText */
	mReportText.Append(NS_ConvertASCIItoUTF16("Leaks in "));
	PRUnichar* ident;
	rv = GetIdent(&ident);
	NS_ENSURE_SUCCESS(rv, rv);
	mReportText.Append(ident);
	nsMemory::Free(ident);
	mReportText.Append(PRUnichar(':'));
	mReportText.Append(PRUnichar('\n'));

	leakmonObjectsInReportTable objectsInReport;
	objectsInReport.Init();

	// weak references to leakmonIJSObjectInfo, with null entries as
	// sentinels to pop stack
	nsTArray<leakmonJSObjectInfo::PropertyStruct> stack;
	NS_NAMED_LITERAL_STRING(lostring, "[leaked object]");

	PRInt32 depth = 0;
	const PRInt32 maxPrintDepth = 3;

	PRInt32 count = aLeakedWrappedJSObjects.Count();
	stack.AppendElements(count);
	for (PRInt32 i = count - 1; i >= 0; --i) {
		JSObject *obj = static_cast<JSObject*>(aLeakedWrappedJSObjects[i]);
		jsval val = OBJECT_TO_JSVAL(obj);
		void *key = reinterpret_cast<void*>(val);
		leakmonJSObjectInfo *info;
		NS_ASSERTION(!objectsInReport.Get(key, &info), "got object twice");

		info = new leakmonJSObjectInfo(val);
		NS_ENSURE_TRUE(info, NS_ERROR_OUT_OF_MEMORY);
		objectsInReport.Put(key, info);

		mLeakedObjects.AppendObject(info);
		stack[i].mName = lostring;
		stack[i].mValue = info;
	}

	rv = NS_OK;
	while (stack.Length()) {
		PRUint32 i = stack.Length() - 1;
		nsString name = stack[i].mName;
		leakmonJSObjectInfo* info = stack[i].mValue;
		stack.RemoveElementAt(i);

		if (!info) {
			/* sentinel value to pop depth */
			--depth;
			continue;
		}

		char twistyChar = ' ';

		/* handle children */
		PRBool wasInitialized = info->IsInitialized();
		if (!wasInitialized) {
			rv = info->Init(objectsInReport);
			NS_ENSURE_SUCCESS(rv, rv);
		}

		PRUint32 nprops;
		info->GetNumProperties(&nprops);
		if (nprops > 0) {
			/* figure out if we've already printed this object */
			// Should we check a maxDepth here too?
			if (!wasInitialized) {
				twistyChar = (depth < maxPrintDepth) ? '+' : '-';

				// append sentinel value to pop depth
				stack.AppendElement();

				/* append kids to stack */
				for (PRUint32 iprop = nprops - 1; iprop != PRUint32(-1);
				     --iprop) {
					stack.AppendElement(info->PropertyStructAt(iprop));
				}
			} else {
				twistyChar = '-';
			}
		}

		/* print representation of this object */
		if (depth <= maxPrintDepth) {
			for (PRInt32 c = 0; c < depth; ++c)
				mReportText.Append(PRUnichar(' '));
			mReportText.Append(PRUnichar('['));
			mReportText.Append(PRUnichar(twistyChar));
			mReportText.Append(PRUnichar(']'));
			mReportText.Append(PRUnichar(' '));
			mReportText.Append(name);
			info->AppendSelfToString(mReportText);
			mReportText.Append(PRUnichar('\n'));
		}

		if (nprops > 0 && !wasInitialized)
			++depth;
	}

	for (PRInt32 i = 0, i_end = aLeakedWrappedJSObjects.Count();
		 i < i_end; ++i) {
		JS_UnlockGCThing(cx, static_cast<JSObject*>(
											aLeakedWrappedJSObjects[i]));
	}

	return rv;
}

NS_IMETHODIMP
leakmonReport::GetIdent(PRUnichar **aResult)
{
	char buf[40];
	PR_snprintf(buf, sizeof(buf), "window 0x%p", mIdent);

	PRUnichar *res = NS_StringCloneData(NS_ConvertASCIItoUTF16(buf));
	NS_ENSURE_TRUE(res, NS_ERROR_OUT_OF_MEMORY);
	*aResult = res;

	return NS_OK;
}

NS_IMETHODIMP
leakmonReport::GetReason(PRUint32 *aResult)
{
	*aResult = mReason;
	return NS_OK;
}

NS_IMETHODIMP
leakmonReport::GetReportText(PRUnichar **aResult)
{
	PRUnichar *buf = NS_StringCloneData(mReportText);
	NS_ENSURE_TRUE(buf, NS_ERROR_OUT_OF_MEMORY);
	*aResult = buf;
	return NS_OK;
}

NS_IMETHODIMP
leakmonReport::GetLeakedWrappedJSs(PRUint32 *aItemCount,
                                   leakmonIJSObjectInfo ***aItems)
{
	PRInt32 count = mLeakedObjects.Count();
	if (count == 0) {
		*aItemCount = 0;
		*aItems = nsnull;
		return NS_OK;
	}

	leakmonIJSObjectInfo **array =
		static_cast<leakmonIJSObjectInfo**>(
			nsMemory::Alloc(count * sizeof(leakmonIJSObjectInfo*)));
	NS_ENSURE_TRUE(array, NS_ERROR_OUT_OF_MEMORY);

	for (PRInt32 i = 0; i < count; ++i) {
		NS_ADDREF(array[i] = mLeakedObjects[i]);
	}

	*aItemCount = count;
	*aItems = array;
	return NS_OK;
}

