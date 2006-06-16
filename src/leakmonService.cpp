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

// Unfrozen APIs that shouldn't hurt
// filed https://bugzilla.mozilla.org/show_bug.cgi?id=335977
#include "nsIAppStartupNotifier.h"

/* static */ leakmonService* leakmonService::gService = nsnull;
/* static */ JSGCCallback leakmonService::gNextGCCallback = nsnull;

static const char gQuitApplicationTopic[] = "quit-application";

#define NS_SUCCESS_NEED_NEW_GC \
	NS_ERROR_GENERATE_SUCCESS(NS_ERROR_MODULE_GENERAL, 1)
#define NS_SUCCESS_REPORT_LEAKS \
	NS_ERROR_GENERATE_SUCCESS(NS_ERROR_MODULE_GENERAL, 2)

leakmonService::leakmonService()
	: mJSRuntime(nsnull)
	, mGeneration(0)
	, mHaveQuitApp(PR_FALSE)
	, mNesting(PR_FALSE)
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
	nsVoidArray rootedXPCWJSs;
	PRInt32 prevRootedXPCWJSCount;
	PRPackedBool generation; // we let it wrap at one bit
	PRPackedBool hasComponents;
	PRPackedBool notified;
};

/* static */ PLDHashOperator PR_CALLBACK
leakmonService::ReportLeaks(PLDHashTable *table, PLDHashEntryHdr *hdr,
                            PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*, hdr);
	leakmonService *service = NS_STATIC_CAST(leakmonService*, arg);

	if (!entry->hasComponents && !entry->notified &&
	    entry->rootedXPCWJSs.Count()) {
		entry->notified = PR_TRUE;
		service->NotifyLeaks(entry->global, leakmonService::NEW_LEAKS);
	}

	return PL_DHASH_NEXT;
}

void
leakmonService::DidGC()
{
	if (mNesting)
		return;

	nsresult rv;
	for (;;) {
		rv = BuildContextInfo();
		if (NS_FAILED(rv)) {
			NS_ERROR("BuildContextInfo failed");
			// not sure how to handle this
			return;
		}
		if (rv != NS_SUCCESS_NEED_NEW_GC)
			break;
		mNesting = PR_TRUE;
		::JS_GC(mJSContext);
		mNesting = PR_FALSE;
	}

	if (rv == NS_SUCCESS_REPORT_LEAKS) {
		PL_DHashTableEnumerate(&mJSScopeInfo, ReportLeaks, this);
	}
	PRInt32 i = mReclaimWindows.Count();
	while (i > 0) {
		--i;
		NotifyLeaks(NS_STATIC_CAST(JSObject*, mReclaimWindows.ElementAt(i)),
		            RECLAIMED_LEAKS);
	}
}

/* static */ intN JS_DLL_CALLBACK
leakmonService::FindXPCGCRoots(void *rp, const char *name, void *data)
{
	nsVoidArray *array = NS_STATIC_CAST(nsVoidArray*, data);

	static const char wrapped_js_root_name[] = "nsXPCWrappedJS::mJSObj";
	if (!strncmp(name, wrapped_js_root_name, sizeof(wrapped_js_root_name)-1)) {
		PRBool ok = array->AppendElement(*NS_STATIC_CAST(JSObject**, rp));
		NS_ASSERTION(ok, "not handling this out of memory case");
	}
	return JS_MAP_GCROOT_NEXT;
}

/* static */ PLDHashOperator PR_CALLBACK
leakmonService::ResetRootedLists(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                 PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*, hdr);
	entry->prevRootedXPCWJSCount = entry->rootedXPCWJSs.Count();
	entry->rootedXPCWJSs.Clear();
	return PL_DHASH_NEXT;
}

/* static */ PLDHashOperator PR_CALLBACK
leakmonService::RemoveDeadScopes(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                 PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*, hdr);
	leakmonService *service = NS_STATIC_CAST(leakmonService*, arg);
	if (entry->generation != service->mGeneration) {
		if (entry->notified) {
			service->mReclaimWindows.AppendElement(entry->global);
		}
		return PL_DHASH_REMOVE;
	}
	if (entry->notified &&
	    entry->prevRootedXPCWJSCount != entry->rootedXPCWJSs.Count()) {
		service->mReclaimWindows.AppendElement(entry->global);
	}
	return PL_DHASH_NEXT;
}

