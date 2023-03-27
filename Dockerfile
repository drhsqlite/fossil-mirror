# syntax=docker/dockerfile:1.3
# See www/containers.md for documentation on how to use this file.

## ---------------------------------------------------------------------
## STAGE 1: Build static Fossil binary
## ---------------------------------------------------------------------

### We aren't pinning to a more stable version of Alpine because we want
### to build with the latest tools and libraries available in case they
### fixed something that matters to us since the last build.  Everything
### below depends on this layer, and so, alas, we toss this container's
### cache on Alpine's release schedule, roughly once a month.
FROM alpine:latest AS builder
WORKDIR /tmp

### Bake the basic Alpine Linux into a base layer so it only changes
### when the upstream image is updated or we change the package set.
RUN set -x                                                             \
    && apk update                                                      \
    && apk upgrade --no-cache                                          \
    && apk add --no-cache                                              \
         busybox-static gcc make                                       \
         linux-headers musl-dev                                        \
         openssl-dev openssl-libs-static                               \
         zlib-dev zlib-static

### Build Fossil as a separate layer so we don't have to rebuild the
### Alpine environment for each iteration of Fossil's dev cycle.
###
### We must cope with a bizarre ADD misfeature here: it unpacks tarballs
### automatically when you give it a local file name but not if you give
### it a /tarball URL!  It matters because we default to a URL in case
### you're building outside a Fossil checkout, but when building via the
### container-image target, we avoid a costly hit on fossil-scm.org
### by leveraging its DVCS nature via the "tarball" command and passing
### the resulting file's name in.
ARG FSLCFG=""
ARG FSLVER="trunk"
ARG FSLURL="https://fossil-scm.org/home/tarball/src?r=${FSLVER}"
ENV FSLSTB=/tmp/fsl/src.tar.gz
ADD $FSLURL $FSLSTB
RUN set -x                                                             \
    && if [ -d $FSLSTB ] ; then mv $FSLSTB/src fsl ;                   \
       else tar -C fsl -xzf fsl/src.tar.gz ; fi                        \
    && m=fsl/src/src/main.mk                                           \
    && fsl/src/configure --static CFLAGS='-Os -s' $FSLCFG && make -j11


## ---------------------------------------------------------------------
## STAGE 2: Pare that back to the bare essentials.
## ---------------------------------------------------------------------

FROM scratch AS os
ARG UID=499

### Lay BusyBox down as the first base layer. Coupled with the host's
### kernel, this is the "OS" used to RUN the subsequent setup script.
COPY --from=builder /bin/busybox.static /bin/busybox
RUN [ "/bin/busybox", "--install", "/bin" ]

### Set up that base OS for our specific use without tying it to
### anything likely to change often.  So long as the user leaves
### UID alone, this layer will be durable.
RUN set -x                                                              \
    && mkdir log museum tmp                                             \
    && echo "root:x:0:0:Admin:/:/false"                   > /tmp/passwd \
    && echo "root:x:0:root"                               > /tmp/group  \
    && echo "fossil:x:${UID}:${UID}:User:/museum:/false" >> /tmp/passwd \
    && echo "fossil:x:${UID}:fossil"                     >> /tmp/group


## ---------------------------------------------------------------------
## STAGE 3: Drop BusyBox, too, now that we're done with its /bin/sh &c
## ---------------------------------------------------------------------

FROM scratch AS run
COPY --from=os /tmp/group /tmp/passwd /etc/
COPY --from=os --chown=fossil:fossil /log    /log/
COPY --from=os --chown=fossil:fossil /museum /museum/
COPY --from=os --chmod=1777          /tmp    /tmp/
COPY --from=builder /tmp/fossil /bin/


## ---------------------------------------------------------------------
## RUN!
## ---------------------------------------------------------------------

ENV PATH "/bin"
EXPOSE 8080/tcp
USER fossil
CMD [ \
    "fossil", "server",     \
    "--create",             \
    "--jsmode", "bundled",  \
    "--user", "admin",      \
    "museum/repo.fossil" ]
