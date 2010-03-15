/* vim: set shiftwidth=4 tabstop=4 autoindent cindent noexpandtab copyindent: */
/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   John Bandhauer <jband@netscape.com>
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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
#ifndef nsIJSRuntimeServiceShim_h_
#define nsIJSRuntimeServiceShim_h_

#include "nsISupports.h"

class nsIXPCScriptable; /* forward declaration */

/* from 1.9.2 */
#define NS_IJSRUNTIMESERVICE_v1_IID \
  {0xe7d09265, 0x4c23, 0x4028, \
    { 0xb1, 0xb0, 0xc9, 0x9e, 0x02, 0xaa, 0x78, 0xf8 }}

/* from 1.9.3a3 */
#define NS_IJSRUNTIMESERVICE_v2_IID \
  {0x364bcec3, 0x7034, 0x4a4e, \
    { 0xbf, 0xf5, 0xb3, 0xf7, 0x96, 0xca, 0x97, 0x71 }}

/**
 * The nsIJSRuntimeService interface as it existed from Mozilla 1.8 to
 * Mozilla 1.9.2, inclusive.
 *
 * It changed in 1.9.3 to add additional methods to the end.
 */
class NS_NO_VTABLE nsIJSRuntimeServiceShim : public nsISupports {
 public: 

  /* readonly attribute JSRuntime runtime; */
  NS_IMETHOD GetRuntime(JSRuntime * *aRuntime) = 0;

  /* readonly attribute nsIXPCScriptable backstagePass; */
  NS_IMETHOD GetBackstagePass(nsIXPCScriptable * *aBackstagePass) = 0;

};

#endif /* !defined(nsIJSRuntimeServiceShim_h_) */
