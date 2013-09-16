/*
 * tclStubLib.c --
 *
 *	Stub object that will be statically linked into extensions that want
 *	to access Tcl.
 *
 * Copyright (c) 1998-1999 by Scriptics Corporation.
 * Copyright (c) 1998 Paul Duffin.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tcl.h"
#include <assert.h>

const TclStubs *tclStubsPtr = NULL;


/*
 *----------------------------------------------------------------------
 *
 * Tcl_InitStubs --
 *
 *	Tries to initialise the stub table pointers and ensures that the
 *	correct version of Tcl is loaded.
 *
 * Results:
 *	The actual version of Tcl that satisfies the request, or NULL to
 *	indicate that an error occurred.
 *
 * FOSSIL MODIFICATION!!!!!!!!!!!!!!!
 *
 *  Because Fossil only uses public functions, and the "exact" parameter
 *  is always 0, everything not needed is stripped off.
 *
 * FOSSIL MODIFICATION!!!!!!!!!!!!!!!
 *
 * Side effects:
 *	Sets the stub table pointers.
 *
 *----------------------------------------------------------------------
 */
#undef Tcl_InitStubs

typedef struct {
    char *unused1;
    Tcl_FreeProc *unused2;
    int unused3;
    const TclStubs *stubTable;
} Interp;

const char *
Tcl_InitStubs(
    Tcl_Interp *interp,
    const char *version,
    int exact)
{
    tclStubsPtr = ((Interp *) interp)->stubTable;

    assert(tclStubsPtr && (tclStubsPtr->magic == TCL_STUB_MAGIC));

    return Tcl_PkgRequireEx(interp, "Tcl", version, exact,
        (void *)&tclStubsPtr);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
