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

#ifndef nsLeakMonitorService_h_
#define nsLeakMonitorService_h_

// Code within this extension
#include "nsILeakMonitorService.h"

// Frozen APIs
#include "nsISupports.h"
#include "nsIObserver.h"

// Frozen APIs that require linking against JS.
#include "jsapi.h"

// Unfrozen APIs (XXX should unroll these, per-version)
#include "nsIJSRuntimeService.h"

// XPCOM glue APIs
#include "nsCOMPtr.h"
#include "pldhash.h"

class nsLeakMonitorService : public nsILeakMonitorService,
                             public nsIObserver {
public:
	nsLeakMonitorService() NS_HIDDEN;
	~nsLeakMonitorService() NS_HIDDEN;

	// For nsLeakMonitorModule
	nsresult Init();

	NS_DECL_ISUPPORTS
	NS_DECL_NSILEAKMONITORSERVICE
	NS_DECL_NSIOBSERVER

private:
	static nsLeakMonitorService *gService;
	static JSGCCallback gNextGCCallback;

	static JSBool GCCallback(JSContext *cx, JSGCStatus status);
	void DidGC();
	nsresult BuildContextInfo();
	nsresult EnsureContextInfo();

	nsCOMPtr<nsIJSRuntimeService> mJSRuntimeService;
	JSRuntime *mJSRuntime;
	PLDHashTable mJSScopeInfo;

	PRPackedBool mGeneration; // let it wrap after 1 bit, since that's all that's needed
};

#endif /* !defined(nsLeakMonitorService_h_) */
