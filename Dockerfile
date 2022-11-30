# See www/containers.md for documentation on how to use this file.

## ---------------------------------------------------------------------
## STAGE 1: Build static Fossil & BusyBox binaries atop Alpine Linux
## ---------------------------------------------------------------------

FROM alpine:latest AS builder
WORKDIR /tmp

### Bake the basic Alpine Linux into a base layer so we never have to
### repeat that step unless we change the package set.  Although we're
### going to throw this layer away below, we still pass --no-cache
### because that cache is of no use in an immutable layer.  Note that
### we allow the UPX step to fail: it isn't in the ARM distros.  We'll
### check whether this optional piece exists before using it below.
RUN set -x                                                             \
    && apk update                                                      \
    && apk upgrade --no-cache                                          \
    && apk add --no-cache                                              \
         gcc make                                                      \
         linux-headers musl-dev                                        \
         openssl-dev openssl-libs-static                               \
         zlib-dev zlib-static                                          \
     ; ( apk add --no-cache upx || exit 0 )

### Bake the custom BusyBox into another layer.  The intent is that this
### changes only when we change BBXVER.  That will force an update of
### the layers below, but this is a rare occurrence.
ARG BBXVER="1_35_0"
ENV BBXURL "https://github.com/mirror/busybox/tarball/${BBXVER}"
COPY containers/busybox-config /tmp/bbx/.config
ADD $BBXURL /tmp/bbx/src.tar.gz
RUN set -x \
    && tar --strip-components=1 -C bbx -xzf bbx/src.tar.gz             \
    && ( cd bbx && yes "" | make oldconfig && make -j11 )              \
    && test ! -x /usr/bin/upx || upx -9q bbx/busybox

### The changeable Fossil layer is the only one in the first stage that
### changes often, so add it last, to make it independent of the others.
###
### $FSLSTB can be either a file or a directory due to a ADD's bizarre
### behavior: it unpacks tarballs when added from a local file but not
### from a URL!   It matters because we default to a URL in case you're
### building outside a Fossil checkout, but when building via the
### container-image target, we can avoid a costly hit on the Fossil
### project's home site by pulling the data from the local repo via the
### "tarball" command.  This is a DVCS, after all!
ARG FSLCFG=""
ARG FSLVER="trunk"
ARG FSLURL="https://fossil-scm.org/home/tarball/src?r=${FSLVER}"
ENV FSLSTB=/tmp/fsl/src.tar.gz
ADD $FSLURL $FSLSTB
RUN set -x \
    && if [ -d $FSLSTB ] ; then mv $FSLSTB/src fsl ;                   \
       else tar -C fsl -xzf fsl/src.tar.gz ; fi                        \
    && m=fsl/src/src/main.mk                                           \
    && fsl/src/configure --static CFLAGS='-Os -s' $FSLCFG && make -j11 \
    && if [ -x /usr/bin/upx ] ; then upx -9q fossil ; fi


## ---------------------------------------------------------------------
## STAGE 2: Pare that back to the bare essentials.
## ---------------------------------------------------------------------

FROM scratch
WORKDIR /jail
ARG UID=499
ENV PATH "/bin:/jail/bin"

### Lay BusyBox down as the first base layer. Coupled with the host's
### kernel, this is the "OS."
COPY --from=builder /tmp/bbx/busybox /bin/
RUN [ "/bin/busybox", "--install", "/bin" ]

### Set up that base OS for our specific use without tying it to
### anything likely to change often.  So long as the user leaves
### UID alone, this layer will be durable.
RUN set -x                                                             \
    && echo 'root:x:0:0:SysAdmin:/:/bin/nologin' > /etc/passwd         \
    && echo 'root:x:0:root'                      > /etc/group          \
    && addgroup -S -g ${UID} fossil                                    \
    && adduser -S -h `pwd` -g 'Fossil User' -G fossil -u ${UID} fossil \
    && install -d -m 700 -o fossil -g fossil log museum                \
    && install -d -m 755 -o fossil -g fossil dev                       \
    && mknod -m 666 dev/null    c 1 3                                  \
    && mknod -m 444 dev/urandom c 1 9

### Do Fossil-specific things atop those base layers; this will change
### as often as the Fossil build-from-source layer above.
COPY --from=builder /tmp/fossil bin/
RUN set -x                                                             \
    && ln -s /jail/bin/fossil /bin/f                                   \
    && echo -e '#!/bin/sh\nfossil sha1sum "$@"' > /bin/sha1sum         \
    && echo -e '#!/bin/sh\nfossil sha3sum "$@"' > /bin/sha3sum         \
    && echo -e '#!/bin/sh\nfossil sqlite3 --no-repository "$@"' >      \
       /bin/sqlite3                                                    \
    && chmod +x /bin/sha?sum /bin/sqlite3


## ---------------------------------------------------------------------
## STAGE 3: Run!
## ---------------------------------------------------------------------

EXPOSE 8080/tcp
CMD [ \
    "bin/fossil", "server", \
    "--chroot", "/jail",    \
    "--create",             \
    "--jsmode", "bundled",  \
    "--user", "admin",      \
    "museum/repo.fossil"]
