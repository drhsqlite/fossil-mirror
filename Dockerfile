###
#   Dockerfile for Fossil
###
FROM fedora:24

### Now install some additional parts we will need for the build
RUN dnf update -y && dnf install -y gcc make zlib-devel openssl-devel tar && dnf clean all && groupadd -r fossil -g 433 && useradd -u 431 -r -g fossil -d /opt/fossil -s /sbin/nologin -c "Fossil user" fossil

### If you want to build "trunk", change the next line accordingly.
ENV FOSSIL_INSTALL_VERSION release

RUN curl "http://core.tcl.tk/tcl/tarball/tcl-src.tar.gz?name=tcl-src&uuid=release" | tar zx
RUN cd tcl-src/unix && ./configure --prefix=/usr --disable-load && make && make install
RUN curl "http://www.fossil-scm.org/index.html/tarball/fossil-src.tar.gz?name=fossil-src&uuid=${FOSSIL_INSTALL_VERSION}" | tar zx
RUN cd fossil-src && ./configure --disable-fusefs --json --with-th1-docs --with-th1-hooks --with-tcl --with-tcl-stubs --with-tcl-private-stubs
RUN cd fossil-src/src && mv main.c main.c.orig && sed s/\"now\"/0/ <main.c.orig >main.c
RUN cd fossil-src && make && strip fossil && cp fossil /usr/bin && cd .. && rm -rf fossil-src && chmod a+rx /usr/bin/fossil && mkdir -p /opt/fossil && chown fossil:fossil /opt/fossil

### Build is done, remove modules no longer needed
RUN dnf remove -y gcc make zlib-devel openssl-devel tar && dnf clean all

USER fossil

ENV HOME /opt/fossil

EXPOSE 8080

CMD ["/usr/bin/fossil", "server", "--create", "--user", "admin", "/opt/fossil/repository.fossil"]
