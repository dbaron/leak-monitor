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

#ifndef leakmonService_h_
#define leakmonService_h_

// Code within this extension
#include "leakmonIService.h"

// Frozen APIs
#include "nsISupports.h"
#include "nsIObserver.h"

// Frozen APIs that require linking against JS.
#include "jsapi.h"

// Frozen APIs that require linking against NSPR.
#include "prthread.h"

// Unfrozen APIs (XXX should unroll these, per-version)
#include "nsIJSRuntimeService.h"
#include "nsITimer.h"

// XPCOM glue APIs in 1.8
#include "nsCOMPtr.h"
#include "pldhash.h"
#include "nsWeakReference.h"

// XPCOM glue APIs in 1.9
#include "nsVoidArray.h"

class leakmonService : public leakmonIService,
                       public nsIObserver,
                       public nsSupportsWeakReference {
public:
	leakmonService() NS_HIDDEN;
	~leakmonService() NS_HIDDEN;

	// For leakmonModule
	NS_HIDDEN_(nsresult) Init();

	NS_DECL_ISUPPORTS
	NS_DECL_LEAKMONISERVICE
	NS_DECL_NSIOBSERVER

	static inline NS_HIDDEN_(JSContext*) GetJSContext() {
		if (!gService)
			return nsnull;
		return gService->mJSContext;
	}

private:
	static NS_HIDDEN_(leakmonService) *gService;

	PR_STATIC_CALLBACK(PLDHashOperator)
		ReportLeaks(PLDHashTable *table, PLDHashEntryHdr *hdr,
		            PRUint32 number, void *arg);
	void HandleRoot(JSObject *aRoot, PRBool *aHaveLeaks);
	JS_STATIC_DLL_CALLBACK(void)
		GCRootTracer(JSTracer *trc, void *thing, uint32 kind);

	PR_STATIC_CALLBACK(PLDHashOperator)
		ResetRootedLists(PLDHashTable *table, PLDHashEntryHdr *hdr,
		                 PRUint32 number, void *arg);
	PR_STATIC_CALLBACK(PLDHashOperator)
		RemoveDeadScopes(PLDHashTable *table, PLDHashEntryHdr *hdr,
		                 PRUint32 number, void *arg);
	PR_STATIC_CALLBACK(PLDHashOperator)
		FindNeedForNewGC(PLDHashTable *table, PLDHashEntryHdr *hdr,
		                 PRUint32 number, void *arg);

	NS_HIDDEN_(void) DidGC();
	NS_HIDDEN_(nsresult) BuildContextInfo();

	typedef PRUint32 NotifyType; // specifically, the reason constants
	                             // on leakmonIReport
	NS_HIDDEN_(nsresult) NotifyLeaks(JSObject *aGlobalObject, NotifyType aType);
	PR_STATIC_CALLBACK(PLDHashOperator)
		AppendToArray(PLDHashTable *table, PLDHashEntryHdr *hdr,
		              PRUint32 number, void *arg);

	nsCOMPtr<nsIJSRuntimeService> mJSRuntimeService;
	JSRuntime *mJSRuntime;
	JSContext *mJSContext; // some JS API functions require a context, and
	                       // it seems safer to have our own than pass
	                       // somebody else's.
	PLDHashTable mJSScopeInfo;
	PRThread *mMainThread;
	nsCOMPtr<nsITimer> mTimer;
	nsVoidArray mReclaimWindows;

	PRPackedBool mGeneration; // let it wrap after 1 bit, since that's all that's needed
	PRPackedBool mHaveQuitApp;
	PRPackedBool mNesting;
};

#endif /* !defined(leakmonService_h_) */
