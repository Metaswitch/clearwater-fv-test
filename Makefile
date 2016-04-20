# Top level Makefile for building homestead

# This should come first so make does the right thing by default
all: build

ROOT ?= ${PWD}
MK_DIR := ${ROOT}/mk
PREFIX ?= ${ROOT}/usr
INSTALL_DIR ?= ${PREFIX}
MODULE_DIR := ${ROOT}/modules

INCLUDE_DIR := ${INSTALL_DIR}/include
LIB_DIR := ${INSTALL_DIR}/lib

SUBMODULES := astaire

include $(patsubst %, ${MK_DIR}/%.mk, ${SUBMODULES})
include ${MK_DIR}/fvtest.mk

build: ${SUBMODULES}

test: ${SUBMODULES} fvtest_test

testall: $(patsubst %, %_test, ${SUBMODULES}) test

clean: $(patsubst %, %_clean, ${SUBMODULES}) fvtest_clean
	rm -rf ${ROOT}/build

distclean: $(patsubst %, %_distclean, ${SUBMODULES}) fvtest_distclean
	rm -rf ${ROOT}/build

.PHONY: all build test clean distclean
