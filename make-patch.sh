#!/bin/bash

TMPDIR="$(mktemp -d)" || exit 1
(cd "$TMPDIR" && cvs -d':pserver:anonymous@cvs-mirror.mozilla.org:/cvsroot' co mozilla/browser/confvars.sh > /dev/null 2>&1 && cd mozilla && perl -pi -e 's/MOZ_EXTENSIONS_DEFAULT=" /MOZ_EXTENSIONS_DEFAULT=" leak-monitor /' browser/confvars.sh && cvs -d':pserver:anonymous@cvs-mirror.mozilla.org:/cvsroot' diff)
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
        echo "@@ -0,0 +$LEN @@"
        cat $FNAME | sed 's/^/\+/'
done
