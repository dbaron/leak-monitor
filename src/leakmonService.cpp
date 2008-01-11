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
#include "nsIXULAppInfo.h"

// Frozen APIs that require linking against NSPR.
#include "plstr.h"

// XPCOM glue APIs
#include "nsDebug.h"
#include "nsMemory.h"
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsStringAPI.h"

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
leakmonService::GetBuildHasCycleCollector(PRBool *aResult)
{
	nsresult rv;
	nsCOMPtr<nsIXULAppInfo> appInfo =
		do_GetService("@mozilla.org/xre/app-info;1", &rv);
	NS_ENSURE_SUCCESS(rv, rv);

	nsCString versionStr, buildidStr;
	appInfo->GetPlatformVersion(versionStr);
	appInfo->GetPlatformBuildID(buildidStr);

	// We support a minimum of the 1.8 branch.  If we're on the 1.8
	// branch, the result should be false.  Otherwise, it should be true
	// if the build ID is at least 2007010415 (when the cycle collector
	// landed).  But, actually, we need the cycle-collector-begin
	// notification which landed starting in builds 2007051014.  So act
	// like the cycle collector wasn't present in builds between those
	// dates, since the failure mode of being way too noisy is better
	// than the failure mode of being way too quiet (for a tool like
	// this, I think).
	const char *version = versionStr.get(), *buildid = buildidStr.get();
	*aResult = (version[0] != '1' || version[1] != '.' || version[2] != '8' ||
			    ('0' <= version[3] && version[3] <= '9')) &&
		       PL_strcmp(buildid, "2007051014") >= 0;
	return NS_OK;
}

