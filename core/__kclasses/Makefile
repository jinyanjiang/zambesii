.PHONY=
DELIVERABLE=../__kclasses.a
OBJFILES=memBmp.o bitmap.o allocTable.o pageTableCache.o slamCache.o \
	debugPipe.o debugBuffer.o memReservoir.o heap.o cachePool.o \
	hardwareIdList.o singleWaiterQueue.o \
	test_memBmp.cpp

all: Makefile $(DELIVERABLE)

memReservoir.o: CXXFLAGS+=-Wno-frame-address

$(DELIVERABLE): $(OBJFILES)
	$(AR) -rcs $@ $(OBJFILES)

clean: fonyphile
	rm -rf *.o *.s

fonyphile:
	rm -f clean aclean dirclean

