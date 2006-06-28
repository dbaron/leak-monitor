vim: set shiftwidth=8 tabstop=8 autoindent noexpandtab textwidth=72:
***** BEGIN LICENSE BLOCK *****
Version: MPL 1.1/GPL 2.0/LGPL 2.1

The contents of this file are subject to the Mozilla Public License Version 
1.1 (the "License"); you may not use this file except in compliance with 
the License. You may obtain a copy of the License at 
http://www.mozilla.org/MPL/

Software distributed under the License is distributed on an "AS IS" basis,
WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
for the specific language governing rights and limitations under the
License.

The Original Code is the leak-monitor extension.

The Initial Developer of the Original Code is the Mozilla Foundation.
Portions created by the Initial Developer are Copyright (C) 2006
the Initial Developer. All Rights Reserved.

Contributor(s):
  L. David Baron <dbaron@dbaron.org>, Mozilla Corporation (original author)

Alternatively, the contents of this file may be used under the terms of
either the GNU General Public License Version 2 or later (the "GPL"), or
the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
in which case the provisions of the GPL or the LGPL are applicable instead
of those above. If you wish to allow use of your version of this file only
under the terms of either the GPL or the LGPL, and not to allow others to
use your version of this file under the terms of the MPL, indicate your
decision by deleting the provisions above and replace them with the notice
and other provisions required by the GPL or the LGPL. If you do not delete
the provisions above, a recipient may use your version of this file under
the terms of any one of the MPL, the GPL or the LGPL.

***** END LICENSE BLOCK *****

Build instructions for leak-monitor extension.

For each platform:

	In a source tree from the MOZILLA_1_8_0_BRANCH, extract the
	source tarball inside of mozilla/extensions/

	Build that tree (at least enough so that you've run the export
	phase for the tier that has layout).  Do this with an optimized,
	non-debug build.

	Change directory to <objdir>/extensions/

	Execute the following commands (if your objdir is your srcdir,
	you need to do the first one considerably earlier!):
		rm -rf leak-monitor ../dist/xpi-stage/leak*
		make MOZ_EXTENSIONS=leak-monitor

	You now have the xpi for that platform in
	<objdir>/dist/xpi-stage/leakmonitor-<version>.xpi

To combine the platform xpis into a single multi-platform XPI, copy them
to the same machine (and not all to the same filename!), and run
	mozilla/extensions/leak-monitor/combine-platform-xpis.sh \
		leakmonitor-<version>.xpi \
		linux-leakmonitor-<version>.xpi \
		windows-leakmonitor-<version>.xpi \
		mac-ppc-leakmonitor-<version>.xpi \
		mac-x86-leakmonitor-<version>.xpi \
(The order doesn't really matter, but I prefer putting a linux one first
for saner newlines.)
