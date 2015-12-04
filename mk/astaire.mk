# mk file for astaire.

ASTAIRE_DIR := ${MODULE_DIR}/astaire

astaire :
	ROOT=${ASTAIRE_DIR} ${MAKE} -C ${ASTAIRE_DIR}

astaire_test :
	ROOT=${ASTAIRE_DIR} ${MAKE} -C ${ASTAIRE_DIR} test

astaire_clean :
	ROOT=${ASTAIRE_DIR} ${MAKE} -C ${ASTAIRE_DIR} clean

astaire_distclean: astaire_clean

.PHONY : astaire astaire_test astaire_clean astaire_distclean
