/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code that implements the client-side HTTP protocol
*/
#include "config.h"
#include "http.h"
#include <assert.h>

#ifdef _WIN32
#include <io.h>
#endif


#if INTERFACE
/*
** Bits of the mHttpFlags parameter to http_exchange()
*/
#define HTTP_USE_LOGIN   0x00001     /* Add a login card to the sync message */
#define HTTP_GENERIC     0x00002     /* Generic HTTP request */
#define HTTP_VERBOSE     0x00004     /* HTTP status messages */
#define HTTP_QUIET       0x00008     /* No surplus output */
#define HTTP_NOCOMPRESS  0x00010     /* Omit payload compression */
#endif

/* Maximum number of HTTP Authorization attempts */
#define MAX_HTTP_AUTH 2

/* Keep track of HTTP Basic Authorization failures */
static int fSeenHttpAuth = 0;

/* The N value for most recent http-request-N.txt and http-reply-N.txt
** trace files.
*/
static int traceCnt = 0;

/*
** Construct the "login" card with the client credentials.
**
**       login LOGIN NONCE SIGNATURE
**
** The LOGIN is the user id of the client.  NONCE is the sha1 checksum
** of all payload that follows the login card.  SIGNATURE is the sha1
** checksum of the nonce followed by the user password.
**
** Write the constructed login card into pLogin.  pLogin is initialized
** by this routine.
*/
static void http_build_login_card(Blob *pPayload, Blob *pLogin){
  Blob nonce;          /* The nonce */
  const char *zLogin;  /* The user login name */
  const char *zPw;     /* The user password */
  Blob pw;             /* The nonce with user password appended */
  Blob sig;            /* The signature field */

  blob_zero(pLogin);
  if( g.url.user==0 || fossil_strcmp(g.url.user, "anonymous")==0 ){
     return;  /* If no login card for users "nobody" and "anonymous" */
  }
  if( g.url.isSsh ){
     return;  /* If no login card for SSH: */
  }
  blob_zero(&nonce);
  blob_zero(&pw);
  sha1sum_blob(pPayload, &nonce);
  blob_copy(&pw, &nonce);
  zLogin = g.url.user;
  if( g.url.passwd ){
    zPw = g.url.passwd;
  }else if( g.cgiOutput ){
    /* Password failure while doing a sync from the web interface */
    cgi_printf("*** incorrect or missing password for user %h\n", zLogin);
    zPw = 0;
  }else{
    /* Password failure while doing a sync from the command-line interface */
    url_prompt_for_password();
    zPw = g.url.passwd;
  }

  /* The login card wants the SHA1 hash of the password (as computed by
  ** sha1_shared_secret()), not the original password.  So convert the
  ** password to its SHA1 encoding if it isn't already a SHA1 hash.
  **
  ** We assume that a hexadecimal string of exactly 40 characters is a
  ** SHA1 hash, not an original password.  If a user has a password which
  ** just happens to be a 40-character hex string, then this routine won't
  ** be able to distinguish it from a hash, the translation will not be
  ** performed, and the sync won't work.
  */
  if( zPw && zPw[0] && (strlen(zPw)!=40 || !validate16(zPw,40)) ){
    const char *zProjectCode = 0;
    if( g.url.flags & URL_USE_PARENT ){
      zProjectCode = db_get("parent-project-code", 0);
    }else{
      zProjectCode = db_get("project-code", 0);
    }
    zPw = sha1_shared_secret(zPw, zLogin, zProjectCode);
    if( g.url.pwConfig!=0 && (g.url.flags & URL_REMEMBER_PW)!=0 ){
      char *x = obscure(zPw);
      db_set(g.url.pwConfig/*works-like:"x"*/, x, 0);
      fossil_free(x);
    }
    fossil_free(g.url.passwd);
    g.url.passwd = fossil_strdup(zPw);
  }

  blob_append(&pw, zPw, -1);
  sha1sum_blob(&pw, &sig);
  blob_appendf(pLogin, "login %F %b %b\n", zLogin, &nonce, &sig);
  blob_reset(&pw);
  blob_reset(&sig);
  blob_reset(&nonce);
}

