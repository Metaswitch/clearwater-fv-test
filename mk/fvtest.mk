# included mk file for clearwater-fv-test

FVTEST_DIR := ${ROOT}/src
FVTEST_TEST_DIR := ${ROOT}/tests

fvtest:
	${MAKE} -C ${FVTEST_DIR}

fvtest_test:
	${MAKE} -C ${FVTEST_DIR} test

fvtest_clean:
	${MAKE} -C ${FVTEST_DIR} clean

fvtest_distclean: fvtest_clean

.PHONY: fvtest fvtest_test fvtest_clean fvtest_distclean
