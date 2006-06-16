#!/bin/bash
# vim: set shiftwidth=4 tabstop=4 autoindent expandtab:

if [ $# -lt 3 ]
then
	echo "Usage: $0 <dest> <source> <source> ..." 1>&2
	exit 1
fi

DEST=$1
shift

if [ -e $DEST ]
then
	echo "Destination file $DEST already exists." 1>&2
	exit 2
fi

OLDUMASK=$(umask)
umask 077
TMPDIR=$(mktemp -d) || exit 2

for SRC in "$@"
do
	mkdir $TMPDIR/in
	unzip -d $TMPDIR/in $SRC > /dev/null
	PLATFORM=$(grep targetPlatform $TMPDIR/in/install.rdf | cut -d\> -f2 | cut -d\< -f1)
	echo Merging $PLATFORM 1>&2
	mkdir $TMPDIR/in/platform
	mkdir $TMPDIR/in/platform/$PLATFORM
	mkdir $TMPDIR/in/platform/$PLATFORM/components
	(cd $TMPDIR/in && find components -type f -a -not -name "*.xpt") | while read BINARY
	do
		mv $TMPDIR/in/$BINARY $TMPDIR/in/platform/$PLATFORM/$BINARY
	done
	if [ -e $TMPDIR/out ]
	then
        # Move platform directory
        mv $TMPDIR/in/platform/$PLATFORM $TMPDIR/out/platform/

        # Merge install.rdf targetPlatform lines
        dos2unix -q $TMPDIR/in/install.rdf
        if diff $TMPDIR/out/install.rdf $TMPDIR/in/install.rdf | grep "^[<>]" | grep -v targetPlatform
        then
            echo "Warning: Differences in install.rdf files other than targetPlatform." 1>&2
        fi
        diff --ifdef=DELETEME $TMPDIR/out/install.rdf $TMPDIR/in/install.rdf | grep -v "^#" > $TMPDIR/install.rdf
        mv $TMPDIR/install.rdf $TMPDIR/out/install.rdf

        # XXX Warn if there are other differences?

        # Remove the rest
		rm -rf $TMPDIR/in
	else
		mv $TMPDIR/in $TMPDIR/out
	fi
done

(cd $TMPDIR/out && zip ../out.zip $(find . -type f))
mv $TMPDIR/out.zip $DEST

umask $OLDUMASK
rm -rf $TMPDIR
