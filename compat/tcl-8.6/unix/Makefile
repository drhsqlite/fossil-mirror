
libtclstub8.6.a:
	$(PREFIX)$(CC) -c -O2 -fomit-frame-pointer -DUSE_TCL_STUBS ../generic/tclStubLib.c -o tclStubLib.o
	$(PREFIX)ar cr libtclstub8.6.a tclStubLib.o
	$(PREFIX)ranlib libtclstub8.6.a

binaries:
	@echo "Please copy the Tcl distribution you want to use to $(TCLSRCDIR),"
	@echo "or compile Fossil with FOSSIL_ENABLE_TCL_STUBS"

clean:
	rm libtclstub8.6.a tclStubLib.o