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

/*
 * service that monitors the state of wrapped JS objects at each GC,
 * particularly to find those in closed windows
 */

// Internal includes
#include "nsLeakMonitorService.h"

// XPCOM glue APIs
#include "nsDebug.h"
#include "nsServiceManagerUtils.h"
#include "nsVoidArray.h"

// ???
#include "nsIAppStartupNotifier.h"

/* static */ nsLeakMonitorService* nsLeakMonitorService::gService = nsnull;
/* static */ JSGCCallback nsLeakMonitorService::gNextGCCallback = nsnull;


nsLeakMonitorService::nsLeakMonitorService()
  : mJSRuntime(nsnull)
{
	NS_ASSERTION(gService == nsnull, "duplicate service creation");

	mJSScopeInfo.ops = nsnull;
}

nsLeakMonitorService::~nsLeakMonitorService()
{
	if (mJSScopeInfo.ops) {
		PL_DHashTableFinish(&mJSScopeInfo);
		mJSScopeInfo.ops = nsnull;
	}
	mJSRuntimeService = nsnull;
	gService = nsnull;
}

NS_IMPL_ISUPPORTS2(nsLeakMonitorService,
                   nsILeakMonitorService,
                   nsIObserver)

NS_IMETHODIMP
nsLeakMonitorService::Observe(nsISupports *aSubject, const char *aTopic,
                              const PRUnichar *aData)
{
	NS_ASSERTION(!strcmp(aTopic, APPSTARTUP_TOPIC), "bad topic");
	return NS_OK;
}

nsresult
nsLeakMonitorService::Init()
{
	// Prevent anyone from using CreateInstance on us.
	NS_ENSURE_TRUE(!gService, NS_ERROR_UNEXPECTED);
	gService = this;

	NS_ASSERTION(!mJSRuntimeService, "Init being called twice");

	nsresult rv;

	mJSRuntimeService =
		do_GetService("@mozilla.org/js/xpc/RuntimeService;1", &rv);
	NS_ENSURE_SUCCESS(rv, rv);

	rv = mJSRuntimeService->GetRuntime(&mJSRuntime);
	NS_ENSURE_SUCCESS(rv, rv);

	gNextGCCallback = JS_SetGCCallbackRT(mJSRuntime, GCCallback);

	return NS_OK;
}

/* static */ JSBool
nsLeakMonitorService::GCCallback(JSContext *cx, JSGCStatus status)
{
	JSBool result = gNextGCCallback ? gNextGCCallback(cx, status) : JS_TRUE;

	if (gService && status == JSGC_END) {
		gService->DidGC();
	}

	return result;
}

struct JSScopeInfoEntry : public PLDHashEntryHdr {
	JSObject *global; // key must be first to match PLDHashEntryStub
	PRBool hasComponents;
	nsVoidArray rootedXPCWJSs;
	PRPackedBool generation; // we let it wrap at one bit
	PRPackedBool hasKnownLeaks;
};

void
nsLeakMonitorService::DidGC()
{
	nsresult rv = BuildContextInfo();
	NS_ASSERTION(NS_SUCCEEDED(rv), "hrm, not sure how to handle this");
}

JS_STATIC_DLL_CALLBACK(intN)
FindXPCGCRoots(void *rp, const char *name, void *data)
{
	nsVoidArray *array = NS_STATIC_CAST(nsVoidArray*, data);

	static const char wrapped_js_root_name[] = "nsXPCWrappedJS::mJSObj";
	if (!strncmp(name, wrapped_js_root_name, sizeof(wrapped_js_root_name)-1)) {
		PRBool ok = array->AppendElement(*NS_STATIC_CAST(JSObject**, rp));
		NS_ASSERTION(ok, "not handling this out of memory case");
	}
	return JS_MAP_GCROOT_NEXT;
}

PR_STATIC_CALLBACK(PLDHashOperator)
ClearRootedLists(PLDHashTable *table, PLDHashEntryHdr *hdr, PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*, hdr);
	entry->rootedXPCWJSs.Clear();
	return PL_DHASH_NEXT;
}

PR_STATIC_CALLBACK(PLDHashOperator)
RemoveDeadScopes(PLDHashTable *table, PLDHashEntryHdr *hdr, PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*, hdr);
	PRPackedBool generation = *NS_STATIC_CAST(PRPackedBool*, arg);
	if (entry->generation != generation)
		return PL_DHASH_REMOVE;
	return PL_DHASH_NEXT;
}

nsresult
nsLeakMonitorService::BuildContextInfo()
{
	if (!mJSScopeInfo.ops) {
		PRBool ok = PL_DHashTableInit(&mJSScopeInfo, PL_DHashGetStubOps(),
		                              nsnull, sizeof(JSScopeInfoEntry), 32);
		NS_ENSURE_TRUE(ok, NS_ERROR_OUT_OF_MEMORY);
	}

	mGeneration = !mGeneration;

	nsVoidArray globalsWithNewLeaks;

	PL_DHashTableEnumerate(&mJSScopeInfo, ClearRootedLists, nsnull);

	JSContext *random_cx = JS_ContextIterator(mJSRuntime, &random_cx);
	NS_ENSURE_TRUE(random_cx, NS_ERROR_UNEXPECTED);

	// Find all the XPConnect wrapped JavaScript objects that are rooted
	// (i.e., owned by native code).
	nsVoidArray xpcGCRoots; // of JSObject*
	JS_MapGCRoots(mJSRuntime, FindXPCGCRoots, &xpcGCRoots);

	for (PRInt32 i = xpcGCRoots.Count() - 1; i >= 0; --i) {
		JSObject *rootedObj = NS_STATIC_CAST(JSObject*, xpcGCRoots[i]);

		JSObject *global, *parent = rootedObj;
		do {
			global = parent;
			parent = JS_GetParent(random_cx, global);
		} while (parent);

		JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*,
			PL_DHashTableOperate(&mJSScopeInfo, global, PL_DHASH_ADD));
		NS_ENSURE_TRUE(entry, NS_ERROR_OUT_OF_MEMORY);

		entry->global = global;
		entry->generation = mGeneration;

		jsval comp;
		entry->hasComponents =
			JS_GetProperty(random_cx, global, "Components", &comp) &&
			JS_TypeOfValue(random_cx, comp) == JSTYPE_OBJECT;

		entry->rootedXPCWJSs.AppendElement(rootedObj);
		if (!entry->hasComponents && !entry->hasKnownLeaks) {
			entry->hasKnownLeaks = PR_TRUE;
			globalsWithNewLeaks.AppendElement(global);
		}
	}

	PL_DHashTableEnumerate(&mJSScopeInfo, RemoveDeadScopes, &mGeneration);

	// XXX Do something with globalsWithNewLeaks!
	
	// XXX Use JS_Enumerate or JS_NewPropertyIterator to find the
	// variables that point to wrapped natives???

	return NS_OK;
}

nsresult
nsLeakMonitorService::EnsureContextInfo()
{
	if (!mJSScopeInfo.ops) {
		nsresult rv = BuildContextInfo();
		NS_ENSURE_SUCCESS(rv, rv);
	}
	return NS_OK;
}