/*
** Construct an appropriate HTTP request header.  Write the header
** into pHdr.  This routine initializes the pHdr blob.  pPayload is
** the complete payload (including the login card) already compressed.
*/
static void http_build_header(
  Blob *pPayload,              /* the payload that will be sent */
  Blob *pHdr,                  /* construct the header here */
  const char *zAltMimetype     /* Alternative mimetype */
){
  int nPayload = pPayload ? blob_size(pPayload) : 0;

  blob_zero(pHdr);
  blob_appendf(pHdr, "%s %s%s HTTP/1.0\r\n",
               nPayload>0 ? "POST" : "GET", g.url.path,
               g.url.path[0]==0 ? "/" : "");
  if( g.url.proxyAuth ){
    blob_appendf(pHdr, "Proxy-Authorization: %s\r\n", g.url.proxyAuth);
  }
  if( g.zHttpAuth && g.zHttpAuth[0] ){
    const char *zCredentials = g.zHttpAuth;
    char *zEncoded = encode64(zCredentials, -1);
    blob_appendf(pHdr, "Authorization: Basic %s\r\n", zEncoded);
    fossil_free(zEncoded);
  }
  blob_appendf(pHdr, "Host: %s\r\n", g.url.hostname);
  blob_appendf(pHdr, "User-Agent: %s\r\n", get_user_agent());
  if( g.url.isSsh ) blob_appendf(pHdr, "X-Fossil-Transport: SSH\r\n");
  if( nPayload ){
    if( zAltMimetype ){
      blob_appendf(pHdr, "Content-Type: %s\r\n", zAltMimetype);
    }else if( g.fHttpTrace ){
      blob_appendf(pHdr, "Content-Type: application/x-fossil-debug\r\n");
    }else{
      blob_appendf(pHdr, "Content-Type: application/x-fossil\r\n");
    }
    blob_appendf(pHdr, "Content-Length: %d\r\n", blob_size(pPayload));
  }
  blob_append(pHdr, "\r\n", 2);
}

/*
** Use Fossil credentials for HTTP Basic Authorization prompt
*/
static int use_fossil_creds_for_httpauth_prompt(void){
  Blob x;
  char c;
  prompt_user("Use Fossil username and password (y/N)? ", &x);
  c = blob_str(&x)[0];
  blob_reset(&x);
  return ( c=='y' || c=='Y' );
}

/*
** Prompt to save HTTP Basic Authorization information
*/
static int save_httpauth_prompt(void){
  Blob x;
  char c;
  if( (g.url.flags & URL_REMEMBER)==0 ) return 0;
  prompt_user("Remember Basic Authorization credentials (Y/n)? ", &x);
  c = blob_str(&x)[0];
  blob_reset(&x);
  return ( c!='n' && c!='N' );
}

/*
** Get the HTTP Basic Authorization credentials from the user
** when 401 is received.
*/
char *prompt_for_httpauth_creds(void){
  Blob x;
  char *zUser;
  char *zPw;
  char *zPrompt;
  char *zHttpAuth = 0;
  if( !fossil_isatty(fossil_fileno(stdin)) ) return 0;
  zPrompt = mprintf("\n%s authorization required by\n%s\n",
    g.url.isHttps==1 ? "Encrypted HTTPS" : "Unencrypted HTTP", g.url.canonical);
  fossil_print("%s", zPrompt);
  free(zPrompt);
  if ( g.url.user && g.url.passwd && use_fossil_creds_for_httpauth_prompt() ){
    zHttpAuth = mprintf("%s:%s", g.url.user, g.url.passwd);
  }else{
    prompt_user("Basic Authorization user: ", &x);
    zUser = mprintf("%b", &x);
    zPrompt = mprintf("HTTP password for %b: ", &x);
    blob_reset(&x);
    prompt_for_password(zPrompt, &x, 0);
    zPw = mprintf("%b", &x);
    zHttpAuth = mprintf("%s:%s", zUser, zPw);
    free(zUser);
    free(zPw);
    free(zPrompt);
    blob_reset(&x);
  }
  if( save_httpauth_prompt() ){
    set_httpauth(zHttpAuth);
  }
  return zHttpAuth;
}

