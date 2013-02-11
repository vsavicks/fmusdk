all:
	(cd src; $(MAKE))

clean:
	(cd src; $(MAKE) clean)

distclean: clean
	rm -f bin/fmusim_cs* bin/fmusim_me*
	rm -rf fmu
	find . -name "*~" -exec rm {} \;
	find . -name "#*~" -exec rm {} \;

test:
	(./fmusim cs componentGraphWt.xml 30 0.1 0 s)
