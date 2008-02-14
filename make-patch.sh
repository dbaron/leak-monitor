#!/bin/bash

TMPDIR="$(mktemp -d)" || exit 1
pushd "$TMPDIR" > /dev/null 2>&1 || exit 1
cvs -d':pserver:anonymous@cvs-mirror.mozilla.org:/cvsroot' co mozilla/browser/confvars.sh mozilla/browser/installer/windows/packages-static mozilla/browser/installer/unix/packages-static > /dev/null 2>&1 || exit 1
cd mozilla || exit 1
perl -pi -e 's/MOZ_EXTENSIONS_DEFAULT=" /MOZ_EXTENSIONS_DEFAULT=" leak-monitor /' browser/confvars.sh || exit 1
cat >> browser/installer/windows/packages-static <<EOM
bin\\extensions\\{1ed6b678-1f93-4660-a9c5-01af87b323d3}\\components\\leakmon.dll
bin\\extensions\\{1ed6b678-1f93-4660-a9c5-01af87b323d3}\\components\\leakmonitor.xpt
bin\\extensions\\{1ed6b678-1f93-4660-a9c5-01af87b323d3}\\chrome\\leakmon.jar
bin\\extensions\\{1ed6b678-1f93-4660-a9c5-01af87b323d3}\\chrome.manifest
bin\\extensions\\{1ed6b678-1f93-4660-a9c5-01af87b323d3}\\install.rdf
EOM
cat >> browser/installer/unix/packages-static <<EOM
bin/extensions/{1ed6b678-1f93-4660-a9c5-01af87b323d3}/chrome.manifest
bin/extensions/{1ed6b678-1f93-4660-a9c5-01af87b323d3}/components/leakmonitor.xpt
bin/extensions/{1ed6b678-1f93-4660-a9c5-01af87b323d3}/components/libleakmon.so
bin/extensions/{1ed6b678-1f93-4660-a9c5-01af87b323d3}/install.rdf
bin/extensions/{1ed6b678-1f93-4660-a9c5-01af87b323d3}/chrome/leakmon.jar
EOM
cvs -d':pserver:anonymous@cvs-mirror.mozilla.org:/cvsroot' diff
popd
rm -rf "$TMPDIR"

DATE="$(LC_TIME=C TZ=UTC date +'%-d %b %Y %H:%M:%S') -0000"

find . -type f | grep -v ".svn" | sed 's/^\.\///' | while read FNAME
do
        cat <<EOM
Index: extensions/leak-monitor/$FNAME
===================================================================
RCS file: extensions/leak-monitor/$FNAME
diff -N extensions/leak-monitor/$FNAME
--- /dev/null	1 Jan 1970 00:00:00 -0000
+++ extensions/leak-monitor/$FNAME	$DATE
EOM
        LEN=$(wc -l "$FNAME" | cut -d' ' -f1)
        if [ "$LEN" == "0" ]
        then
            # really shouldn't even output previous 2 lines, but this
            # case irrelevant in practice
            echo -n
        elif [ "$LEN" == "1" ]
        then
            echo "@@ -0,0 +1 @@"
        else 
            echo "@@ -0,0 +1,$LEN @@"
        fi
        cat $FNAME | sed 's/^/\+/'
done
