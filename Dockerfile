###
#   (experimental) Dockerfile for Fossil (Trunk)
#
#   Although it works fine, there is a one little thing which is not 100%
#   correct: the fossil repository is created at Docker image creation time,
#   which means everyone using the same docker image will have the same
#   server ID and project ID.
###
FROM fedora:21

### Now install some additional parts we will need for the build
# RUN yum update -y && yum clean all
RUN yum install -y gcc make zlib-devel sqlite-devel openssl-devel tcl-devel && yum clean all

RUN groupadd -r fossil -g 433 && useradd -u 431 -r -g fossil -d /opt/fossil -s /sbin/nologin -c "Fossil user" fossil

RUN curl -o Fossil-trunk.tar.gz http://www.fossil-scm.org/index.html/tarball/Fossil-trunk.tar.gz?name=fossil-trunk | tar zx
RUN cd fossil-trunk && ./configure --lineedit=0 --json --with-tcl --with-tcl-stubs --with-tcl-private-stubs && make;
RUN cp fossil-trunk/fossil /usr/bin
RUN rm -rf fossil-trunk
RUN chmod a+rx /usr/bin/fossil
RUN mkdir -p /opt/fossil
RUN chown fossil:fossil /opt/fossil

### Build is done, remove modules no longer needed
RUN yum remove -y gcc make zlib-devel sqlite-devel openssl-devel tcl-devel && yum clean all

USER fossil

ENV HOME /opt/fossil

RUN fossil new --empty -A admin /opt/fossil/repository.fossil
RUN fossil user password -R /opt/fossil/repository.fossil admin admin
RUN fossil cache init -R /opt/fossil/repository.fossil

EXPOSE 8080

CMD ["/usr/bin/fossil", "server", "/opt/fossil/repository.fossil"]
