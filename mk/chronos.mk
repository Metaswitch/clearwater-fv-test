# mk file for chronos.

CHRONOS_DIR := ${MODULE_DIR}/chronos

chronos :
	ROOT=${CHRONOS_DIR} ${MAKE} -C ${CHRONOS_DIR}

chronos_test :
	ROOT=${CHRONOS_DIR} ${MAKE} -C ${CHRONOS_DIR} test

chronos_clean :
	ROOT=${CHRONOS_DIR} ${MAKE} -C ${CHRONOS_DIR} clean

chronos_distclean: chronos_clean

.PHONY : chronos chronos_test chronos_clean chronos_distclean