/*
** Send content pSend to the the server identified by g.url using the
** external program given by g.zHttpCmd.  Capture the reply from that
** program and load it into pReply.
**
** This routine implements the --transport-command option for "fossil sync".
*/
static int http_exchange_external(
  Blob *pSend,                /* Message to be sent */
  Blob *pReply,               /* Write the reply here */
  int mHttpFlags,             /* Flags.  See above */
  const char *zAltMimetype    /* Alternative mimetype if not NULL */
){
  char *zUplink;
  char *zDownlink;
  char *zCmd;
  char *zFullUrl;
  int rc;

  zUplink = fossil_temp_filename();
  zDownlink = fossil_temp_filename();
  zFullUrl = url_full(&g.url);
  zCmd = mprintf("%s %$ %$ %$", g.zHttpCmd, zFullUrl,zUplink,zDownlink);
  fossil_free(zFullUrl);
  blob_write_to_file(pSend, zUplink);
  if( g.fHttpTrace ){
    fossil_print("RUN %s\n", zCmd);
  }
  rc = fossil_system(zCmd);
  if( rc ){
    fossil_warning("Transport command failed: %s\n", zCmd);
  }
  fossil_free(zCmd);
  file_delete(zUplink);
  if( file_size(zDownlink, ExtFILE)<0 ){
    blob_zero(pReply);
  }else{
    blob_read_from_file(pReply, zDownlink, ExtFILE);
    file_delete(zDownlink);
  }
  return rc;
}

/* If iTruth<0 then guess as to whether or not a PATH= argument is required
** when using ssh to run fossil on a remote machine name zHostname.  Return
** true if a PATH= should be provided and 0 if not.
**
** If iTruth is 1 or 0 then that means that the PATH= is or is not required,
** respectively.  Record this fact for future reference.
**
** If iTruth is 99 or more, then toggle the value that will be returned
** for future iTruth==(-1) queries.
*/
int ssh_needs_path_argument(const char *zHostname, int iTruth){
  int ans = 0;  /* Default to "no" */
  char *z = mprintf("use-path-for-ssh:%s", zHostname);
  if( iTruth<0 ){
    if( db_get_boolean(z/*works-like:"x"*/, 0) ) ans = 1;
  }else{
    if( iTruth>=99 ){
      iTruth = !db_get_boolean(z/*works-like:"x"*/, 0);
    }
    if( iTruth ){
      ans = 1;
      db_set(z/*works-like:"x"*/, "1", 1);
    }else{
      db_unset(z/*works-like:"x"*/, 1);
    }
  }
  fossil_free(z);
  return ans;
}

/*
** COMMAND: test-ssh-needs-path
**
** Usage: fossil test-ssh-needs-path ?HOSTNAME? ?BOOLEAN?
**
** With one argument, show whether or not the PATH= argument is included
** by default for HOSTNAME.  If the second argument is a boolean, then
** change the value.
**
** With no arguments, show all hosts for which ssh-needs-path is true.
*/
void test_ssh_needs_path(void){
  db_find_and_open_repository(OPEN_OK_NOT_FOUND|OPEN_SUBSTITUTE,0);
  db_open_config(0,0);
  if( g.argc>=3 ){
    const char *zHost = g.argv[2];
    int a = -1;
    int rc;
    if( g.argc>=4 ) a = is_truth(g.argv[3]);
    rc = ssh_needs_path_argument(zHost, a);
    fossil_print("%-20s %s\n", zHost, rc ? "yes" : "no");
  }else{
    Stmt s;
    db_swap_connections();
    db_prepare(&s, "SELECT substr(name,18) FROM global_config"
                   " WHERE name GLOB 'use-path-for-ssh:*'");
    while( db_step(&s)==SQLITE_ROW ){
      const char *zHost = db_column_text(&s,0);
      fossil_print("%-20s yes\n", zHost);
    }
    db_finalize(&s);
    db_swap_connections();
  }
}

