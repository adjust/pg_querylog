ARG PG_VERSION
FROM postgres:${PG_VERSION}-alpine

ARG CHECK_CODE
RUN if [ "${CHECK_CODE}" = "clang" ] ; then \
	echo 'http://dl-cdn.alpinelinux.org/alpine/latest-stable/main' > /etc/apk/repositories; \
	apk update && \
	apk --no-cache add clang-analyzer make musl-dev gcc openssl-dev; \
	fi

RUN if [ "${CHECK_CODE}" = "false" ] ; then \
	echo 'http://dl-cdn.alpinelinux.org/alpine/latest-stable/main' > /etc/apk/repositories; \
	apk --no-cache add curl linux-headers python3 python3-dev py3-virtualenv gcc make musl-dev openssl-dev;\
	fi

ENV LANG=C.UTF-8 PGDATA=/pg/data CHECK_CODE=${CHECK_CODE}

RUN mkdir -p ${PGDATA} && \
	mkdir /pg/src && \
	chown postgres:postgres ${PGDATA} && \
	chmod a+rwx /usr/local/lib/postgresql && \
	chmod a+rwx /usr/local/share/postgresql/extension && \
	mkdir -p /usr/local/share/doc/postgresql/contrib && \
	chmod a+rwx /usr/local/share/doc/postgresql/contrib

ADD . /pg/src
WORKDIR /pg/src
RUN chmod -R go+rwX /pg/src
USER postgres
ENTRYPOINT PGDATA=${PGDATA} CHECK_CODE=${CHECK_CODE} bash run_tests.sh
