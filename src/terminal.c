/*
** Copyright (c) 2020 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code used to query terminal info
*/

#include "config.h"
#include "terminal.h"
#include <assert.h>
#ifdef _WIN32
# include <windows.h>
#else
#ifdef __EXTENSIONS__
#include <termio.h>
#endif
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#endif



#if INTERFACE
/*
** Terminal size defined in terms of columns and lines.
*/
struct TerminalSize {
  unsigned int nColumns;         /* Number of characters on a single line */
  unsigned int nLines;           /* Number of lines */
};
#endif


/* Get the current terminal size by calling a system service.
**
** Return 1 on success. This sets the size parameters to the values retured by
** the system call, when such is supported; set the size to zero otherwise.
** Return 0 on the system service call failure.
**
** Under Linux/bash the size info is also available from env $LINES, $COLUMNS.
** Or it can be queried using tput `echo -e "lines\ncols"|tput -S`.
** Technically, this info could be cached, but then we'd need to handle
** SIGWINCH signal to requery the terminal on resize event.
*/
int terminal_get_size(TerminalSize *t){
  memset(t, 0, sizeof(*t));

#if defined(TIOCGSIZE)
  {
    struct ttysize ts;
    if( ioctl(STDIN_FILENO, TIOCGSIZE, &ts)>=0
     || ioctl(STDOUT_FILENO, TIOCGSIZE, &ts)>=0
     || ioctl(STDERR_FILENO, TIOCGSIZE, &ts)>=0
    ){
      t->nColumns = ts.ts_cols;
      t->nLines = ts.ts_lines;
      return 1;
    }
    return 0;
  }
#elif defined(TIOCGWINSZ)
  {
    struct winsize ws;
    if( ioctl(STDIN_FILENO, TIOCGWINSZ, &ws)>=0
     || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)>=0
     || ioctl(STDERR_FILENO, TIOCGWINSZ, &ws)>=0
    ){
      t->nColumns = ws.ws_col;
      t->nLines = ws.ws_row;
      return 1;
    }
    return 0;
  }
#elif defined(_WIN32)
  {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if( GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)
     || GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi)
     || GetConsoleScreenBufferInfo(GetStdHandle(STD_INPUT_HANDLE), &csbi)
    ){
      t->nColumns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
      t->nLines = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
      return 1;
    }
    return 0;
  }
#else
  return 1;
#endif
}

/*
** Return the terminal's current width in columns when available, otherwise
** return the specified default value.
*/
unsigned int terminal_get_width(unsigned int nDefault){
  TerminalSize ts;
  if( terminal_get_size(&ts) ){
    return ts.nColumns;
  }
  return nDefault;
}

/*
** Return the terminal's current height in lines when available, otherwise
** return the specified default value.
*/
unsigned int terminal_get_height(unsigned int nDefault){
  TerminalSize ts;
  if( terminal_get_size(&ts) ){
    return ts.nLines;
  }
  return nDefault;
}

/*
** COMMAND: test-terminal-size
**
** Show the size of the terminal window from which the command is launched
** as two integers, the width in characters and the height in lines.
**
** If the size cannot be determined, two zeros are shown.
*/
void test_terminal_size_cmd(void){
  TerminalSize ts;
  terminal_get_size(&ts);
  fossil_print("%d %d\n", ts.nColumns, ts.nLines);
}

/*
** Return true if it is reasonable is emit VT100 escape codes.
*/
int terminal_is_vt100(void){
  char *zNoColor;
#ifdef _WIN32
  if( !win32_terminal_is_vt100(1) ) return 0;
#endif /* _WIN32 */
  if( !fossil_isatty(1) ) return 0;
  zNoColor =fossil_getenv("NO_COLOR");
  if( zNoColor==0 ) return 1;
  if( zNoColor[0]==0 ) return 1;
  if( is_false(zNoColor) ) return 1;
  return 0;
}

#ifdef _WIN32
/*
** Return true if the Windows console supports VT100 escape codes.
**
** Support for VT100 escape codes is enabled by default in Windows Terminal
** on Windows 10 and Windows 11, and disabled by default in Legacy Consoles
** and on older versions of Windows. Programs can turn on VT100 support for
** Legacy Consoles using the ENABLE_VIRTUAL_TERMINAL_PROCESSING flag.
**
** NOTE: If this function needs to be called in more complex scenarios with
** reassigned stdout and stderr streams, the following CRT calls are useful
** to translate from CRT streams to file descriptors and to Win32 handles:
**
**    HANDLE hOutputHandle = (HANDLE)_get_osfhandle(_fileno(<FILE*>));
*/
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
int win32_terminal_is_vt100(int fd){
  HANDLE hConsole = NULL;
  DWORD dwConsoleMode;
  switch( fd ){
    case 1:
      hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
      break;
    case 2:
      hConsole = GetStdHandle(STD_ERROR_HANDLE);
      break;
  }
  if( GetConsoleMode(hConsole,&dwConsoleMode) ){
    return (dwConsoleMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)!=0;
  }
  return 0;
}
#endif /* _WIN32 */
