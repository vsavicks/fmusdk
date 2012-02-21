# Build the .fmu files for both co-simulation and model-exchange.
#
# The build_fmu script calls make with the appropriate makefile variables set.

# The architecture: linux32, linux64, darwin32, darwin64
ARCH = darwin64

# Either cs for co-simuluation or me for model exchange.
CSORME = cs
CSORME_INCLUDE = -DFMI_COSIMULATION -I../../co_simulation/include
#CSORME = me
#CSORME_INCLUDE = -I../../model_exchange/include

# The suffix for shared libraries.
# dylib for Mac OS X, so for Linux
SHARED_LIBRARY_SUFFIX = dylib
#SHARED_LIBRARY_SUFFIX = so

# Empty for Mac OS X, -fPIC for Linux
PIC =
#PIC = -fPIC

# Build the .fmu files for both co-simulation and model-exchange
all:
	./build_fmu me bouncingBall
	./build_fmu me dq
	./build_fmu me inc
	./build_fmu me values
	./build_fmu me vanDerPol
	./build_fmu cs bouncingBall
	./build_fmu cs dq
	./build_fmu cs inc
	./build_fmu cs values
	./build_fmu cs vanDerPol

%.o: %.c
	echo `pwd`
	$(CC) -g -c $(PIC) -Wall $(CSORME_INCLUDE) $(CFLAGS) $< -o $@

%.so: %.o
	$(CC) -shared -Wl,-soname,$@ -o $@ $<

%.dylib: %.o
	$(CC) -dynamiclib -o $@ $<

%.fmu: %.$(SHARED_LIBRARY_SUFFIX)
	rm -rf fmu
	mkdir fmu
	mkdir fmu/binaries
	mkdir fmu/binaries/$(ARCH)
	mkdir fmu/sources
	mkdir fmu/documentation
	cp $< fmu/binaries/$(ARCH)
	-cp *.c fmu/sources
	-cp *.h fmu/sources
	-cp *.html fmu/documentation
	-cp *.png fmu/documentation
	cat modelDescription.xml ../$(CSORME).xml > fmu/modelDescription.xml
	cp model.png fmu
	if [ ! -d ../../../fmu/$(CSORME) ]; then \
		mkdir -p ../../../fmu/$(CSORME); \
	fi
	rm -f ../../../fmu/$(CSORME)/$@
	(cd fmu; zip -r ../../../../fmu/$(CSORME)/$@ *)

clean:
	(cd bouncingBall; make dirclean)
	(cd dq; make dirclean)
	(cd inc; make dirclean)
	(cd values; make dirclean)
	(cd vanDerPol; make dirclean)

dirclean:
	rm -f *.so *.dylib *.o *.fmu
	rm -f *~
	rm -rf fmu
