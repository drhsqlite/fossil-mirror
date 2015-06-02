###
#   Dockerfile for Fossil
###
FROM fedora:21

### Now install some additional parts we will need for the build
RUN yum update -y && yum install -y gcc make zlib-devel openssl-devel tar && yum clean all && groupadd -r fossil -g 433 && useradd -u 431 -r -g fossil -d /opt/fossil -s /sbin/nologin -c "Fossil user" fossil

### If you want to build "release", change the next line accordingly.
ENV FOSSIL_INSTALL_VERSION branch-1.33

RUN curl "http://core.tcl.tk/tcl/tarball/tcl-src.tar.gz?name=tcl-src&uuid=release" | tar zx
RUN cd tcl-src/unix && ./configure --prefix=/usr --disable-shared --disable-threads --disable-load && make && make install
RUN curl "http://www.fossil-scm.org/index.html/tarball/fossil-src.tar.gz?name=fossil-src&uuid=${FOSSIL_INSTALL_VERSION}" | tar zx
RUN cd fossil-src && ./configure --disable-lineedit --disable-fusefs --json --with-th1-docs --with-th1-hooks --with-tcl
RUN cd fossil-src && make && strip fossil && cp fossil /usr/bin && cd .. && rm -rf fossil-src && chmod a+rx /usr/bin/fossil && mkdir -p /opt/fossil && chown fossil:fossil /opt/fossil

### Build is done, remove modules no longer needed
RUN yum remove -y gcc make zlib-devel openssl-devel tar && yum clean all

USER fossil

ENV HOME /opt/fossil

EXPOSE 8080

CMD ["/usr/bin/fossil", "server", "--create", "--user", "admin", "/opt/fossil/repository.fossil"]
