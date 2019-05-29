
CFLAGS = -g -I.
CXXFLAGS = -g -I. -std=gnu++17

OBJS = desmume/FIFO.o desmume/GPU.o desmume/MMU.o desmume/NDSSystem.o desmume/SPU.o desmume/arm_instructions.o desmume/armcpu.o desmume/barray.o desmume/bios.o desmume/cp15.o desmume/isqrt.o desmume/matrix.o desmume/mc.o desmume/resampler.o desmume/state.o desmume/thumb_instructions.o 2sftowav.o

OPTS = -O3

all: libvio2sf.a 2sftowav

../psflib/libpsflib.a:
	make -C ../psflib

libvio2sf.a : $(OBJS)
	$(AR) rcs $@ $^

trim2sflib: trim2sflib.o libvio2sf.a ../psflib/libpsflib.a common.h
	$(CXX) -o $@ $^ -lz

%.o: %.c
	$(CC) -c $(CFLAGS) $(OPTS) -o $@ $<

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(OPTS) -o $@ $<

clean:
	rm -f $(OBJS) libvio2sf.a trim2sflib.o trim2sflib > /dev/null
	make -C ../psflib clean

2sftowav: 2sftowav.o libvio2sf.a ../psflib/libpsflib.a common.h
	$(CXX) $(CXXFLAGS) -o $@ 2sftowav.o libvio2sf.a ../psflib/libpsflib.a -lz -lm
