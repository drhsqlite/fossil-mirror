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
WORKDIR /jail
ENV UID 499
ENV PATH "/bin:/jail/bin"
COPY --from=builder /tmp/fossil bin/
COPY --from=builder /bin/busybox.static /bin/busybox
RUN [ "/bin/busybox", "--install", "/bin" ]
RUN mkdir -m 700 dev museum                                            \
    && mknod -m 600 dev/null    c 1 3                                  \
    && mknod -m 600 dev/urandom c 1 9                                  \
    && echo 'root:x:0:0:Fossil Init:/:/bin/nologin' > /etc/passwd      \
    && echo 'root:x:0:root'                         > /etc/group       \
    && addgroup -g ${UID} fossil                                       \
    && adduser -h `pwd` -g 'Fossil User' -G fossil -u ${UID} -S fossil \
    && chown fossil:fossil . museum

# Now we can run the stripped-down environment in a chroot jail, while
# leaving open the option to debug it live via the Busybox shell.
#
# Implicit: We don't set USER here on purpose because we want Fossil to
# start as root so it can chroot itself away inside /jail.  Since that's
# owned by the special fossil user, it drops root privileges for that
# user, preventing exotic root-based hacks on Docker.

EXPOSE 8080/tcp
CMD [ \
    "bin/fossil", "server", \
    "--chroot", "/jail",    \
    "--create",             \
    "--jsmode", "bundled",  \
    "museum/repo.fossil"]
