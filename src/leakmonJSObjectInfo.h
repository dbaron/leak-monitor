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

/* Information about a JavaScript object that can be displayed in the UI. */

#ifndef leakmonJSObjectInfo_h_
#define leakmonJSObjectInfo_h_

// Code within this extension
#include "leakmonIJSObjectInfo.h"

// XPCOM glue APIs
#include "nsStringAPI.h"
#include "nsTArray.h"
#include "nsAutoPtr.h"
#include "nsDataHashtable.h"

// Frozen APIs
#include "jsapi.h"

class leakmonJSObjectInfo;
typedef nsDataHashtable<nsVoidPtrHashKey, leakmonJSObjectInfo*>
	leakmonObjectsInReportTable;

class leakmonJSObjectInfo : public leakmonIJSObjectInfo {
public:
	leakmonJSObjectInfo(jsval aJSValue) NS_HIDDEN;
	~leakmonJSObjectInfo() NS_HIDDEN;

	struct PropertyStruct {
		nsString mName;
		nsRefPtr<leakmonJSObjectInfo> mValue;
	};

	// For leakmonReport
	NS_HIDDEN_(nsresult) Init(leakmonObjectsInReportTable& aObjectsInReport);
	NS_HIDDEN_(jsval) GetJSValue() { return mJSValue; }
	NS_HIDDEN_(void) AppendSelfToString(nsString& aString);
	NS_HIDDEN_(PRBool) IsInitialized() const { return mIsInitialized; }
	NS_HIDDEN_(PropertyStruct&) PropertyStructAt(PRUint32 i) {
		return mProperties[PropertiesIndex(i)];
	}

	NS_DECL_ISUPPORTS
	NS_DECL_LEAKMONIJSOBJECTINFO

private:
	// We do NOT lock this, so if it is a GC thing, it cannot be
	// accessed.  However, we can still use it to determine the type.
	jsval mJSValue;
	PRBool mIsInitialized;

	nsTArray<PropertyStruct> mProperties;

	nsresult AppendProperty(jsid aID, JSContext *aCx,
	                        leakmonObjectsInReportTable &aObjectsInReport);

	PRUint32 PropertiesIndex(PRUint32 i) {
		// We store mProperties in backwards order.
		return mProperties.Length() - (i + 1);
	}

	nsString mFileName;
	PRUint32 mLineStart;
	PRUint32 mLineEnd;
	nsString mString;
};

#endif /* !defined(leakmonJSObjectInfo_h_) */
