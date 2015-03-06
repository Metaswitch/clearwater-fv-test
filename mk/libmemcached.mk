# included mk file for the libmemcached module

ifndef LIBEVENT_DIR
  include ${MK_DIR}/libevent.mk
endif

LIBMEM_DIR := ${MODULE_DIR}/libmemcached
LIBMEM_CONFIGURE := ${LIBMEM_DIR}/configure
LIBMEM_MAKEFILE := ${LIBMEM_DIR}/Makefile

${LIBMEM_CONFIGURE}:
	cd ${LIBMEM_DIR} && ./config/autorun.sh

${LIBMEM_MAKEFILE}: ${LIBMEM_CONFIGURE}
	cd ${LIBMEM_DIR} && ./configure --prefix=${INSTALL_DIR} \
                                  --with-lib-prefix=${INSTALL_DIR} \
                                  CFLAGS="-I${INSTALL_DIR}/include" \
                                  LDFLAGS="-L${INSTALL_DIR}/lib"

libmemcached: libevent ${LIBMEM_MAKEFILE}
	${MAKE} -C ${LIBMEM_DIR}
	${MAKE} -C ${LIBMEM_DIR} install

libmemcached_test: libevent ${LIBMEM_MAKEFILE}
	${MAKE} -C ${LIBMEM_DIR} test

libmemcached_clean: ${LIBMEM_MAKEFILE}
	${MAKE} -C ${LIBMEM_DIR} clean

libmemcached_distclean: ${LIBMEM_MAKEFILE}
	${MAKE} -C ${LIBMEM_DIR} distclean


.PHONY: libmemcached libmemcached_test libmemcached_clean libmemcached_distclean
