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

// Frozen APIs that require linking against JS
#include "jsapi.h"

// Frozen APIs that require linking against NSPR
#include "prprf.h"

leakmonReport::leakmonReport()
{
}

leakmonReport::~leakmonReport()
{
	JSContext *cx = leakmonService::GetJSContext();
	NS_ASSERTION(cx, "yikes");

	if (cx) {
		for (PRInt32 i = 0, i_end = mLeakedWrappedJSObjects.Count();
			 i < i_end; ++i) {
			JS_UnlockGCThing(cx, NS_STATIC_CAST(JSObject*,
												mLeakedWrappedJSObjects[i]));
		}
	}
}

NS_IMPL_ISUPPORTS1(leakmonReport, leakmonIReport)

struct ObjectInReportEntry : public PLDHashEntryHdr {
	void *key; /* lines up with PLDHashEntryStub */
};

nsresult
leakmonReport::Init(void *aIdent, const nsVoidArray &aLeakedWrappedJSObjects)
{
	nsresult rv;

	mIdent = aIdent;

	JSContext *cx = leakmonService::GetJSContext();
	NS_ENSURE_TRUE(cx, NS_ERROR_UNEXPECTED);

	mLeakedWrappedJSObjects = aLeakedWrappedJSObjects;
	
	/*
	 * This rooting isn't quite sufficient, and can't be, since we
	 * gather these during garbage collection but root them after (when
	 * GC could, in theory, be running on another thread
	 */
	for (PRInt32 i = 0, i_end = mLeakedWrappedJSObjects.Count();
	     i < i_end; ++i) {
		JSObject *obj = NS_STATIC_CAST(JSObject*, mLeakedWrappedJSObjects[i]);
		JSBool ok = JS_LockGCThing(cx, obj);
		if (!ok) {
			NS_NOTREACHED("JS_LockGCThing failed");
			mLeakedWrappedJSObjects.Clear();
			return NS_ERROR_FAILURE;
		}
	}
	NS_ENSURE_TRUE(mLeakedWrappedJSObjects.Count() ==
	                   aLeakedWrappedJSObjects.Count(),
	               NS_ERROR_OUT_OF_MEMORY);

	/* build mReportText */
	mReportText.Append(NS_ConvertASCIItoUTF16("Leaks in window "));
	PRUnichar* ident;
	rv = GetIdent(&ident);
	NS_ENSURE_SUCCESS(rv, rv);
	mReportText.Append(ident);
	nsMemory::Free(ident);
	mReportText.Append(PRUnichar(':'));
	mReportText.Append(PRUnichar('\n'));

	PLDHashTable objectsInReport;
	nsVoidArray stack; /* strong references to leakmonIJSObjectInfo, with
	                      null entries as sentinels to pop stack */
	PRInt32 depth = 0;
	const PRInt32 maxDepth = 3;

	PRBool ok = PL_DHashTableInit(&objectsInReport, PL_DHashGetStubOps(),
	                              nsnull, sizeof(ObjectInReportEntry), 16);
	NS_ENSURE_TRUE(ok, NS_ERROR_OUT_OF_MEMORY);

	leakmonIJSObjectInfo **array;
	PRUint32 count;
	rv = GetLeakedWrappedJSs(&count, &array);
	NS_ENSURE_SUCCESS(rv, rv);
	for (PRInt32 j = count - 1; j >= 0; --j) {
		stack.AppendElement(array[j]);
	}
	nsMemory::Free(array);

	rv = NS_OK;
	while (stack.Count()) {
		PRInt32 i = stack.Count() - 1;
		nsCOMPtr<leakmonIJSObjectInfo> iinfo =
			dont_AddRef(NS_STATIC_CAST(leakmonIJSObjectInfo*, stack[i]));
		stack.RemoveElementAt(i);
		leakmonJSObjectInfo *info = NS_STATIC_CAST(leakmonJSObjectInfo*,
			NS_STATIC_CAST(leakmonIJSObjectInfo*, iinfo));

		if (!info) {
			/* sentinel value to pop depth */
			--depth;
			continue;
		}

		char twistyChar = ' ';

		/* handle children */
		PRUint32 nprops;
		info->GetNumProperties(&nprops);
		if (nprops > 0) {
			/* figure out if we've already printed this object */
			ObjectInReportEntry *inReportEntry =
				NS_STATIC_CAST(ObjectInReportEntry*,
					PL_DHashTableOperate(&objectsInReport,
					                     (void*) info->GetJSValue(),
					                     PL_DHASH_ADD));
			PRBool inReport;
			if (!inReportEntry) {
				rv = NS_ERROR_OUT_OF_MEMORY;
				inReport = PR_TRUE;
			} else if (inReportEntry->key) {
				inReport = PR_TRUE;
			} else {
				inReport = PR_FALSE;
				inReportEntry->key = (void*) info->GetJSValue();
			}

			if (depth < maxDepth && !inReport) {
				twistyChar = '+';
				stack.AppendElement(nsnull); /* sentinel value to pop depth */

				/* append kids to stack */
				for (PRUint32 iprop = nprops - 1; iprop != PRUint32(-1);
				     --iprop) {
					leakmonIJSObjectInfo *child;
					nsresult rv2 = info->GetPropertyAt(iprop, &child);
					if (NS_SUCCEEDED(rv2)) {
						stack.AppendElement(child); /* transfer reference */
					} else {
						rv = rv2;
					}
				}
			} else {
				twistyChar = '-';
			}
		}

		/* print representation of this object */
		for (PRInt32 c = 0; c < depth; ++c)
			mReportText.Append(PRUnichar(' '));
		mReportText.Append(PRUnichar('['));
		mReportText.Append(PRUnichar(twistyChar));
		mReportText.Append(PRUnichar(']'));
		mReportText.Append(PRUnichar(' '));
		info->AppendSelfToString(mReportText);
		mReportText.Append(PRUnichar('\n'));

		if (twistyChar == '+')
			++depth;
	}

	PL_DHashTableFinish(&objectsInReport);

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
	PRInt32 count = mLeakedWrappedJSObjects.Count();
	if (count == 0) {
		*aItemCount = 0;
		*aItems = nsnull;
		return NS_OK;
	}

	leakmonIJSObjectInfo **array =
		NS_STATIC_CAST(leakmonIJSObjectInfo**,
			nsMemory::Alloc(count * sizeof(leakmonIJSObjectInfo*)));
	NS_ENSURE_TRUE(array, NS_ERROR_OUT_OF_MEMORY);

	memset(array, 0, count * sizeof(leakmonIJSObjectInfo*));

	for (PRInt32 i = 0; i < count; ++i) {
		leakmonJSObjectInfo *item = new leakmonJSObjectInfo;
		nsresult rv;
		if (item) {
			NS_NAMED_LITERAL_STRING(n, "[leaked object]");
			rv = item->Init(OBJECT_TO_JSVAL(NS_STATIC_CAST(JSObject*,
			                                    mLeakedWrappedJSObjects[i])),
			                n.get());
		} else {
			rv = NS_ERROR_OUT_OF_MEMORY;
		}
		if (NS_FAILED(rv)) {
			NS_FREE_XPCOM_ISUPPORTS_POINTER_ARRAY(count, array);
			return rv;
		}
		NS_ADDREF(array[i] = item);
	}

	*aItemCount = count;
	*aItems = array;
	return NS_OK;
}

