#OS_TYPE := UNSUPORTED
#ifeq ($(OS),Windows_NT)
#	OS_TYPE := WINDOWS
#else
#UNAME_S := $(shell uname -s)
#	ifeq ($(UNAME_S),Linux)
#		OS_TYPE := LINUX
#	endif
#	ifeq ($(UNAME_S),Darwin)
#		OS_TYPE := MACOS
#	endif
#endif
#
#OS_TARGET := unsuported_os
#ifeq ($(OS_TYPE),WINDOWS)
#	OS_TARGET := windows_all
#endif
#ifeq ($(OS_TYPE),LINUX)
#	OS_TARGET := linux_all
#endif
#ifeq ($(OS_TYPE),MACOS)
#	OS_TARGET := macos_all
#endif

all:
	$(MAKE) deps
	$(MAKE) sol

sol:
	mkdir -p out/sol
	$(CXX) -c engines/sol/sol-base.cpp -I engines -I engines/sol -I out/gmp -o out/sol/sol-base.o

deps:
	$(MAKE) gmp

gmp:
	mkdir -p out/gmp
	cd out/gmp ; ../../libs/gmp/configure
	$(MAKE) -C out/gmp

clean:
	rm -r out