/* Add an approprate PATH= argument to the SSH command under construction
** in pCmd.
**
** About This Feature
** ==================
**
** On some ssh servers (Macs in particular are guilty of this) the PATH
** variable in the shell that runs the command that is sent to the remote
** host contains a limited number of read-only system directories:
**
**      /usr/bin:/bin:/usr/sbin:/sbin
**
** The fossil executable cannot be installed into any of those directories
** because they are locked down, and so the "fossil" command cannot run.
**
** To work around this, the fossil command is prefixed with the PATH=
** argument, inserted by this function, to augment the PATH with additional
** directories in which the fossil executable is often found.
**
** But other ssh servers are confused by this initial PATH= argument.
** Some ssh servers have a list of programs that they are allowed to run
** and will fail if the first argument is not on that list, and PATH=....
** is not on that list.
**
** So that various commands that use ssh can run seamlessly on a variety
** of systems (commands that use ssh include "fossil sync" with an ssh:
** URL and the "fossil patch pull" and "fossil patch push" commands where
** the destination directory starts with HOSTNAME: or USER@HOSTNAME:.)
** the following algorithm is used:
**
**   *  First try running the fossil without any PATH= argument.  If that
**      works (and it does on a majority of systems) then we are done.
**
**   *  If the first attempt fails, then try again after adding the
**      PATH= prefix argument.  (This function is what adds that
**      argument.)  If the retry works, then remember that fact using
**      the use-path-for-ssh:HOSTNAME setting so that the first step
**      is skipped on subsequent uses of the same command.
**
** See the forum thread at
** https://fossil-scm.org/forum/forumpost/4903cb4b691af7ce for more
** background.
**
** See also:
**
**   *  The ssh_needs_path_argument() function above.
**   *  The test-ssh-needs-path command that shows the settings
**      that cache whether or not a PATH= is needed for a particular
**      HOSTNAME.
*/
void ssh_add_path_argument(Blob *pCmd){
  blob_append_escaped_arg(pCmd, 
     "PATH=$HOME/bin:/usr/local/bin:/opt/homebrew/bin:$PATH", 1);
}

/*
** Return the complete text of the last HTTP reply as saved in the
** http-reply-N.txt file.  This only works if run using --httptrace.
** Without the --httptrace option, this routine returns a NULL pointer.
** It still might return a NULL pointer if for some reason it cannot
** find and open the last http-reply-N.txt file.
*/
char *http_last_trace_reply(void){
  Blob x;
  int n;
  char *zFilename;
  if( g.fHttpTrace==0 ) return 0;
  zFilename = mprintf("http-reply-%d.txt", traceCnt);
  n = blob_read_from_file(&x, zFilename, ExtFILE);
  fossil_free(zFilename);
  if( n<=0 ) return 0;
  return blob_str(&x);
}

