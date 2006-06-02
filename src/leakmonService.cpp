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
#include "leakmonService.h"
#include "leakmonReport.h"

// Frozen APIs
#include "nsIObserverService.h"
#include "nsIWindowWatcher.h"
#include "nsIDOMWindow.h"

// XPCOM glue APIs
#include "nsDebug.h"
#include "nsMemory.h"
#include "nsServiceManagerUtils.h"
#include "nsVoidArray.h"

// Unfrozen APIs that shouldn't hurt
// filed https://bugzilla.mozilla.org/show_bug.cgi?id=335977
#include "nsIAppStartupNotifier.h"

/* static */ leakmonService* leakmonService::gService = nsnull;
/* static */ JSGCCallback leakmonService::gNextGCCallback = nsnull;

static const char gQuitApplicationTopic[] = "quit-application";

leakmonService::leakmonService()
  : mJSRuntime(nsnull)
  , mGeneration(0)
  , mHaveQuitApp(PR_FALSE)
{
	NS_ASSERTION(gService == nsnull, "duplicate service creation");

	mJSScopeInfo.ops = nsnull;
	// This assumes we're always created on the main thread.
	mMainThread = PR_GetCurrentThread();
}

leakmonService::~leakmonService()
{
	if (mJSScopeInfo.ops) {
		PL_DHashTableFinish(&mJSScopeInfo);
		mJSScopeInfo.ops = nsnull;
	}

	if (mJSContext) {
		JS_DestroyContext(mJSContext);
		mJSContext = nsnull;
	}

	mJSRuntimeService = nsnull;

	gService = nsnull;
}

NS_IMPL_ISUPPORTS3(leakmonService,
                   leakmonIService,
                   nsIObserver,
                   nsISupportsWeakReference)

NS_IMETHODIMP
leakmonService::Observe(nsISupports *aSubject, const char *aTopic,
                        const PRUnichar *aData)
{
	if (!strcmp(aTopic, gQuitApplicationTopic)) {
		mHaveQuitApp = PR_TRUE;
	} else if (!strcmp(aTopic, APPSTARTUP_TOPIC)) {
	} else {
		NS_NOTREACHED("bad topic");
	}
	return NS_OK;
}

nsresult
leakmonService::Init()
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

	mJSContext = JS_NewContext(mJSRuntime, 256);
	NS_ENSURE_TRUE(mJSContext, NS_ERROR_OUT_OF_MEMORY);

	gNextGCCallback = JS_SetGCCallbackRT(mJSRuntime, GCCallback);

	nsCOMPtr<nsIObserverService> os =
		do_GetService("@mozilla.org/observer-service;1", &rv);
	NS_ENSURE_SUCCESS(rv, rv);

	rv = os->AddObserver(this, gQuitApplicationTopic, PR_TRUE);
	NS_ENSURE_SUCCESS(rv, rv);

	return NS_OK;
}

/* static */ JSBool JS_DLL_CALLBACK
leakmonService::GCCallback(JSContext *cx, JSGCStatus status)
{
	JSBool result = gNextGCCallback ? gNextGCCallback(cx, status) : JS_TRUE;

	if (gService && status == JSGC_END) {
		if (PR_GetCurrentThread() == gService->mMainThread) {
			gService->DidGC();
		} else {
			// XXX Perhaps we should proxy, except that requires unfrozen APIs.
		}
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
leakmonService::DidGC()
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
leakmonService::BuildContextInfo()
{
	if (!mJSScopeInfo.ops) {
		PRBool ok = PL_DHashTableInit(&mJSScopeInfo, PL_DHashGetStubOps(),
		                              nsnull, sizeof(JSScopeInfoEntry), 32);
		NS_ENSURE_TRUE(ok, NS_ERROR_OUT_OF_MEMORY);
	}

	mGeneration = !mGeneration;

	nsVoidArray globalsWithNewLeaks;

	PL_DHashTableEnumerate(&mJSScopeInfo, ClearRootedLists, nsnull);

	// Find all the XPConnect wrapped JavaScript objects that are rooted
	// (i.e., owned by native code).
	nsVoidArray xpcGCRoots; // of JSObject*
	JS_MapGCRoots(mJSRuntime, FindXPCGCRoots, &xpcGCRoots);

	PRInt32 i;
	for (i = xpcGCRoots.Count() - 1; i >= 0; --i) {
		JSObject *rootedObj = NS_STATIC_CAST(JSObject*, xpcGCRoots[i]);

		JSObject *global, *parent = rootedObj;
		do {
			global = parent;
			parent = JS_GetParent(mJSContext, global);
		} while (parent);

		JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*,
			PL_DHashTableOperate(&mJSScopeInfo, global, PL_DHASH_ADD));
		NS_ENSURE_TRUE(entry, NS_ERROR_OUT_OF_MEMORY);

		entry->global = global;
		entry->generation = mGeneration;

		jsval comp;
		entry->hasComponents =
			JS_GetProperty(mJSContext, global, "Components", &comp) &&
			JS_TypeOfValue(mJSContext, comp) == JSTYPE_OBJECT;

		entry->rootedXPCWJSs.AppendElement(rootedObj);
		if (!entry->hasComponents && !entry->hasKnownLeaks) {
			entry->hasKnownLeaks = PR_TRUE;
			globalsWithNewLeaks.AppendElement(global);
		}
	}

	PL_DHashTableEnumerate(&mJSScopeInfo, RemoveDeadScopes, &mGeneration);

	for (i = 0; i < globalsWithNewLeaks.Count(); ++i) {
		NotifyNewLeak(NS_STATIC_CAST(JSObject*, globalsWithNewLeaks[i]));
	}
	
	// XXX Look at wrapped natives too, perhaps?
	// Maybe use JS_Enumerate or JS_NewPropertyIterator to find the
	// variables that point to wrapped natives???

	return NS_OK;
}

nsresult
leakmonService::NotifyNewLeak(JSObject *aGlobalObject)
{
	nsresult rv;

	nsCOMPtr<nsIWindowWatcher> ww =
		do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
	NS_ENSURE_SUCCESS(rv, rv);

	JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*,
		PL_DHashTableOperate(&mJSScopeInfo, aGlobalObject, PL_DHASH_LOOKUP));
	NS_ASSERTION(PL_DHASH_ENTRY_IS_BUSY(entry), "entry not in hashtable");

	leakmonReport *report = new leakmonReport();
	NS_ENSURE_TRUE(report, NS_ERROR_OUT_OF_MEMORY);
	nsCOMPtr<leakmonIReport> reportI = report;
	rv = report->Init(entry->rootedXPCWJSs);
	NS_ENSURE_SUCCESS(rv, rv);

	if (!mHaveQuitApp) {
		nsCOMPtr<nsIDOMWindow> win;
		rv = ww->OpenWindow(nsnull,
		                    "chrome://leakmonitor/content/leakAlert.xul",
		                    nsnull, nsnull, report, getter_AddRefs(win));
		NS_ENSURE_SUCCESS(rv, rv);
	} else {
		// During shutdown, just print to standard output.
		PRUnichar *reportText;
		rv = report->GetReportText(&reportText);
		NS_ENSURE_SUCCESS(rv, rv);
		printf("\nLeakReport:\n%s\n", NS_ConvertUTF16toUTF8(reportText).get());
		nsMemory::Free(reportText);
	}

	return NS_OK;
}
