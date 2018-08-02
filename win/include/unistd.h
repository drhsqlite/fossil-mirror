#ifndef _UNISTD_H
#define _UNISTD_H	 1

/* This file intended to serve as a drop-in replacement for
 *  unistd.h on Windows
 *  Please add functionality as neeeded
 */

#include <stdlib.h>
#include <io.h>
#define srandom srand
#define random rand
#if defined(__DMC__)
#endif

#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS 1

#ifndef F_OK
#define F_OK 0
#endif /* not F_OK */

#ifndef X_OK
#define X_OK 1
#endif /* not X_OK */

#ifndef W_OK
#define W_OK 2
#endif /* not W_OK */

#ifndef R_OK
#define R_OK 4
#endif /* not R_OK */

#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)



#endif

#define access _access
#define ftruncate _chsize

#define ssize_t int

#endif /* unistd.h  */
