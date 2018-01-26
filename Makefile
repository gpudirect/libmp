include libmp.mk

all: checks libs

checks:
	@if [ ! -d $(PREFIX) ]; then echo "PREFIX env not defined"; exit; fi;\
	echo "Using PREFIX=$(PREFIX)"
	mkdir -p $(PREFIX)/lib
	mkdir -p $(PREFIX)/include
	mkdir -p $(PREFIX)/bin

libs: gdrcopy libgdsync libmp

gdrcopy:
ifeq ($(GDRCOPY_BUILD), 1)
	make -C gdrcopy PREFIX=$(PREFIX) DESTLIB=$(PREFIX)/lib clean all install
else
	@echo "GDRcopy not built"
endif

libgdsync:
ifeq ($(GDSYNC_BUILD), 1)
	cp -r libgdsync/include/* include && cd libgdsync && ./build.sh
else
	@echo "LibGDSync in $(GDSYNC_ENABLE)"
endif

libmp:
	./build.sh $(CUDA_ENABLE) $(GDSYNC_ENABLE) $(MPI_ENABLE)

clean:
	make -C gdrcopy clean && \
	make -C libgdsync clean && \
	make -C libmp clean

.PHONY: checks libgdsync libmp clean all libs gdrcopy
