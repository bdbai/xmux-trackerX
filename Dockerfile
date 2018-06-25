FROM alpine:latest as builder
LABEL maintainer="bdbai <bdbaiapp@163.com>"

RUN echo '@edge http://nl.alpinelinux.org/alpine/edge/main' >> /etc/apk/repositories && \
  apk update && \
  apk upgrade && \
  apk add g++ fcgi-dev git autoconf automake make mariadb-connector-c@edge mariadb-connector-c-dev@edge mariadb-static@edge curl-dev libssh2-dev && \
  git clone https://github.com/lighttpd/spawn-fcgi /src/spawn-fcgi
RUN cd /src/spawn-fcgi && \
  ./autogen.sh && \
  ./configure LDFLAGS='-static' && \
  make

COPY * /src/
RUN cd /src && \
  g++ -O3 -ffunction-sections -fdata-sections -Wl,--gc-sections -static tracker.cpp -lcurl -lmysqlclient -lssh2 -lz -lssl -lcrypto -lfcgi && \
  strip -s -R .comment -R .gnu.version --strip-unneeded -x -X -o b.out a.out

FROM scratch
COPY --from=builder /src/spawn-fcgi/src/spawn-fcgi /usr/bin/spawn-fcgi
COPY --from=builder /src/b.out /tracker
CMD [ "/usr/bin/spawn-fcgi", "-p", "10081", "-n", "/tracker" ]
EXPOSE 10081