/*
** Sign the content in pSend, compress it, and send it to the server
** via HTTP or HTTPS.  Get a reply, uncompress the reply, and store the reply
** in pRecv.  pRecv is assumed to be uninitialized when
** this routine is called - this routine will initialize it.
**
** The server address is contain in the "g" global structure.  The
** url_parse() routine should have been called prior to this routine
** in order to fill this structure appropriately.
*/
int http_exchange(
  Blob *pSend,                /* Message to be sent */
  Blob *pReply,               /* Write the reply here */
  int mHttpFlags,             /* Flags.  See above */
  int maxRedirect,            /* Max number of redirects */
  const char *zAltMimetype    /* Alternative mimetype if not NULL */
){
  Blob login;           /* The login card */
  Blob payload;         /* The complete payload including login card */
  Blob hdr;             /* The HTTP request header */
  int closeConnection;  /* True to close the connection when done */
  int iLength;          /* Expected length of the reply payload */
  int rc = 0;           /* Result code */
  int iHttpVersion;     /* Which version of HTTP protocol server uses */
  char *zLine;          /* A single line of the reply header */
  int i;                /* Loop counter */
  int isError = 0;      /* True if the reply is an error message */
  int isCompressed = 1; /* True if the reply is compressed */

  if( g.zHttpCmd!=0 ){
    /* Handle the --transport-command option for "fossil sync" and similar */
    return http_exchange_external(pSend,pReply,mHttpFlags,zAltMimetype);
  }

  /* Activate the PATH= auxiliary argument to the ssh command if that
  ** is called for.
  */
  if( g.url.isSsh
   && (g.url.flags & URL_SSH_RETRY)==0
   && ssh_needs_path_argument(g.url.hostname, -1)
  ){
    g.url.flags |= URL_SSH_PATH;
  }

  if( transport_open(&g.url) ){
    fossil_warning("%s", transport_errmsg(&g.url));
    return 1;
  }

  /* Construct the login card and prepare the complete payload */
  if( blob_size(pSend)==0 ){
    blob_zero(&payload);
  }else{
    blob_zero(&login);
    if( mHttpFlags & HTTP_USE_LOGIN ) http_build_login_card(pSend, &login);
    if( g.fHttpTrace || (mHttpFlags & HTTP_NOCOMPRESS)!=0 ){
      payload = login;
      blob_append(&payload, blob_buffer(pSend), blob_size(pSend));
    }else{
      blob_compress2(&login, pSend, &payload);
      blob_reset(&login);
    }
  }

  /* Construct the HTTP request header */
  http_build_header(&payload, &hdr, zAltMimetype);

  /* When tracing, write the transmitted HTTP message both to standard
  ** output and into a file.  The file can then be used to drive the
  ** server-side like this:
  **
  **      ./fossil test-http <http-request-1.txt
  */
  if( g.fHttpTrace ){
    char *zOutFile;
    FILE *out;
    traceCnt++;
    zOutFile = mprintf("http-request-%d.txt", traceCnt);
    out = fopen(zOutFile, "wb");
    if( out ){
      fwrite(blob_buffer(&hdr), 1, blob_size(&hdr), out);
      fwrite(blob_buffer(&payload), 1, blob_size(&payload), out);
      fclose(out);
    }
    free(zOutFile);
    zOutFile = mprintf("http-reply-%d.txt", traceCnt);
    out = fopen(zOutFile, "wb");
    transport_log(out);
    free(zOutFile);
  }

  /*
  ** Send the request to the server.
  */
  if( mHttpFlags & HTTP_VERBOSE ){
    fossil_print("URL: %s\n", g.url.canonical);
    fossil_print("Sending %d byte header and %d byte payload\n",
                  blob_size(&hdr), blob_size(&payload));
  }
  transport_send(&g.url, &hdr);
  transport_send(&g.url, &payload);
  blob_reset(&hdr);
  blob_reset(&payload);
  transport_flip(&g.url);
  if( mHttpFlags & HTTP_VERBOSE ){
    fossil_print("IP-Address: %s\n", g.zIpAddr);
  }

  /*
  ** Read and interpret the server reply
  */
  closeConnection = 1;
  iLength = -1;
  iHttpVersion = -1;
  while( (zLine = transport_receive_line(&g.url))!=0 && zLine[0]!=0 ){
    if( mHttpFlags & HTTP_VERBOSE ){
      fossil_print("Read: [%s]\n", zLine);
    }
    if( fossil_strnicmp(zLine, "http/1.", 7)==0 ){
      if( sscanf(zLine, "HTTP/1.%d %d", &iHttpVersion, &rc)!=2 ) goto write_err;
      if( rc==401 ){
        if( fSeenHttpAuth++ < MAX_HTTP_AUTH ){
          if( g.zHttpAuth ){
            if( g.zHttpAuth ) free(g.zHttpAuth);
          }
          g.zHttpAuth = prompt_for_httpauth_creds();
          transport_close(&g.url);
          return http_exchange(pSend, pReply, mHttpFlags,
                               maxRedirect, zAltMimetype);
        }
      }
      if( rc!=200 && rc!=301 && rc!=302 && rc!=307 && rc!=308 ){
        int ii;
        for(ii=7; zLine[ii] && zLine[ii]!=' '; ii++){}
        while( zLine[ii]==' ' ) ii++;
        fossil_warning("server says: %s", &zLine[ii]);
        goto write_err;
      }
      if( iHttpVersion==0 ){
        closeConnection = 1;
      }else{
        closeConnection = 0;
      }
    }else if( g.url.isSsh && fossil_strnicmp(zLine, "status:", 7)==0 ){
      if( sscanf(zLine, "Status: %d", &rc)!=1 ) goto write_err;
      if( rc!=200 && rc!=301 && rc!=302 && rc!=307 && rc!=308 ){
        int ii;
        for(ii=7; zLine[ii] && zLine[ii]!=' '; ii++){}
        while( zLine[ii]==' ' ) ii++;
        fossil_warning("server says: %s", &zLine[ii]);
        goto write_err;
      }
      if( iHttpVersion<0 ) iHttpVersion = 1;
      closeConnection = 0;
    }else if( fossil_strnicmp(zLine, "content-length:", 15)==0 ){
      for(i=15; fossil_isspace(zLine[i]); i++){}
      iLength = atoi(&zLine[i]);
    }else if( fossil_strnicmp(zLine, "connection:", 11)==0 ){
      if( sqlite3_strlike("%close%", &zLine[11], 0)==0 ){
        closeConnection = 1;
      }else if( sqlite3_strlike("%keep-alive%", &zLine[11], 0)==0 ){
        closeConnection = 0;
      }
    }else if( ( rc==301 || rc==302 || rc==307 || rc==308 ) &&
                fossil_strnicmp(zLine, "location:", 9)==0 ){
      int i, j;
      int wasHttps;
      int priorUrlFlags;

      if ( --maxRedirect == 0){
        fossil_warning("redirect limit exceeded");
        goto write_err;
      }
      for(i=9; zLine[i] && zLine[i]==' '; i++){}
      if( zLine[i]==0 ){
        fossil_warning("malformed redirect: %s", zLine);
        goto write_err;
      }
      j = strlen(zLine) - 1;
      while( j>4 && fossil_strcmp(&zLine[j-4],"/xfer")==0 ){
         j -= 4;
         zLine[j] = 0;
      }
      if( (mHttpFlags & HTTP_QUIET)==0 ){
        fossil_print("redirect with status %d to %s\n", rc, &zLine[i]);
      }
      if( g.url.isFile || g.url.isSsh ){
        fossil_warning("cannot redirect from %s to %s", g.url.canonical,
                       &zLine[i]);
        goto write_err;
      }
      wasHttps = g.url.isHttps;
      priorUrlFlags = g.url.flags;
      url_parse(&zLine[i], 0);
      if( wasHttps && !g.url.isHttps ){
        fossil_warning("cannot redirect from HTTPS to HTTP");
        goto write_err;
      }
      if( g.url.isSsh || g.url.isFile ){
        fossil_warning("cannot redirect to %s", &zLine[i]);
        goto write_err;
      }
      transport_close(&g.url);
      transport_global_shutdown(&g.url);
      fSeenHttpAuth = 0;
      if( g.zHttpAuth ) free(g.zHttpAuth);
      g.zHttpAuth = get_httpauth();
      if( (rc==301 || rc==308) && (priorUrlFlags & URL_REMEMBER)!=0 ){
        g.url.flags |= URL_REMEMBER;
        url_remember();
      }
      return http_exchange(pSend, pReply, mHttpFlags,
                           maxRedirect, zAltMimetype);
    }else if( fossil_strnicmp(zLine, "content-type: ", 14)==0 ){
      if( fossil_strnicmp(&zLine[14], "application/x-fossil-debug", -1)==0 ){
        isCompressed = 0;
      }else if( fossil_strnicmp(&zLine[14],
                          "application/x-fossil-uncompressed", -1)==0 ){
        isCompressed = 0;
      }else{
        if( mHttpFlags & HTTP_GENERIC ){
          if( mHttpFlags & HTTP_NOCOMPRESS ) isCompressed = 0;
        }else if( fossil_strnicmp(&zLine[14], "application/x-fossil", -1)!=0 ){
          isError = 1;
        }
      }
    }
  }
  if( iHttpVersion<0 ){
    /* We got nothing back from the server.  If using the ssh: protocol,
    ** this might mean we need to add or remove the PATH=... argument
    ** to the SSH command being sent.  If that is the case, retry the
    ** request after adding or removing the PATH= argument.
    */
    if( g.url.isSsh                         /* This is an SSH: sync */
     && (g.url.flags & URL_SSH_EXE)==0      /* Does not have ?fossil=.... */
     && (g.url.flags & URL_SSH_RETRY)==0    /* Not retried already */
    ){
      /* Retry after flipping the SSH_PATH setting */
      transport_close(&g.url);
      fossil_print(
        "First attempt to run fossil on %s using SSH failed.\n"
        "Retrying %s the PATH= argument.\n",
        g.url.hostname,
        (g.url.flags & URL_SSH_PATH)!=0 ? "without" : "with"
      );
      g.url.flags ^= URL_SSH_PATH|URL_SSH_RETRY;
      rc = http_exchange(pSend,pReply,mHttpFlags,0,zAltMimetype);
      if( rc==0 ){
        (void)ssh_needs_path_argument(g.url.hostname,
                                (g.url.flags & URL_SSH_PATH)!=0);
      }
      return rc;
    }else{
      /* The problem could not be corrected by retrying.  Report the
      ** the error. */
      if( g.url.isSsh && !g.fSshTrace ){
        fossil_warning("server did not reply: "
                       " rerun with --sshtrace for diagnostics");
      }else{
        fossil_warning("server did not reply");
      }
      goto write_err;
    }
  }
  if( rc!=200 ){
    fossil_warning("\"location:\" missing from %d redirect reply", rc);
    goto write_err;
  }

  /*
  ** Extract the reply payload that follows the header
  */
  blob_zero(pReply);
  if( iLength==0 ){
    /* No content to read */
  }else if( iLength>0 ){
    /* Read content of a known length */
    int iRecvLen;         /* Received length of the reply payload */
    blob_resize(pReply, iLength);
    iRecvLen = transport_receive(&g.url, blob_buffer(pReply), iLength);
    if( mHttpFlags & HTTP_VERBOSE ){
      fossil_print("Reply received: %d of %d bytes\n", iRecvLen, iLength);
    }
    if( iRecvLen != iLength ){
      fossil_warning("response truncated: got %d bytes of %d",
                     iRecvLen, iLength);
      goto write_err;
    }
  }else if( closeConnection ){
    /* Read content until end-of-file */
    int iRecvLen;         /* Received length of the reply payload */
    unsigned int nReq = 1000;
    unsigned int nPrior = 0;
    do{
      nReq *= 2;
      blob_resize(pReply, nPrior+nReq);
      iRecvLen = transport_receive(&g.url, &pReply->aData[nPrior], (int)nReq);
      nPrior += iRecvLen;
      pReply->nUsed = nPrior;
    }while( iRecvLen==nReq && nReq<0x20000000 );
    if( mHttpFlags & HTTP_VERBOSE ){
      fossil_print("Reply received: %u bytes (w/o content-length)\n", nPrior);
    }
  }else{
    assert( iLength<0 && !closeConnection );
    fossil_warning("\"content-length\" missing from %d keep-alive reply", rc);
  }
  if( isError ){
    char *z;
    int i, j;
    z = blob_str(pReply);
    for(i=j=0; z[i]; i++, j++){
      if( z[i]=='<' ){
        while( z[i] && z[i]!='>' ) i++;
        if( z[i]==0 ) break;
      }
      z[j] = z[i];
    }
    z[j] = 0;
    fossil_warning("server sends error: %s", z);
    goto write_err;
  }
  if( isCompressed ) blob_uncompress(pReply, pReply);

  /*
  ** Close the connection to the server if appropriate.
  **
  ** FIXME:  There is some bug in the lower layers that prevents the
  ** connection from remaining open.  The easiest fix for now is to
  ** simply close and restart the connection for each round-trip.
  **
  ** For SSH we will leave the connection open.
  */
  if( ! g.url.isSsh ) closeConnection = 1; /* FIX ME */
  if( closeConnection ){
    transport_close(&g.url);
  }else{
    transport_rewind(&g.url);
  }
  return 0;

  /*
  ** Jump to here if an error is seen.
  */
write_err:
  transport_close(&g.url);
  return 1;
}

