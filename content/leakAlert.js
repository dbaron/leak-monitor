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

const CC = Components.classes;
const CI = Components.interfaces;

var gTreeView = new XULTreeView();

function getReport()
{
	return window.arguments[0].QueryInterface(CI.leakmonIReport);
}

function LeakAlertOnLoad()
{
	document.getElementById("lwjs-tree").view = gTreeView;

	var report = getReport();
	var reason;
	switch (report.reason) {
		case CI.leakmonIReport.NEW_LEAKS:
			reason = "leak";
			break;
		case CI.leakmonIReport.RECLAIMED_LEAKS:
			reason = "reclaim"
			break;
		default:
			throw "Unknown report type";
	}

	/* change the dialog so it shows appropriate UI for the report type */
	document.title = document.documentElement.getAttribute("title." + reason) +
	                 " (" + report.ident + ")";
	document.getElementById("desc." + reason).removeAttribute("collapsed");

	/* populate the tree from the report */
	var lwjss = report.getLeakedWrappedJSs({});

	for each (var lwjs in lwjss) {
		gTreeView.childData.appendChild(new JSObjectRecord(
			"[leaked object]", lwjs));
	}
}

function LeakAlertOnUnload()
{
}

function LeakAlertCopyReport()
{
	var text = getReport().reportText;
	var clipboard = CC["@mozilla.org/widget/clipboardhelper;1"]
		                .getService(CI.nsIClipboardHelper);
	clipboard.copyString(text);
}

function JSObjectRecord(name, lwjs)
{
	this.lwjs = lwjs;

	this.setColumnPropertyName("name", "name");
	this.setColumnPropertyName("filename", "fileName");
	this.setColumnPropertyName("linestart", "lineStart");
	this.setColumnPropertyName("lineend", "lineEnd");
	this.setColumnPropertyName("string", "stringRep");

	/* we have to set these *after* calling setColumnPropertyName */
	this.name = name;
	for each (var m in
	          ["fileName", "lineStart", "lineEnd", "stringRep"]) {
		this[m] = lwjs[m];
	}
	if (!this.fileName) {
		this.lineStart = "";
		this.lineEnd = "";
	}
	/* collapse whitespace in stringRep */
	this.stringRep = this.stringRep.replace(/\s+/, " ");

	if (lwjs.numProperties > 0)
		this.reserveChildren(true);
}

JSObjectRecord.prototype = new XULTreeViewRecord(gTreeView.share);

JSObjectRecord.prototype.onPreOpen = function() {
	this.childData = new Array();
	for (var i = 0; i < this.lwjs.numProperties; ++i) {
		this.appendChild(new JSObjectRecord(
			this.lwjs.getPropertyNameAt(i), this.lwjs.getPropertyValueAt(i)));
	}
}
