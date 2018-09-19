###
#   Dockerfile for Fossil
###
FROM fedora:28

### Now install some additional parts we will need for the build
RUN dnf update -y && dnf install -y gcc make tcl tcl-devel zlib-devel openssl-devel tar && dnf clean all && groupadd -r fossil -g 433 && useradd -u 431 -r -g fossil -d /opt/fossil -s /sbin/nologin -c "Fossil user" fossil

### If you want to build "trunk", change the next line accordingly.
ENV FOSSIL_INSTALL_VERSION release

RUN curl "https://www.fossil-scm.org/index.html/tarball/fossil-src.tar.gz?name=fossil-src&uuid=${FOSSIL_INSTALL_VERSION}" | tar zx
RUN cd fossil-src && ./configure --disable-fusefs --json --with-th1-docs --with-th1-hooks --with-tcl=1 --with-tcl-stubs --with-tcl-private-stubs
RUN cd fossil-src/src && mv main.c main.c.orig && sed s/\"now\"/0/ <main.c.orig >main.c
RUN cd fossil-src && make && strip fossil && cp fossil /usr/bin && cd .. && rm -rf fossil-src && chmod a+rx /usr/bin/fossil && mkdir -p /opt/fossil && chown fossil:fossil /opt/fossil

### Build is done, remove modules no longer needed
RUN dnf remove -y gcc make zlib-devel tcl-devel openssl-devel tar && dnf clean all

USER fossil

ENV HOME /opt/fossil

EXPOSE 8080

CMD ["/usr/bin/fossil", "server", "--create", "--user", "admin", "/opt/fossil/repository.fossil"]
