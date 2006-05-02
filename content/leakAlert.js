/* vim: set shiftwidth=4 tabstop=4 autoindent noexpandtab copyindent: */
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

/* Dialog that comes up to tell user about leaks. */

const CI = Components.interfaces;

var gTreeView = new XULTreeView();

function LeakAlertOnLoad()
{
	document.getElementById("lwjs-tree").view = gTreeView;

	var lwjss = window.arguments[0].QueryInterface(CI.leakmonIReport)
		.getLeakedWrappedJSs({});

	for each (var lwjs in lwjss) {
		gTreeView.childData.appendChild(new JSObjectRecord(lwjs));
	}
}

function LeakAlertOnUnload()
{
}

function JSObjectRecord(lwjs)
{
	this.lwjs = lwjs;

	this.setColumnPropertyName("name", "name");
	this.setColumnPropertyName("filename", "fileName");
	this.setColumnPropertyName("linestart", "lineStart");
	this.setColumnPropertyName("lineend", "lineEnd");
	this.setColumnPropertyName("string", "stringRep");
	for each (var m in
	          ["name", "fileName", "lineStart", "lineEnd", "stringRep"]) {
		this[m] = lwjs[m];
	}
	if (!this.fileName) {
		this.lineStart = "";
		this.lineEnd = "";
	}

	if (lwjs.numProperties > 0)
		this.reserveChildren(true);
}

JSObjectRecord.prototype = new XULTreeViewRecord(gTreeView.share);

JSObjectRecord.prototype.onPreOpen = function() {
	this.childData = new Array();
	for (var i = 0; i < this.lwjs.numProperties; ++i) {
		this.appendChild(new JSObjectRecord(this.lwjs.getPropertyAt(i)));
	}
}