/*
** COMMAND: test-httpmsg
**
** Usage: %fossil test-httpmsg ?OPTIONS? URL ?PAYLOAD? ?OUTPUT?
**
** Send an HTTP message to URL and get the reply. PAYLOAD is a file containing
** the payload, or "-" to read payload from standard input.  a POST message
** is sent if PAYLOAD is specified and is non-empty.  If PAYLOAD is omitted
** or is an empty file, then a GET message is sent.
**
** If a second filename (OUTPUT) is given after PAYLOAD, then the reply
** is written into that second file instead of being written on standard
** output.  Use the "--out OUTPUT" option to specify an output file for
** a GET request where there is no PAYLOAD.
**
** Options:
**     --compress                 Use ZLIB compression on the payload
**     --mimetype TYPE            Mimetype of the payload
**     --no-cert-verify           Disable TLS cert verification
**     --out FILE                 Store the reply in FILE
**     -v                         Verbose output
**     --xfer                     PAYLOAD in a Fossil xfer protocol message
*/
void test_httpmsg_command(void){
  const char *zMimetype;
  const char *zInFile;
  const char *zOutFile;
  Blob in, out;
  unsigned int mHttpFlags = HTTP_GENERIC|HTTP_NOCOMPRESS;

  zMimetype = find_option("mimetype",0,1);
  zOutFile = find_option("out","o",1);
  if( find_option("verbose","v",0)!=0 ) mHttpFlags |= HTTP_VERBOSE;
  if( find_option("compress",0,0)!=0 ) mHttpFlags &= ~HTTP_NOCOMPRESS;
  if( find_option("no-cert-verify",0,0)!=0 ){
    #ifdef FOSSIL_ENABLE_SSL
    ssl_disable_cert_verification();
    #endif
  }
  if( find_option("xfer",0,0)!=0 ){
    mHttpFlags |= HTTP_USE_LOGIN;
    mHttpFlags &= ~HTTP_GENERIC;
  }
  if( find_option("ipv4",0,0) ) g.fIPv4 = 1;
  verify_all_options();
  if( g.argc<3 || g.argc>5 ){
    usage("URL ?PAYLOAD? ?OUTPUT?");
  }
  zInFile = g.argc>=4 ? g.argv[3] : 0;
  if( g.argc==5 ){
    if( zOutFile ){
      fossil_fatal("output file specified twice: \"--out %s\" and \"%s\"",
        zOutFile, g.argv[4]);
    }
    zOutFile = g.argv[4];
  }
  url_parse(g.argv[2], 0);
  if( g.url.protocol[0]!='h' ){
    fossil_fatal("the %s command supports only http: and https:", g.argv[1]);
  }
  if( zInFile ){
    blob_read_from_file(&in, zInFile, ExtFILE);
    if( zMimetype==0 && (mHttpFlags & HTTP_GENERIC)!=0 ){
      if( fossil_strcmp(zInFile,"-")==0 ){
        zMimetype = "application/x-unknown";
      }else{
        zMimetype = mimetype_from_name(zInFile);
      }
    }
  }else{
    blob_init(&in, 0, 0);
  }
  blob_init(&out, 0, 0);
  if( (mHttpFlags & HTTP_VERBOSE)==0 && zOutFile==0 ){
    zOutFile = "-";
    mHttpFlags |= HTTP_QUIET;
  }
  http_exchange(&in, &out, mHttpFlags, 4, zMimetype);
  if( zOutFile ) blob_write_to_file(&out, zOutFile);
  blob_zero(&in);
  blob_zero(&out);
}
