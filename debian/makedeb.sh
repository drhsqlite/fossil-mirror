#!/bin/bash
# A quick hack to generate a Debian package of fossil. i took most of this
# from Martin Krafft's "The Debian System" book.

DEB_REV=${1-1} # .deb package build/revision number.
PACKAGE_DEBNAME=fossil-scm
THISDIR=${PWD}

if uname -a | grep -i nexenta &>/dev/null; then
# Assume NexentaOS/GnuSolaris:
    DEB_PLATFORM=nexenta
    DEB_ARCH_NAME=solaris-i386
    DEB_ARCH_PKGDEPENDS="sunwcsl" # for -lsocket
else
    DEB_PLATFORM=${DEB_PLATFORM-ubuntu-feisty}
    DEB_ARCH_NAME=i386
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
DEBFILE=${THISDIR}/${PACKAGE_DEBNAME}-${PACKAGE_DEB_VERSION}-dev-${DEB_ARCH_NAME}-${DEB_PLATFORM}.deb
PACKAGE_TIME=$(/bin/date)

rm -f ${DEBFILE}
echo "Creating .deb package [${DEBFILE}]..."

echo "Generating md5 sums..."
find ${DEBLOCALPREFIX} -type f -exec md5sum {} \; > DEBIAN/md5sums

true && {
    echo "Generating Debian-specific files..."
    COPYRIGHT=${DEBLOCALPREFIX}/share/doc/${PACKAGE_DEBNAME}/copyright
    cat <<EOF > ${COPYRIGHT}
This package was created by stephan beal <stephan@s11n.net>
on ${PACKAGE_TIME}.

The original sources for fossil can be downloaded for free from:

http://www.fossil-scm.org/

fossil is released under the terms of the GNU General Public License.

EOF
}

true && {
    CHANGELOG=${DEBLOCALPREFIX}/share/doc/${PACKAGE_DEBNAME}/changelog.gz
    cat <<EOF | gzip -c > ${CHANGELOG}
${PACKAGE_DEBNAME} ${PACKAGE_DEB_VERSION}; urgency=low

This release has no changes over the core source distribution. It has
simply been Debianized.

Packaged by stephan beal <stephan@s11n.net> on
${PACKAGE_TIME}.

EOF

}


true && {
    CONTROL=DEBIAN/control
    echo "Generating ${CONTROL}..."
    cat <<EOF > ${CONTROL}
Package: ${PACKAGE_DEBNAME}
Section: devel
Priority: optional
Maintainer: stephan beal <stephan@s11n.net>
Architecture: ${DEB_ARCH_NAME}
Depends: libc6-dev ${DEB_ARCH_PKGDEPENDS+, }${DEB_ARCH_PKGDEPENDS}
Version: ${PACKAGE_DEB_VERSION}
Description: a powerful, flexible serialization framework for C++.
 This package contains all files needed for development, as well as the s11nconvert tool
 and library documentation. Note that an ODD minor version number (e.g. 1.1 or 1.3)
 indicates a beta/development version, not intended for general client-side use,
 whereas EVEN minor numbers (e.g. 1.2 or 1.4) indicate stable versions.
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
