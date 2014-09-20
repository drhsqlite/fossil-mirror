/*
** Copyright (c) 2006 D. Richard Hipp
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
** This file implements several non-trivial file handling wrapper functions
** on Windows using the Win32 API.
*/
#include "config.h"
#ifdef _WIN32
/* This code is for win32 only */
#include <sys/stat.h>
#include <windows.h>
#include <versionhelpers.h>
#include "winfile.h"

#ifndef LABEL_SECURITY_INFORMATION
#   define LABEL_SECURITY_INFORMATION (0x00000010L)
#endif

/* copy & paste from ntifs.h */
typedef struct _REPARSE_DATA_BUFFER {
  ULONG  ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG  Flags;
      WCHAR  PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR  PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  };
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

#define LINK_BUFFER_SIZE 1024

int win32_lstat(const wchar_t *zFilename, struct fossilStat *buf){
  WIN32_FILE_ATTRIBUTE_DATA attr;
  int rc = GetFileAttributesExW(zFilename, GetFileExInfoStandard, &attr);
  if( rc ){
    char *tname = fossil_filename_to_utf8(zFilename);
    char tlink[LINK_BUFFER_SIZE];
    ssize_t tlen = win32_readlink(tname, tlink, sizeof(tlink));
    ULARGE_INTEGER ull;

    buf->st_mode = (tlen > 0) ? S_IFLNK :
                   ((attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
                     S_IFDIR : S_IFREG);
    
    buf->st_size = (((i64)attr.nFileSizeHigh)<<32) | attr.nFileSizeLow;
    
    ull.LowPart = attr.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = attr.ftLastWriteTime.dwHighDateTime;
    buf->st_mtime = ull.QuadPart / 10000000ULL - 11644473600ULL;
  }
  return !rc;
}

/*
** Fill stat buf with information received from stat() or lstat().
** lstat() is called on Unix if isWd is TRUE and allow-symlinks setting is on.
**
*/
int win32_stat(const wchar_t *zFilename, struct fossilStat *buf){
  int rc;
  HANDLE file;
  wchar_t nextFilename[LINK_BUFFER_SIZE];
  DWORD len;
  
  while (1){
    rc = win32_lstat(zFilename, buf);
    /* exit on error or not link */
    if ((rc != 0) || (buf->st_mode != S_IFLNK))
      break;

    /* it is a link, so open the linked file */      
    file = CreateFileW(zFilename, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if ((file == NULL) || (file == INVALID_HANDLE_VALUE)){
      rc = -1;
      break;
    }

    /* get the final path name and close the handle */
    len = GetFinalPathNameByHandleW(file, nextFilename, LINK_BUFFER_SIZE - 1, 0);
    CloseHandle(file);
    
    /* if any problems getting the final path name error so exit */
    if ((len <= 0) || (len > LINK_BUFFER_SIZE - 1)){
      rc = -1;
      break;
    }
    
    /* prepare to try again just in case we have a chain to follow */
    /* this shouldn't happen, but just trying to be safe */
    zFilename = nextFilename;
  }
  
  return rc;
}

ssize_t win32_readlink(const char *path, char *buf, size_t bufsiz){
  /* assume we're going to fail */
  ssize_t rv = -1;
  
  /* does path reference a reparse point? */
  WIN32_FILE_ATTRIBUTE_DATA attr;
  int rc = GetFileAttributesEx(path, GetFileExInfoStandard, &attr);
  if (rc && (attr.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)){
  
    /* since it is a reparse point, open it */
    HANDLE file = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 
      FILE_FLAG_OPEN_REPARSE_POINT, NULL);      
    if ((file != NULL) && (file != INVALID_HANDLE_VALUE)){

      /* use DeviceIoControl to get the reparse point data */
    
      union {
        REPARSE_DATA_BUFFER data;
        char buffer[sizeof(REPARSE_DATA_BUFFER) + LINK_BUFFER_SIZE * sizeof(wchar_t)];
      } u;
      DWORD bytes;
      
      u.data.ReparseTag = IO_REPARSE_TAG_SYMLINK;
      u.data.ReparseDataLength = 0;
      u.data.Reserved = 0;
    
      int rc = DeviceIoControl(file, FSCTL_GET_REPARSE_POINT, NULL, 0,
        &u, sizeof(u), &bytes, NULL);

      /* did the reparse point data fit into the desired buffer? */
      if (rc && (bytes < sizeof(u))){
        /* it fit, so setup the print name for further processing */
        USHORT
          offset = u.data.SymbolicLinkReparseBuffer.PrintNameOffset / sizeof(wchar_t),
          length = u.data.SymbolicLinkReparseBuffer.PrintNameLength / sizeof(wchar_t);
        char *temp;
        u.data.SymbolicLinkReparseBuffer.PathBuffer[offset + length] = 0;

        /* convert the filename to utf8, copy it, and discard the converted copy */
        temp = fossil_filename_to_utf8(u.data.SymbolicLinkReparseBuffer.PathBuffer + offset);
        rv = strlen(temp);
        if (rv >= bufsiz)
          rv = bufsiz;
        memcpy(buf, temp, rv);
        fossil_filename_free(temp);
      }
      
      /* all done, close the reparse point */
      CloseHandle(file);
    }
  }

  return rv;
}

int win32_symlink(const char *oldpath, const char *newpath){
  fossilStat stat;
  int created = 0;
  DWORD flags = 0;
  wchar_t *zMbcs;

  /* does oldpath exist? is it a dir or a file? */  
  zMbcs = fossil_utf8_to_filename(oldpath);
  if (win32_stat(zMbcs, &stat) == 0){
    if (stat.st_mode == S_IFDIR)
      flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
  }
  fossil_filename_free(zMbcs);

  /* remove newpath before creating the symlink */
  zMbcs = fossil_utf8_to_filename(newpath);
  if (win32_stat(zMbcs, &stat) == 0){
    if (stat.st_mode == S_IFDIR)
      RemoveDirectory(newpath);
    else
      DeleteFile(newpath);
  }
  fossil_filename_free(zMbcs);

  if (CreateSymbolicLink(newpath, oldpath, flags))
    created = 1;

  /* if the symlink was not created, create a plain text file */
  if (!created){
    Blob content;
    blob_set(&content, oldpath);
    blob_write_to_file(&content, newpath);
    blob_reset(&content);
    created = 1;
  }
  
  return created ? 0 : -1;
}

int win32_symlinks_supported(){
  TOKEN_PRIVILEGES tp;
  LUID luid;
  HANDLE process, token;
  DWORD status;

  /* symlinks only supported on vista or greater */
  if (!IsWindowsVistaOrGreater())
    return 0;
  
  /* next we need to check to see if the privilege is available */
  
  /* can't check privilege if we can't lookup its value */
  if (!LookupPrivilegeValue(NULL, SE_CREATE_SYMBOLIC_LINK_NAME, &luid))
    return 0;
  
  /* can't check privilege if we can't open the process token */
  process = GetCurrentProcess();
  if (!OpenProcessToken(process, TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &token))
    return 0;
  
  /* by this point, we have a process token and the privilege value */
  /* try to enable the privilege then close the token */
  
  tp.PrivilegeCount = 1;
  tp.Privileges[0].Luid = luid;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  
  AdjustTokenPrivileges(token, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
  status = GetLastError();
  
  CloseHandle(token);

  /* any error means we failed to enable the privilege, symlinks not supported */
  if (status != ERROR_SUCCESS)
    return 0;
  
  /* we made it this far, symlinks must be supported */
  return 1;  
}

/*
** Wrapper around the access() system call.  This code was copied from Tcl
** 8.6 and then modified.
*/
int win32_access(const wchar_t *zFilename, int flags){
  int rc = 0;
  PSECURITY_DESCRIPTOR pSd = NULL;
  unsigned long size = 0;
  PSID pSid = NULL;
  BOOL sidDefaulted;
  BOOL impersonated = FALSE;
  SID_IDENTIFIER_AUTHORITY unmapped = {{0, 0, 0, 0, 0, 22}};
  GENERIC_MAPPING genMap;
  HANDLE hToken = NULL;
  DWORD desiredAccess = 0, grantedAccess = 0;
  BOOL accessYesNo = FALSE;
  PPRIVILEGE_SET pPrivSet = NULL;
  DWORD privSetSize = 0;
  DWORD attr = GetFileAttributesW(zFilename);

  if( attr==INVALID_FILE_ATTRIBUTES ){
    /*
     * File might not exist.
     */

    if( GetLastError()!=ERROR_SHARING_VIOLATION ){
      rc = -1; goto done;
    }
  }

  if( flags==F_OK ){
    /*
     * File exists, nothing else to check.
     */

    goto done;
  }

  if( (flags & W_OK)
      && (attr & FILE_ATTRIBUTE_READONLY)
      && !(attr & FILE_ATTRIBUTE_DIRECTORY) ){
    /*
     * The attributes say the file is not writable.  If the file is a
     * regular file (i.e., not a directory), then the file is not
     * writable, full stop.  For directories, the read-only bit is
     * (mostly) ignored by Windows, so we can't ascertain anything about
     * directory access from the attrib data.
     */

    rc = -1; goto done;
  }

  /*
   * It looks as if the permissions are ok, but if we are on NT, 2000 or XP,
   * we have a more complex permissions structure so we try to check that.
   * The code below is remarkably complex for such a simple thing as finding
   * what permissions the OS has set for a file.
   */

  /*
   * First find out how big the buffer needs to be.
   */

  GetFileSecurityW(zFilename,
      OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
      DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
      0, 0, &size);

  /*
   * Should have failed with ERROR_INSUFFICIENT_BUFFER
   */

  if( GetLastError()!=ERROR_INSUFFICIENT_BUFFER ){
    /*
     * Most likely case is ERROR_ACCESS_DENIED, which we will convert to
     * EACCES - just what we want!
     */

    rc = -1; goto done;
  }

  /*
   * Now size contains the size of buffer needed.
   */

  pSd = (PSECURITY_DESCRIPTOR)HeapAlloc(GetProcessHeap(), 0, size);

  if( pSd==NULL ){
    rc = -1; goto done;
  }

  /*
   * Call GetFileSecurity() for real.
   */

  if( !GetFileSecurityW(zFilename,
          OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
          DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
          pSd, size, &size) ){
    /*
     * Error getting owner SD
     */

    rc = -1; goto done;
  }

  /*
   * As of Samba 3.0.23 (10-Jul-2006), unmapped users and groups are
   * assigned to SID domains S-1-22-1 and S-1-22-2, where "22" is the
   * top-level authority.  If the file owner and group is unmapped then
   * the ACL access check below will only test against world access,
   * which is likely to be more restrictive than the actual access
   * restrictions.  Since the ACL tests are more likely wrong than
   * right, skip them.  Moreover, the unix owner access permissions are
   * usually mapped to the Windows attributes, so if the user is the
   * file owner then the attrib checks above are correct (as far as they
   * go).
   */

  if( !GetSecurityDescriptorOwner(pSd, &pSid, &sidDefaulted) ||
      memcmp(GetSidIdentifierAuthority(pSid), &unmapped,
             sizeof(SID_IDENTIFIER_AUTHORITY))==0 ){
    goto done; /* Attrib tests say access allowed. */
  }

  /*
   * Perform security impersonation of the user and open the resulting
   * thread token.
   */

  if( !ImpersonateSelf(SecurityImpersonation) ){
    /*
     * Unable to perform security impersonation.
     */

    rc = -1; goto done;
  }
  impersonated = TRUE;

  if( !OpenThreadToken(GetCurrentThread(),
      TOKEN_DUPLICATE | TOKEN_QUERY, FALSE, &hToken) ){
    /*
     * Unable to get current thread's token.
     */

    rc = -1; goto done;
  }

  /*
   * Setup desiredAccess according to the access priveleges we are
   * checking.
   */

  if( flags & R_OK ){
    desiredAccess |= FILE_GENERIC_READ;
  }
  if( flags & W_OK){
    desiredAccess |= FILE_GENERIC_WRITE;
  }

  memset(&genMap, 0, sizeof(GENERIC_MAPPING));
  genMap.GenericRead = FILE_GENERIC_READ;
  genMap.GenericWrite = FILE_GENERIC_WRITE;
  genMap.GenericExecute = FILE_GENERIC_EXECUTE;
  genMap.GenericAll = FILE_ALL_ACCESS;

  AccessCheck(pSd, hToken, desiredAccess, &genMap, 0,
                   &privSetSize, &grantedAccess, &accessYesNo);
  /*
   * Should have failed with ERROR_INSUFFICIENT_BUFFER
   */

  if( GetLastError()!=ERROR_INSUFFICIENT_BUFFER ){
    rc = -1; goto done;
  }
  pPrivSet = (PPRIVILEGE_SET)HeapAlloc(GetProcessHeap(), 0, privSetSize);

  if( pPrivSet==NULL ){
    rc = -1; goto done;
  }

  /*
   * Perform access check using the token.
   */

  if( !AccessCheck(pSd, hToken, desiredAccess, &genMap, pPrivSet,
                   &privSetSize, &grantedAccess, &accessYesNo) ){
    /*
     * Unable to perform access check.
     */

    rc = -1; goto done;
  }
  if( !accessYesNo ) rc = -1;

done:

  if( hToken != NULL ){
    CloseHandle(hToken);
  }
  if( impersonated ){
    RevertToSelf();
    impersonated = FALSE;
  }
  if( pPrivSet!=NULL ){
    HeapFree(GetProcessHeap(), 0, pPrivSet);
  }
  if( pSd!=NULL ){
    HeapFree(GetProcessHeap(), 0, pSd);
  }
  return rc;
}

/*
** Wrapper around the chdir() system call.
*/
int win32_chdir(const wchar_t *zChDir, int bChroot){
  int rc = (int)!SetCurrentDirectoryW(zChDir);
  return rc;
}

/*
** Get the current working directory.
**
** On windows, the name is converted from unicode to UTF8 and all '\\'
** characters are converted to '/'.
*/
void win32_getcwd(char *zBuf, int nBuf){
  int i;
  char *zUtf8;
  wchar_t *zWide = fossil_malloc( sizeof(wchar_t)*nBuf );
  if( GetCurrentDirectoryW(nBuf, zWide)==0 ){
    fossil_fatal("cannot find current working directory.");
  }
  zUtf8 = fossil_filename_to_utf8(zWide);
  fossil_free(zWide);
  for(i=0; zUtf8[i]; i++) if( zUtf8[i]=='\\' ) zUtf8[i] = '/';
  strncpy(zBuf, zUtf8, nBuf);
  fossil_filename_free(zUtf8);
}
#endif /* _WIN32  -- This code is for win32 only */
