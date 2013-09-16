
libtclstub86.a:
	$(PREFIX)gcc -c -O2 -fomit-frame-pointer -DUSE_TCL_STUBS ../generic/tclStubLib.c -o tclStubLib.o
	$(PREFIX)ar cr libtclstub86.a tclStubLib.o
	$(PREFIX)ranlib libtclstub86.a

binaries:
	@echo "Please copy the Tcl distribution you want to use to $(TCLSRCDIR),"
	@echo "or compile Fossil with FOSSIL_ENABLE_TCL_STUBS"
