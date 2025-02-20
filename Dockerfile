# syntax=docker/dockerfile:1.3
# See www/containers.md for documentation on how to use this file.

## ---------------------------------------------------------------------
## STAGE 1: Build static Fossil binary
## ---------------------------------------------------------------------

### We don't pin a more stable version of our base layer because we want
### to build with the latest tools and libraries available in case they
### fixed something that matters to us since the last build.  Everything
### below depends on this layer, and so, alas, we toss this container's
### cache on Alpine's release schedule, roughly once a month.
FROM alpine:latest AS bld
WORKDIR /fsl

### Bake the build-time userland into a base layer so it only changes
### when the upstream image is updated or we change the package set.
RUN set -x                                                             \
    && apk update                                                      \
    && apk upgrade --no-cache                                          \
    && apk add --no-cache                                              \
         gcc make                                                      \
         linux-headers musl-dev                                        \
         openssl-dev openssl-libs-static                               \
         zlib-dev zlib-static

### Build Fossil as a separate layer so we don't have to rebuild the
### userland for each iteration of Fossil's dev cycle.
###
### We must cope with a bizarre ADD misfeature here: it unpacks tarballs
### automatically when you give it a local file name but not if you give
### it a /tarball URL!  It matters because we default to a URL in case
### you're building outside a Fossil checkout, but when building via the
### container-image target, we avoid a costly hit on fossil-scm.org by
### leveraging its DVCS nature via the "tarball" command and passing the
### resulting file's name in.
ARG FSLCFG=""
ARG FSLVER="trunk"
ARG FSLURL="https://fossil-scm.org/home/tarball/src?r=${FSLVER}"
ENV FSLSTB=/fsl/src.tar.gz
ADD $FSLURL $FSLSTB
RUN set -x                                                             \
    && if [ -d $FSLSTB ] ;                                             \
       then mv $FSLSTB/src . ;                                         \
       else tar -xf src.tar.gz ; fi                                    \
    && src/configure --static CFLAGS='-Os -s' $FSLCFG && make -j16


## ---------------------------------------------------------------------
## STAGE 2: Pare that back to the bare essentials.
## ---------------------------------------------------------------------

FROM busybox AS os
ARG UID=499

### Set up that base OS for our specific use without tying it to
### anything likely to change often.  So long as the user leaves
### UID alone, this layer will be durable.
RUN set -x                                                             \
    && mkdir e log museum                                              \
    && echo "root:x:0:0:Admin:/:/false"                   > /e/passwd  \
    && echo "root:x:0:root"                               > /e/group   \
    && echo "fossil:x:${UID}:${UID}:User:/museum:/false" >> /e/passwd  \
    && echo "fossil:x:${UID}:fossil"                     >> /e/group


## ---------------------------------------------------------------------
## STAGE 3: Drop BusyBox, too, now that we're done with its /bin/sh &c
## ---------------------------------------------------------------------

FROM scratch AS run
COPY --from=bld --chmod=755           /fsl/fossil /bin/
COPY --from=os  --chmod=600           /e/*        /etc/
COPY --from=os  --chmod=1777          /tmp        /tmp/
COPY --from=os  --chown=fossil:fossil /log        /log/
COPY --from=os  --chown=fossil:fossil /museum     /museum/


## ---------------------------------------------------------------------
## RUN!
## ---------------------------------------------------------------------

ENV PATH "/bin"
EXPOSE 8080/tcp
USER fossil
ENTRYPOINT [ "fossil", "server" ]
CMD [                       \
    "--create",             \
    "--jsmode", "bundled",  \
    "--user",   "admin",    \
    "museum/repo.fossil" ]
