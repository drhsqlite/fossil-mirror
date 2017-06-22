#!/bin/bash
# A quick hack to generate a Debian package of fossil. i took most of this
# from Martin Krafft's "The Debian System" book.

DEB_REV=${1-1} # .deb package build/revision number.
PACKAGE_DEBNAME=fossil
THISDIR=${PWD}

if uname -a | grep -i nexenta &>/dev/null; then
# Assume NexentaOS/GnuSolaris:
    DEB_ARCH_NAME=solaris-i386
    DEB_ARCH_PKGDEPENDS="sunwcsl" # for -lsocket
else
    DEB_ARCH_NAME=$(dpkg --print-architecture)
fi

SRCDIR=$(cd ..; pwd)
test -e ${SRCDIR}/fossil || {
    echo "This script must be run from a BUILT copy of the source tree."
    exit 1
}

DEBROOT=$PWD/deb.tmp
test -d ${DEBROOT} && rm -fr ${DEBROOT}

DEBLOCALPREFIX=${DEBROOT}/usr
BINDIR=${DEBLOCALPREFIX}/bin
mkdir -p ${BINDIR}
mkdir -p ${DEBLOCALPREFIX}/share/doc/${PACKAGE_DEBNAME}
cp ../fossil ${BINDIR}
strip ${BINDIR}/fossil

cd $DEBROOT || {
    echo "Debian dest dir [$DEBROOT] not found. :("
    exit 2
}


rm -fr DEBIAN
mkdir DEBIAN

PACKAGE_VERSION=$(date +%Y.%m.%d)
PACKAGE_DEB_VERSION=${PACKAGE_VERSION}-${DEB_REV}
DEBFILE=${THISDIR}/${PACKAGE_DEBNAME}-${PACKAGE_DEB_VERSION}-dev-${DEB_ARCH_NAME}.deb
PACKAGE_TIME=$(/bin/date)

rm -f ${DEBFILE}
echo "Creating .deb package [${DEBFILE}]..."

echo "Generating md5 sums..."
find ${DEBLOCALPREFIX} -type f -exec md5sum {} \; > DEBIAN/md5sums

true && {
    echo "Generating Debian-specific files..."
    COPYRIGHT=${DEBLOCALPREFIX}/share/doc/${PACKAGE_DEBNAME}/copyright
    cat <<EOF > ${COPYRIGHT}
This package was created by fossil-scm <fossil-dev@lists.fossil-scm.org>
on ${PACKAGE_TIME}.

The original sources for fossil can be downloaded for free from:

http://www.fossil-scm.org/

fossil is released under the terms of the 2-clause BSD License.

EOF
}

true && {
    CHANGELOG=${DEBLOCALPREFIX}/share/doc/${PACKAGE_DEBNAME}/changelog.gz
    cat <<EOF | gzip -c > ${CHANGELOG}
${PACKAGE_DEBNAME} ${PACKAGE_DEB_VERSION}; urgency=low

This release has no changes over the core source distribution. It has
simply been Debianized.

Packaged by fossil-dev <fossil-dev@lists.fossil-scm.org> on
${PACKAGE_TIME}.

EOF

}


true && {
    CONTROL=DEBIAN/control
    echo "Generating ${CONTROL}..."
    cat <<EOF > ${CONTROL}
Package: ${PACKAGE_DEBNAME}
Section: vcs
Priority: optional
Maintainer: fossil-dev <fossil-dev@lists.fossil-scm.org>
Architecture: ${DEB_ARCH_NAME}
Depends: libc6 ${DEB_ARCH_PKGDEPENDS+, }${DEB_ARCH_PKGDEPENDS}
Version: ${PACKAGE_DEB_VERSION}
Description: Fossil is a unique SCM (Software Configuration Management) system.
 This package contains the Fossil binary for *buntu/Debian systems.
 Fossil is a unique SCM program which supports distributed source control
 management using local repositories, access over HTTP CGI, or using the
 built-in HTTP server. It has a built-in wiki, file browsing, etc.
 Fossil home page: http://fossil-scm.org
 Fossil author: D. Richard Hipp
 License: 2-clause BSD
EOF

}


true && {
#    GZ_CONTROL=control.tar.gz
#    GZ_DATA=data.tar.gz
#    echo "Generating ${GZ_CONTROL} and ${GZ_DATA}..."
#    rm -f ${GZ_CONTROL} ${GZ_DATA} ${DEBFILE} 2>/dev/null
#    tar cz -C DEBIAN -f ${GZ_CONTROL} .
#    tar czf ${GZ_DATA} --exclude='*/doxygen-*' usr
#    echo '2.0' > debian-binary
    #ar rcu ${DEBFILE} debian-binary ${GZ_CONTROL} ${GZ_DATA}
    dpkg-deb -b ${DEBROOT} ${DEBFILE}
    echo "Package file created:"
    ls -la ${DEBFILE}
    dpkg-deb --info ${DEBFILE}
}

cd - >/dev/null
true && {
    echo "Cleaning up..."
    rm -fr ${DEBROOT}
}

echo "Done :)"
