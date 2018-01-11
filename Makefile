include libmp.mk

all: checks libs

checks:
	@if [ ! -d $(PREFIX) ]; then echo "PREFIX env not defined"; exit; fi;\
	echo "Using PREFIX=$(PREFIX)"

libs: gdrcopy libgdsync libmp

gdrcopy:
ifeq ($(GDRCOPY_BUILD), 1)
	make -C gdrcopy PREFIX=$(PREFIX) DESTLIB=$(PREFIX)/lib clean all install
else
	echo "GDRCopy not built"
endif

libgdsync:
ifeq ($(GDSYNC_BUILD), 1)
	cd libgdsync && ./build.sh
else
	echo "LibGDSync not built"
endif

libmp:
	./build.sh $(CUDA_ENABLE) $(GDSYNC_ENABLE) $(MPI_ENABLE)

clean:
	make -C gdrcopy clean && \
	make -C libgdsync clean && \
	make -C libmp clean

.PHONY: checks libgdsync libmp clean all libs gdrcopy
