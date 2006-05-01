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

// Internal includes
#include "nsLeakMonitorService.h"

// Frozen APIs
#include "nsICategoryManager.h"

// XPCOM glue APIs
#include "nsIGenericFactory.h"
#include "nsServiceManagerUtils.h"

// ???
#include "nsIAppStartupNotifier.h"

// 1ee1b3fc-e896-4ac9-870f-43d3a0581dc8
#define NS_LEAKMONITOR_SERVICE_CID \
{ 0x1ee1b3fc, 0xe896, 0x4ac9, \
	{ 0x87, 0x0f, 0x43, 0xd3, 0xa0, 0x58, 0x1d, 0xc8 } }

#define NS_LEAKMONITOR_SERVICE_CONTRACTID \
	"@dbaron.org/leak-monitor-service;1"

#define NS_LEAKMONITOR_SERVICE_ENTRY_NAME \
	"nsLeakMonitorService"

NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsLeakMonitorService, Init)

static NS_METHOD
RegisterServiceForStartup(nsIComponentManager *aCompMgr, nsIFile* aPath,
                          const char *aLoaderStr, const char *aType,
                          const nsModuleComponentInfo *aInfo)
{
	nsresult rv;
	nsCOMPtr<nsICategoryManager> catMan =
		do_GetService(NS_CATEGORYMANAGER_CONTRACTID, &rv);
	NS_ENSURE_SUCCESS(rv, rv);

	const char service[] = "service,";
	// sizeof() includes room for terminating null
	char *value = NS_STATIC_CAST(char*,
		NS_Alloc(sizeof(service) + strlen(aInfo->mContractID)));
	if (!value)
		return NS_ERROR_OUT_OF_MEMORY;
	strcpy(value, service);
	strcat(value, aInfo->mContractID);
	rv = catMan->AddCategoryEntry(APPSTARTUP_CATEGORY,
	                              aInfo->mContractID, value,
	                              PR_TRUE, PR_TRUE, nsnull);
	NS_Free(value);
	NS_ENSURE_SUCCESS(rv, rv);

	return NS_OK;
}

static NS_METHOD
UnregisterServiceForStartup(nsIComponentManager *aCompMgr, nsIFile *aPath,
                            const char *aLoaderStr,
                            const nsModuleComponentInfo *aInfo)
{
	nsresult rv;
	nsCOMPtr<nsICategoryManager> catMan =
		do_GetService(NS_CATEGORYMANAGER_CONTRACTID, &rv);
	NS_ENSURE_SUCCESS(rv, rv);

	rv = catMan->DeleteCategoryEntry(APPSTARTUP_CATEGORY,
	                                 aInfo->mContractID,
	                                 PR_TRUE);
	NS_ENSURE_SUCCESS(rv, rv);

	return NS_OK;
}


static const nsModuleComponentInfo components[] =
{
  { "nsLeakMonitorService",
	NS_LEAKMONITOR_SERVICE_CID,
	NS_LEAKMONITOR_SERVICE_CONTRACTID,
	nsLeakMonitorServiceConstructor,
	RegisterServiceForStartup,
	UnregisterServiceForStartup
  }
};

NS_IMPL_NSGETMODULE(nsLeakMonitorModule, components)
