
CC = gcc
CXX = g++
CFLAGS        = -pipe -O2 -Wall -W -D_REENTRANT -lpthread -lLEPTON_SDK -L/usr/lib/arm-linux-gnueabihf -L../raspberrypi_libs/leptonSDKEmb32PUB/Debug
CXXFLAGS      = -pipe -O2 -Wall -W -D_REENTRANT -lpthread -lLEPTON_SDK -L/usr/lib/arm-linux-gnueabihf -L../raspberrypi_libs/leptonSDKEmb32PUB/Debug
INCPATH = -I. -I../raspberrypi_libs 

INATALL_PATH = /usr/local/bin

all: sdk SPI.o Lepton_I2C.o Palettes.o v4l2lepton

sdk:
	make -C ../raspberrypi_libs/leptonSDKEmb32PUB

sdkclean:
	make -C ../raspberrypi_libs/leptonSDKEmb32PUB clean

Palettes.o: Palettes.cpp Palettes.h
	${CXX} -c ${CXXFLAGS} ${INCPATH} -o Palettes.o Palettes.cpp

SPI.o: SPI.cpp SPI.h
	${CXX} -c ${CXXFLAGS} ${INCPATH} -o SPI.o SPI.cpp

Lepton_I2C.o: 
	${CXX} -c ${CXXFLAGS} ${INCPATH} -o Lepton_I2C.o Lepton_I2C.cpp

v4l2lepton: v4l2lepton.o Palettes.o SPI.o Lepton_I2C.o
	${CXX} -o v4l2lepton  Palettes.o SPI.o Lepton_I2C.o v4l2lepton.cpp ${CXXFLAGS}

#leptsci.o: leptsci.c

.PHONY: clean
clean:
	rm -f SPI.o Lepton_I2C.o Palettes.o leptsci.o v4l2lepton.o v4l2lepton

.PHONY: install
install:
	echo "Please add '${INATALL_PATH}' to your PATH after install"
	mkdir -p ${INATALL_PATH}
	cp v4l2lepton ${INATALL_PATH}

.PHONY: uninstall
uninstall:
	rm ${INATALL_PATH}/v4l2lepton
