###
#   (experimental) Dockerfile for Fossil (version 1.29)
#
#   Although it works fine, there is a one little thing which is not 100%
#   correct: the fossil repository is created at Docker image creation time,
#   which means everyone using the same docker image will have the same
#   server ID and project ID.
###
FROM fedora:21

### Now install some additional parts we will need for the build
#RUN yum update -y && yum clean all
RUN yum install -y gcc make zlib-devel sqlite-devel openssl-devel tcl-devel && yum clean all

RUN groupadd -r fossil -g 433 && useradd -u 431 -r -g fossil -d /opt/fossil -s /sbin/nologin -c "Fossil user" fossil

RUN curl http://www.fossil-scm.org/download/fossil-src-20140612172556.tar.gz  | tar zx
RUN cd fossil-src-20140612172556 && ./configure --lineedit=0 --json --with-tcl --with-tcl-stubs --with-tcl-private-stubs --disable-internal-sqlite && make; 
RUN cp fossil-src-20140612172556/fossil /usr/bin
RUN rm -rf fossil-src-20140612172556
RUN chmod a+rx /usr/bin/fossil
RUN mkdir -p /opt/fossil
RUN chown fossil:fossil /opt/fossil

### Build is done, remove modules no longer needed
RUN yum remove -y gcc make zlib-devel sqlite-devel openssl-devel tcl-devel && yum clean all

USER fossil

ENV HOME /opt/fossil

RUN fossil new --empty -A admin /opt/fossil/fossil.fossil
RUN fossil user password -R /opt/fossil/fossil.fossil admin admin
RUN fossil cache init -R /opt/fossil/fossil.fossil

EXPOSE 8080

CMD ["/usr/bin/fossil", "server", "/opt/fossil/fossil.fossil"]