/* static */ PLDHashOperator PR_CALLBACK
leakmonService::FindNeedForNewGC(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                 PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*, hdr);
	PRBool *needNewGC = NS_STATIC_CAST(PRBool*, arg);
	if (!entry->hasComponents && !entry->notified) {
		PRInt32 count = entry->rootedXPCWJSs.Count();
		if (count != entry->prevRootedXPCWJSCount) {
			*needNewGC = PR_TRUE;
			return PL_DHASH_STOP;
		}
	}
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

	PL_DHashTableEnumerate(&mJSScopeInfo, ResetRootedLists, nsnull);

	// Find all the XPConnect wrapped JavaScript objects that are rooted
	// (i.e., owned by native code).
	nsVoidArray xpcGCRoots; // of JSObject*
	JS_MapGCRoots(mJSRuntime, FindXPCGCRoots, &xpcGCRoots);

	PRBool haveLeaks = PR_FALSE;

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

		if (!entry->global) {
			entry->global = global;
			entry->prevRootedXPCWJSCount = PR_UINT32_MAX;
		}
		entry->generation = mGeneration;

		jsval comp;
		entry->hasComponents =
			JS_GetProperty(mJSContext, global, "Components", &comp) &&
			JS_TypeOfValue(mJSContext, comp) == JSTYPE_OBJECT;

		entry->rootedXPCWJSs.AppendElement(rootedObj);
		if (!entry->hasComponents && !entry->notified) {
			haveLeaks = PR_TRUE;
		}
	}

	PRUint32 oldCount = mJSScopeInfo.entryCount;
	PL_DHashTableEnumerate(&mJSScopeInfo, RemoveDeadScopes, this);

	if (!haveLeaks)
		return NS_OK;

	PRBool needNewGC = oldCount != mJSScopeInfo.entryCount;
	if (!needNewGC)
		PL_DHashTableEnumerate(&mJSScopeInfo, FindNeedForNewGC, &needNewGC);

	if (needNewGC)
		return NS_SUCCESS_NEED_NEW_GC;
	return NS_SUCCESS_REPORT_LEAKS;

	// XXX Look at wrapped natives too, perhaps?
	// Maybe use JS_Enumerate or JS_NewPropertyIterator to find the
	// variables that point to wrapped natives???
}

nsresult
leakmonService::NotifyLeaks(JSObject *aGlobalObject, NotifyType aType)
{
	nsresult rv;

	nsCOMPtr<nsIWindowWatcher> ww =
		do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
	NS_ENSURE_SUCCESS(rv, rv);

	JSScopeInfoEntry *entry = NS_STATIC_CAST(JSScopeInfoEntry*,
		PL_DHashTableOperate(&mJSScopeInfo, aGlobalObject, PL_DHASH_LOOKUP));
	nsVoidArray empty;
	const nsVoidArray *leaks;
	if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
		leaks = &entry->rootedXPCWJSs;
	} else {
		NS_ASSERTION(aType != NEW_LEAKS, "should be in table");
		leaks = &empty;
	}

	leakmonReport *report = new leakmonReport();
	NS_ENSURE_TRUE(report, NS_ERROR_OUT_OF_MEMORY);
	nsCOMPtr<leakmonIReport> reportI = report;
	rv = report->Init(aGlobalObject, *leaks);
	NS_ENSURE_SUCCESS(rv, rv);

	const char *dialogURL;
	switch (aType) {
		case NEW_LEAKS:
			dialogURL = "chrome://leakmonitor/content/leakAlert.xul";
			break;
		case RECLAIMED_LEAKS:
			dialogURL = "chrome://leakmonitor/content/reclaimedLeakAlert.xul";
			break;
		default:
			NS_ERROR("unknown type");
			return NS_ERROR_UNEXPECTED;
	}

	if (!mHaveQuitApp) {
		nsCOMPtr<nsIDOMWindow> win;
		rv = ww->OpenWindow(nsnull, dialogURL,
		                    nsnull, nsnull, report, getter_AddRefs(win));
		NS_ENSURE_SUCCESS(rv, rv);
	} else {
		// During shutdown, just print to standard output.
		PRUnichar *reportText;
		rv = report->GetReportText(&reportText);
		NS_ENSURE_SUCCESS(rv, rv);
		printf("\nLeakReport (%s leaks):\n%s\n",
		       (aType == NEW_LEAKS) ? "new" : "reclaimed",
		       NS_ConvertUTF16toUTF8(reportText).get());
		nsMemory::Free(reportText);
	}

	return NS_OK;
}
