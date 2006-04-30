/* vim: set shiftwidth=4 tabstop=8 autoindent cindent expandtab: */
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

// ???
#include "nsIAppStartupNotifier.h"

/* static */ nsLeakMonitorService* nsLeakMonitorService::gService = nsnull;
/* static */ JSGCCallback nsLeakMonitorService::gNextGCCallback = nsnull;


nsLeakMonitorService::nsLeakMonitorService()
{
    NS_ASSERTION(gService == nsnull, "duplicate service creation");

    mJSContextInfo.ops = nsnull;

    gService = this;
}

nsLeakMonitorService::~nsLeakMonitorService()
{
    FreeContextInfo();
    mJSRuntimeService = nsnull;
    gService = nsnull;
}

NS_IMPL_ISUPPORTS1(nsLeakMonitorService, nsIObserver)

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
    NS_ASSERTION(!mJSRuntimeService, "Init being called twice");

    nsresult rv;

    mJSRuntimeService =
        do_GetService("@mozilla.org/js/xpc/RuntimeService;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    JSRuntime *rt;
    rv = mJSRuntimeService->GetRuntime(&rt);
    NS_ENSURE_SUCCESS(rv, rv);

    gNextGCCallback = JS_SetGCCallbackRT(rt, GCCallback);

    return NS_OK;
}

/* static*/ nsresult
nsLeakMonitorService::Create(nsLeakMonitorService **aResult)
{
    if (gService) {
        NS_ADDREF(*aResult = gService);
        return NS_OK;
    }

    *aResult = nsnull;
    nsLeakMonitorService *result = new nsLeakMonitorService();
    NS_ENSURE_TRUE(result, NS_ERROR_OUT_OF_MEMORY);
    NS_ADDREF(result);

    nsresult rv = result->Init();
    if (NS_FAILED(rv)) {
        NS_RELEASE(result);
        return rv;
    }

    *aResult = result;
    return NS_OK;
}

/* static */ JSBool
nsLeakMonitorService::GCCallback(JSContext *cx, JSGCStatus status)
{
    JSBool result = gNextGCCallback ? gNextGCCallback(cx, status) : JS_TRUE;

    if (gService && status == JSGC_END) {
        gService->DidGC(JS_GetRuntime(cx));
    }

    return result;
}

struct JSContextInfoEntry : public PLDHashEntryStub {
};

void
nsLeakMonitorService::DidGC(JSRuntime *rt)
{
    FreeContextInfo();
    NS_ASSERTION(!mJSContextInfo.ops, "FreeContextInfo didn't work");
    PL_DHashTableInit(&mJSContextInfo, PL_DHashGetStubOps(), nsnull,
                      sizeof(JSContextInfoEntry), 32);

    // Use JS_ContextIterator to figure out which contexts are still
    // live XPConnect contexts (Components property on global?).
    JSContext *iter = nsnull;
    while (JS_ContextIterator(rt, &iter)) {
        // ...
    }

    // XXX Use JS_MapGCRoots to find wrapped JS in each context
    
    // Use JS_Enumerate or JS_NewPropertyIterator to find the variables
    // that point to wrapped natives???

}

void
nsLeakMonitorService::FreeContextInfo()
{
    if (mJSContextInfo.ops) {
        PL_DHashTableFinish(&mJSContextInfo);
        mJSContextInfo.ops = nsnull;
    }
}
