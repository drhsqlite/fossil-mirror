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
#include "winfile.h"

#ifndef LABEL_SECURITY_INFORMATION
#   define LABEL_SECURITY_INFORMATION (0x00000010L)
#endif

/*
** Fill stat buf with information received from stat() or lstat().
** lstat() is called on Unix if eFType is RepoFile and the allow-symlinks
** setting is on.  But as windows does not support symbolic links, the
** eFType parameter is ignored here.
*/
int win32_stat(const wchar_t *zFilename, struct fossilStat *buf, int eFType){
  WIN32_FILE_ATTRIBUTE_DATA attr;
  int rc = GetFileAttributesExW(zFilename, GetFileExInfoStandard, &attr);
  if( rc ){
    ULARGE_INTEGER ull;
    ull.LowPart = attr.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = attr.ftLastWriteTime.dwHighDateTime;
    buf->st_mode = (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
                   S_IFDIR : S_IFREG;
    buf->st_size = (((i64)attr.nFileSizeHigh)<<32) | attr.nFileSizeLow;
    buf->st_mtime = ull.QuadPart / 10000000ULL - 11644473600ULL;
  }
  return !rc;
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
  if( !accessYesNo ){
    rc = -1;
  }

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
  zUtf8 = fossil_path_to_utf8(zWide);
  fossil_free(zWide);
  for(i=0; zUtf8[i]; i++) if( zUtf8[i]=='\\' ) zUtf8[i] = '/';
  strncpy(zBuf, zUtf8, nBuf);
  fossil_path_free(zUtf8);
}

/* Perform case-insensitive comparison of two UTF-16 file names. Try to load the
** CompareStringOrdinal() function on Windows Vista and newer, and resort to the
** RtlEqualUnicodeString() function on Windows XP.
** The dance to invoke RtlEqualUnicodeString() is necessary because lstrcmpiW()
** performs linguistic comparison, while the former performs binary comparison.
** As an example, matching "ß" (U+00DF Latin Small Letter Sharp S) with "ss" is
** undesirable in file name comparison, so lstrcmpiW() is only invoked in cases
** that are technically impossible and contradicting all known laws of physics.
*/
int win32_filenames_equal_nocase(
  const wchar_t *fn1,
  const wchar_t *fn2
){
  /* ---- Data types used by dynamically loaded API functions. -------------- */
  typedef struct { /* UNICODE_STRING from <ntdef.h> */
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
  } MY_UNICODE_STRING;
  /* ---- Prototypes for dynamically loaded API functions. ------------------ */
  typedef int (WINAPI *FNCOMPARESTRINGORDINAL)(LPCWCH,int,LPCWCH,int,BOOL);
  typedef VOID (NTAPI *FNRTLINITUNICODESTRING)(MY_UNICODE_STRING*,PCWSTR);
  typedef BOOLEAN (NTAPI *FNRTLEQUALUNICODESTRING)
    (MY_UNICODE_STRING*,MY_UNICODE_STRING*,BOOLEAN);
  /* ------------------------------------------------------------------------ */
  static FNCOMPARESTRINGORDINAL fnCompareStringOrdinal;
  static FNRTLINITUNICODESTRING fnRtlInitUnicodeString;
  static FNRTLEQUALUNICODESTRING fnRtlEqualUnicodeString;
  static int loaded_CompareStringOrdinal;
  static int loaded_RtlUnicodeStringAPIs;
  if( !loaded_CompareStringOrdinal ){
    fnCompareStringOrdinal = (FNCOMPARESTRINGORDINAL)
      GetProcAddress(GetModuleHandleA("kernel32"),"CompareStringOrdinal");
    loaded_CompareStringOrdinal = 1;
  }
  if( fnCompareStringOrdinal ){
    return fnCompareStringOrdinal(fn1,-1,fn2,-1,1)-2==0;
  }
  if( !loaded_RtlUnicodeStringAPIs ){
    fnRtlInitUnicodeString = (FNRTLINITUNICODESTRING)
      GetProcAddress(GetModuleHandleA("ntdll"),"RtlInitUnicodeString");
    fnRtlEqualUnicodeString = (FNRTLEQUALUNICODESTRING)
      GetProcAddress(GetModuleHandleA("ntdll"),"RtlEqualUnicodeString");
    loaded_RtlUnicodeStringAPIs = 1;
  }
  if( fnRtlInitUnicodeString && fnRtlEqualUnicodeString ){
    MY_UNICODE_STRING u1, u2;
    fnRtlInitUnicodeString(&u1,fn1);
    fnRtlInitUnicodeString(&u2,fn2);
    return (BOOLEAN/*unsigned char*/)fnRtlEqualUnicodeString(&u1,&u2,1);
  }
  /* In what kind of strange parallel universe are we? */
  return lstrcmpiW(fn1,fn2)==0;
}

/* Helper macros to deal with directory separators. */
#define IS_DIRSEP(s,i) ( s[i]=='/' || s[i]=='\\' )
#define NEXT_DIRSEP(s,i) while( s[i] && s[i]!='/' && s[i]!='\\' ){i++;}

/* The Win32 version of file_case_preferred_name() from file.c, which is able to
** find case-preserved file names containing non-ASCII characters. The result is
** allocated by fossil_malloc() and *should* be free'd by the caller. While this
** function usually gets canonicalized paths, it is able to handle any input and
** figure out more cases than the original:
**
**    fossil test-case-filename C:/ .//..\WINDOWS\/.//.\SYSTEM32\.\NOTEPAD.EXE
**    → Original:   .//..\WINDOWS\/.//.\SYSTEM32\.\NOTEPAD.EXE
**    → Modified:   .//..\Windows\/.//.\System32\.\notepad.exe
**
**    md ÄÖÜ
**    fossil test-case-filename ./\ .\äöü\/[empty]\\/
**    → Original:   ./äöü\/[empty]\\/
**    → Modified:   .\ÄÖÜ\/[empty]\\/
**
** The function preserves slashes and backslashes: only single file or directory
** components without directory separators ("basenames") are converted to UTF-16
** using fossil_utf8_to_path(), so bypassing its slash ↔ backslash translations.
** Note that the original function doesn't preserve all slashes and backslashes,
** for example in the second example above.
**
** NOTE: As of Windows 10, version 1803, case sensitivity may be enabled on a
** per-directory basis, as returned by NtQueryInformationFile() with the file
** information class FILE_CASE_SENSITIVE_INFORMATION. So this function may be
** changed to act like fossil_strdup() for files located in such directories.
*/
char *win32_file_case_preferred_name(
  const char *zBase,
  const char *zPath
){
  int cchBase;
  int cchPath;
  int cchBuf;
  int cchRes;
  char *zBuf;
  char *zRes;
  int ncUsed;
  int i, j;
  if( filenames_are_case_sensitive() ){
    return fossil_strdup(zPath);
  }
  cchBase = strlen(zBase);
  cchPath = strlen(zPath);
  cchBuf = cchBase + cchPath + 2; /* + NULL + optional directory slash */
  cchRes = cchPath + 1;           /* + NULL */
  zBuf = fossil_malloc(cchBuf);
  zRes = fossil_malloc(cchRes);
  ncUsed = 0;
  memcpy(zBuf,zBase,cchBase);
  if( !IS_DIRSEP(zBuf,cchBase-1) ){
    zBuf[cchBase++]=L'/';
  }
  memcpy(zBuf+cchBase,zPath,cchPath+1);
  i = j = cchBase;
  while( 1 ){
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    wchar_t *wzBuf;
    char *zCompBuf = 0;
    char *zComp = &zBuf[i];
    int cchComp;
    char chSep;
    int fDone;
    if( IS_DIRSEP(zBuf,i) ){
      if( ncUsed+2>cchRes ){  /* Directory slash + NULL*/
        cchRes += 32;         /* Overprovisioning. */
        zRes = fossil_realloc(zRes,cchRes);
      }
      zRes[ncUsed++] = zBuf[i];
      i = j = i+1;
      continue;
    }
    NEXT_DIRSEP(zBuf,j);
    fDone = zBuf[j]==0;
    chSep = zBuf[j];
    zBuf[j] = 0;                /* Truncate working buffer. */
    wzBuf = fossil_utf8_to_path(zBuf,0);
    hFind = FindFirstFileW(wzBuf,&fd);
    if( hFind!=INVALID_HANDLE_VALUE ){
      wchar_t *wzComp = fossil_utf8_to_path(zComp,0);
      FindClose(hFind);
      /* Test fd.cFileName, not fd.cAlternateFileName (classic 8.3 format). */
      if( win32_filenames_equal_nocase(wzComp,fd.cFileName) ){
        zCompBuf = fossil_path_to_utf8(fd.cFileName);
        zComp = zCompBuf;
      }
      fossil_path_free(wzComp);
    }
    fossil_path_free(wzBuf);
    cchComp = strlen(zComp);
    if( ncUsed+cchComp+1>cchRes ){  /* Current component + NULL */
      cchRes += cchComp + 32;       /* Overprovisioning. */
      zRes = fossil_realloc(zRes,cchRes);
    }
    memcpy(zRes+ncUsed,zComp,cchComp);
    ncUsed += cchComp;
    if( zCompBuf ){
      fossil_path_free(zCompBuf);
    }
    if( fDone ){
      zRes[ncUsed] = 0;
      break;
    }
    zBuf[j] = chSep;            /* Undo working buffer truncation. */
    i = j;
  }
  fossil_free(zBuf);
  return zRes;
}

/* Return the unique identifier (UID) for a file, made up of the file identifier
** (equal to "inode" for Unix-style file systems) plus the volume serial number.
** Call the GetFileInformationByHandleEx() function on Windows Vista, and resort
** to the GetFileInformationByHandle() function on Windows XP. The result string
** is allocated by mprintf(), or NULL on failure.
*/
char *win32_file_id(
  const char *zFileName
){
  /* ---- Data types used by dynamically loaded API functions. -------------- */
  typedef struct { /* FILE_ID_INFO from <winbase.h> */
    ULONGLONG VolumeSerialNumber;
    BYTE FileId[16];
  } MY_FILE_ID_INFO;
  /* ---- Prototypes for dynamically loaded API functions. ------------------ */
  typedef int (WINAPI *FNGETFILEINFORMATIONBYHANDLEEX)
    (HANDLE,int/*enum*/,MY_FILE_ID_INFO*,DWORD);
  /* ------------------------------------------------------------------------ */
  static FNGETFILEINFORMATIONBYHANDLEEX fnGetFileInformationByHandleEx;
  static int loaded_fnGetFileInformationByHandleEx;
  wchar_t *wzFileName = fossil_utf8_to_path(zFileName,0);
  HANDLE hFile;
  char *zFileId = 0;
  hFile = CreateFileW(
            wzFileName,
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            NULL);
  if( hFile!=INVALID_HANDLE_VALUE ){
    BY_HANDLE_FILE_INFORMATION fi;
    MY_FILE_ID_INFO fi2;
    if( !loaded_fnGetFileInformationByHandleEx ){
      fnGetFileInformationByHandleEx = (FNGETFILEINFORMATIONBYHANDLEEX)
        GetProcAddress(
          GetModuleHandleA("kernel32"),"GetFileInformationByHandleEx");
      loaded_fnGetFileInformationByHandleEx = 1;
    }
    if( fnGetFileInformationByHandleEx ){
      if( fnGetFileInformationByHandleEx(
            hFile,/*FileIdInfo*/0x12,&fi2,sizeof(fi2)) ){
        zFileId = mprintf(
                    "%016llx/"
                      "%02x%02x%02x%02x%02x%02x%02x%02x"
                      "%02x%02x%02x%02x%02x%02x%02x%02x",
                    fi2.VolumeSerialNumber,
                    fi2.FileId[15], fi2.FileId[14],
                    fi2.FileId[13], fi2.FileId[12],
                    fi2.FileId[11], fi2.FileId[10],
                    fi2.FileId[9],  fi2.FileId[8],
                    fi2.FileId[7],  fi2.FileId[6],
                    fi2.FileId[5],  fi2.FileId[4],
                    fi2.FileId[3],  fi2.FileId[2],
                    fi2.FileId[1],  fi2.FileId[0]);
      }
    }
    if( zFileId==0 ){
      if( GetFileInformationByHandle(hFile,&fi) ){
        ULARGE_INTEGER FileId = {{
          /*.LowPart = */ fi.nFileIndexLow,
          /*.HighPart = */ fi.nFileIndexHigh
        }};
        zFileId = mprintf(
                    "%08x/%016llx",
                    fi.dwVolumeSerialNumber,(u64)FileId.QuadPart);
      }
    }
    CloseHandle(hFile);
  }
  fossil_path_free(wzFileName);
  return zFileId;
}
#endif /* _WIN32  -- This code is for win32 only */