NS_IMETHODIMP
leakmonService::Observe(nsISupports *aSubject, const char *aTopic,
                        const PRUnichar *aData)
{
	if (!PL_strcmp(aTopic, gQuitApplicationTopic)) {
		mHaveQuitApp = PR_TRUE;
	} else if (!PL_strcmp(aTopic, APPSTARTUP_TOPIC)) {
	} else if (!PL_strcmp(aTopic, "cycle-collector-begin")) {
		// We want to call DidGC once cycle collection is done.  So
		// we'll make a timer.
		if (!mTimer) {
			mTimer = do_CreateInstance(NS_TIMER_CONTRACTID);
			mTimer->Init(this, 0, nsITimer::TYPE_ONE_SHOT);
		}
	} else if (!PL_strcmp(aTopic, NS_TIMER_CALLBACK_TOPIC)) {
		mTimer = nsnull;
		DidGC();
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

	nsCOMPtr<nsIObserverService> os =
		do_GetService("@mozilla.org/observer-service;1", &rv);
	NS_ENSURE_SUCCESS(rv, rv);

	rv = os->AddObserver(this, gQuitApplicationTopic, PR_TRUE);
	NS_ENSURE_SUCCESS(rv, rv);

	PRBool buildHasCycleCollector;
	rv = GetBuildHasCycleCollector(&buildHasCycleCollector);
	NS_ENSURE_SUCCESS(rv, rv);

	if (buildHasCycleCollector) {
		rv = os->AddObserver(this, "cycle-collector-begin", PR_TRUE);
		NS_ENSURE_SUCCESS(rv, rv);
	} else {
		gNextGCCallback = JS_SetGCCallbackRT(mJSRuntime, GCCallback);
	}

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

// Only create entries if they don't have a Components object.
struct JSScopeInfoEntry : public PLDHashEntryHdr {
	JSObject *global; // key must be first to match PLDHashEntryStub
	PLDHashTable roots;
	PRUint32 prevRootCount;
	PRPackedBool generation; // we let it wrap at one bit
	PRPackedBool notified;
};

struct RootsEntry : public PLDHashEntryHdr {
	void *key; // key must be first to match PLDHashEntryStub
};

/* static */ PLDHashOperator PR_CALLBACK
leakmonService::ReportLeaks(PLDHashTable *table, PLDHashEntryHdr *hdr,
                            PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = static_cast<JSScopeInfoEntry*>(hdr);
	leakmonService *service = static_cast<leakmonService*>(arg);

	if (!entry->notified && entry->roots.entryCount) {
		entry->notified = PR_TRUE;
		service->NotifyLeaks(entry->global, leakmonIReport::NEW_LEAKS);
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
		NotifyLeaks(static_cast<JSObject*>(mReclaimWindows.ElementAt(i)),
		            leakmonIReport::RECLAIMED_LEAKS);
	}
	mReclaimWindows.Clear();
}

void
leakmonService::HandleRoot(JSObject *aRoot, PRBool *aHaveLeaks)
{
	JSObject *global, *parent = aRoot;
	do {
		global = parent;
		parent = JS_GetParent(mJSContext, global);
	} while (parent);

	JSClass *clazz = JS_GET_CLASS(mJSContext, global);

	if (PL_strcmp(clazz->name, "Window") != 0 &&
	    PL_strcmp(clazz->name, "ChromeWindow") != 0) {
	    // Global is not a window.
	    return;
	}

	jsval comp;
	PRBool hasComponents =
		JS_GetProperty(mJSContext, global, "Components", &comp) &&
		JS_TypeOfValue(mJSContext, comp) == JSTYPE_OBJECT;

	if (hasComponents) {
		// It's still live, so don't worry about leaks.
		return;
	}

	JSScopeInfoEntry *entry = static_cast<JSScopeInfoEntry*>(
		PL_DHashTableOperate(&mJSScopeInfo, global, PL_DHASH_ADD));
	if (!entry) {
		NS_WARNING("out of memory");
		return;
	}

	if (!entry->global) {
		entry->global = global;
		entry->prevRootCount = PR_UINT32_MAX;
		PL_DHashTableInit(&entry->roots, PL_DHashGetStubOps(), nsnull,
		                  sizeof(RootsEntry), 16);
	}
	entry->generation = mGeneration;

	RootsEntry *rootEntry = static_cast<RootsEntry*>(
		PL_DHashTableOperate(&entry->roots, aRoot, PL_DHASH_ADD));
	if (!rootEntry) {
		NS_WARNING("out of memory");
		return;
	}
	NS_ASSERTION(rootEntry->key == nsnull || rootEntry->key == aRoot,
	             "wrong entry");
	rootEntry->key = aRoot;
	if (!entry->notified) {
		*aHaveLeaks = PR_TRUE;
	}
}

struct FindGCRootData {
	leakmonService *service;
	PRBool haveLeaks;
};

struct TracerWithData : public JSTracer {
	FindGCRootData *data;
};

/* static */ void JS_DLL_CALLBACK
leakmonService::GCRootTracer(JSTracer *trc, void *thing, uint32 kind)
{
	FindGCRootData *data = static_cast<TracerWithData*>(trc)->data;

	if (kind == 0 /* JSTRACE_OBJECT */ ||
	    kind == 4 /* JSTRACE_NAMESPACE */ ||
	    kind == 5 /* JSTRACE_QNAME */ ||
	    kind == 6 /* JSTRACE_XML */)
		data->service->HandleRoot(static_cast<JSObject*>(thing),
		                          &data->haveLeaks);
}

/* static */ intN JS_DLL_CALLBACK
leakmonService::GCRootMapper(void *rp, const char *name, void *aData)
{
	FindGCRootData *data = static_cast<FindGCRootData*>(aData);

	jsval *vp = static_cast<jsval*>(rp);
	jsval v = *vp;
	if (!JSVAL_IS_PRIMITIVE(v))
		data->service->HandleRoot(JSVAL_TO_OBJECT(v), &data->haveLeaks);

	return JS_MAP_GCROOT_NEXT;
}

/* static */ PLDHashOperator PR_CALLBACK
leakmonService::ResetRootedLists(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                 PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = static_cast<JSScopeInfoEntry*>(hdr);
	entry->prevRootCount = entry->roots.entryCount;
	PL_DHashTableFinish(&entry->roots);
	PL_DHashTableInit(&entry->roots, PL_DHashGetStubOps(), nsnull,
	                  sizeof(RootsEntry), 16);
	return PL_DHASH_NEXT;
}

/* static */ PLDHashOperator PR_CALLBACK
leakmonService::RemoveDeadScopes(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                 PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = static_cast<JSScopeInfoEntry*>(hdr);
	leakmonService *service = static_cast<leakmonService*>(arg);
	if (entry->generation != service->mGeneration) {
		if (entry->notified) {
			service->mReclaimWindows.AppendElement(entry->global);
		}
		return PL_DHASH_REMOVE;
	}
	if (entry->notified &&
	    entry->prevRootCount != entry->roots.entryCount) {
		service->mReclaimWindows.AppendElement(entry->global);
	}
	return PL_DHASH_NEXT;
}

/* static */ PLDHashOperator PR_CALLBACK
leakmonService::FindNeedForNewGC(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                 PRUint32 number, void *arg)
{
	JSScopeInfoEntry *entry = static_cast<JSScopeInfoEntry*>(hdr);
	PRBool *needNewGC = static_cast<PRBool*>(arg);
	if (!entry->notified) {
		PRUint32 count = entry->roots.entryCount;
		if (count != entry->prevRootCount) {
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

	// Find all GC roots in closed windows.
	FindGCRootData data;
	data.service = this;
	data.haveLeaks = PR_FALSE;
	{
		JSAutoRequest ar(mJSContext);

		TracerWithData trc;
		trc.context = mJSContext;
		trc.callback = GCRootTracer;
		trc.debugPrinter = NULL;
		trc.debugPrintArg = NULL;
		trc.debugPrintIndex = (size_t)-1;
		trc.data = &data;
		JS_TraceRuntime(&trc);
	}

	PRBool haveLeaks = data.haveLeaks;

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

/* static */ PLDHashOperator PR_CALLBACK
leakmonService::AppendToArray(PLDHashTable *table, PLDHashEntryHdr *hdr,
                              PRUint32 number, void *arg)
{
	nsVoidArray *array = static_cast<nsVoidArray*>(arg);
	RootsEntry *entry = static_cast<RootsEntry*>(hdr);

	array->AppendElement(entry->key);

	return PL_DHASH_NEXT;
}

nsresult
leakmonService::NotifyLeaks(JSObject *aGlobalObject, NotifyType aType)
{
	nsresult rv;

	nsCOMPtr<nsIWindowWatcher> ww =
		do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
	NS_ENSURE_SUCCESS(rv, rv);

	JSScopeInfoEntry *entry = static_cast<JSScopeInfoEntry*>(
		PL_DHashTableOperate(&mJSScopeInfo, aGlobalObject, PL_DHASH_LOOKUP));
	nsVoidArray leaks;
	if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
		PL_DHashTableEnumerate(&entry->roots, AppendToArray, &leaks);
	} else {
		NS_ASSERTION(aType != leakmonIReport::NEW_LEAKS,
		             "should be in table");
	}

	leakmonReport *report = new leakmonReport();
	NS_ENSURE_TRUE(report, NS_ERROR_OUT_OF_MEMORY);
	nsCOMPtr<leakmonIReport> reportI = report;
	rv = report->Init(aGlobalObject, aType, leaks);
	NS_ENSURE_SUCCESS(rv, rv);

	if (!mHaveQuitApp) {
		const char dialogURL[] = "chrome://leakmonitor/content/leakAlert.xul";
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
		       (aType == leakmonIReport::NEW_LEAKS) ? "new" : "reclaimed",
		       NS_ConvertUTF16toUTF8(reportText).get());
		nsMemory::Free(reportText);
	}

	return NS_OK;
}
