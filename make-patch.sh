#!/bin/bash

# Which source should we patch?
MOZRELEASE=http://hg.mozilla.org/mozilla-central/
MOZREV=FIREFOX_4_0b11_RELEASE # any revision syntax that hg accepts

TMPDIR="$(mktemp -d)" || exit 1
pushd "$TMPDIR" > /dev/null 2>&1 || exit 1

for FILE in "browser/confvars.sh" "browser/installer/package-manifest.in"
do
    mkdir -p "$(echo "$FILE" | sed 's,/[^/]*$,,;s,^,a/,')"
    wget -q "$MOZRELEASE/raw-file/$MOZREV/$FILE" -O "a/$FILE"
done

cp -r a b

perl -pi -e 's/MOZ_EXTENSIONS_DEFAULT=" /MOZ_EXTENSIONS_DEFAULT=" leak-monitor /' b/browser/confvars.sh || exit 1
cat >> b/browser/installer/package-manifest.in <<EOM

@BINPATH@/leakmonitor.xpi
EOM
diff -u -r a b
popd > /dev/null
rm -rf "$TMPDIR"

DATE="$(LC_TIME=C TZ=UTC date +'%-d %b %Y %H:%M:%S') -0000"

find . -type f | grep -v ".hg" | sed 's/^\.\///' | while read FNAME
do
        cat <<EOM
diff -N b/extensions/leak-monitor/$FNAME
--- /dev/null	1 Jan 1970 00:00:00 -0000
+++ b/extensions/leak-monitor/$FNAME	$DATE
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
        # Hack the root makefile to put the resulting XPI in dist/bin.
        if [ "$FNAME" == "Makefile.in" ]
        then
            cat $FNAME | sed 's!^XPI_PKGNAME.*!XPI_PKGNAME	= ../bin/$(MODULE)!;s/^/\+/'
        else
            cat $FNAME | sed 's/^/\+/'
        fi
done
