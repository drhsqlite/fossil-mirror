# STAGE 1: Build a static Fossil binary atop Alpine Linux

# Avoid the temptation to swap the wget call below out for an ADD URL
# directive.  The URL is fixed for a given release tag, which triggers
# Docker's caching behavior, causing it to reuse that version as long
# as it remains in the cache.  We prefer to rely on the caching of the
# server instance on fossil-scm.org, which will keep these trunk
# tarballs around until the next trunk commit.

FROM alpine:latest AS builder
WORKDIR /tmp
RUN apk update                                                         \
     && apk upgrade --no-cache                                         \
     && apk add --no-cache                                             \
         busybox-static gcc make                                       \
         musl-dev                                                      \
         openssl-dev openssl-libs-static                               \
         zlib-dev zlib-static                                          \
     && wget -O - https://fossil-scm.org/home/tarball/src | tar -xz    \
     && src/configure --static CFLAGS='-Os -s'                         \
     && make -j

# STAGE 2: Pare that back to the bare essentials.

FROM scratch
ENV JAIL=/jail
WORKDIR ${JAIL}
COPY --from=builder /tmp/fossil ${JAIL}/bin/
COPY --from=builder /bin/busybox.static /bin/busybox
RUN [ "/bin/busybox", "--install", "/bin" ]
RUN mkdir -m 700 dev                   \
    && mknod -m 600 dev/null    c 1 3  \
    && mknod -m 600 dev/urandom c 1 9

# Now we can run the stripped-down environment in a chroot jail, while
# leaving open the option to debug it live via the Busybox shell.

EXPOSE 8080/tcp
CMD [ \
    "bin/fossil", "server", \
    "--create",             \
    "--jsmode", "bundled",  \
    "--user", "admin",      \
    "repo.fossil"]
