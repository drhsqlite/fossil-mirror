/*
** Copyright (c) 2009 D. Richard Hipp
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
** Implementation of the Setup page for "skins".
*/
#include "config.h"
#include <assert.h>
#include "skins.h"

/*
** An array of available built-in skins.
**
** To add new built-in skins:
**
**    1.  Pick a name for the new skin.  (Here we use "xyzzy").
**
**    2.  Install files skins/xyzzy/css.txt, skins/xyzzy/header.txt,
**        and skins/xyzzy/footer.txt into the source tree.
**
**    3.  Rerun "tclsh makemake.tcl" in the src/ folder in order to
**        rebuild the makefiles to reference the new CSS, headers, and footers.
**
**    4.  Make an entry in the following array for the new skin.
*/
static struct BuiltinSkin {
  const char *zDesc;    /* Description of this skin */
  const char *zLabel;   /* The directory under skins/ holding this skin */
  char *zSQL;           /* Filled in at run-time with SQL to insert this skin */
} aBuiltinSkin[] = {
  { "Default",                           "default",           0 },
  { "Blitz",                             "blitz",             0 },
  { "Blitz, No Logo",                    "blitz_no_logo",     0 },
  { "Bootstrap",                         "bootstrap",         0 },
  { "Xekri",                             "xekri",             0 },
  { "Original",                          "original",          0 },
  { "Enhanced Original",                 "enhanced1",         0 },
  { "Shadow boxes & Rounded Corners",    "rounded1",          0 },
  { "Eagle",                             "eagle",             0 },
  { "Black & White, Menu on Left",       "black_and_white",   0 },
  { "Plain Gray, No Logo",               "plain_gray",        0 },
  { "Khaki, No Logo",                    "khaki",             0 },
};

/*
** Alternative skins can be specified in the CGI script or by options
** on the "http", "ui", and "server" commands.  The alternative skin
** name must be one of the aBuiltinSkin[].zLabel names.  If there is
** a match, that alternative is used.
**
** The following static variable holds the name of the alternative skin,
** or NULL if the skin should be as configured.
*/
static struct BuiltinSkin *pAltSkin = 0;
static char *zAltSkinDir = 0;

/*
** Skin details are a set of key/value pairs that define display
** attributes of the skin that cannot be easily specified using CSS
** or that need to be known on the server-side.
**
** The following array holds the value for all known skin details.
*/
static struct SkinDetail {
  const char *zName;      /* Name of the detail */
  const char *zValue;     /* Value of the detail */
} aSkinDetail[] = {
  { "timeline-arrowheads",        "1"  },
  { "timeline-circle-nodes",      "0"  },
  { "timeline-color-graph-lines", "0"  },
  { "white-foreground",           "0"  },
};

/*
** Invoke this routine to set the alternative skin.  Return NULL if the
** alternative was successfully installed.  Return a string listing all
** available skins if zName does not match an available skin.  Memory
** for the returned string comes from fossil_malloc() and should be freed
** by the caller.
**
** If the alternative skin name contains one or more '/' characters, then
** it is assumed to be a directory on disk that holds override css.txt,
** footer.txt, and header.txt.  This mode can be used for interactive
** development of new skins.
*/
char *skin_use_alternative(const char *zName){
  int i;
  Blob err = BLOB_INITIALIZER;
  if( strchr(zName, '/')!=0 ){
    zAltSkinDir = fossil_strdup(zName);
    return 0;
  }
  for(i=0; i<count(aBuiltinSkin); i++){
    if( fossil_strcmp(aBuiltinSkin[i].zLabel, zName)==0 ){
      pAltSkin = &aBuiltinSkin[i];
      return 0;
    }
  }
  blob_appendf(&err, "available skins: %s", aBuiltinSkin[0].zLabel);
  for(i=1; i<count(aBuiltinSkin); i++){
    blob_append(&err, " ", 1);
    blob_append(&err, aBuiltinSkin[i].zLabel, -1);
  }
  return blob_str(&err);
}

/*
** Look for the --skin command-line option and process it.  Or
** call fossil_fatal() if an unknown skin is specified.
*/
void skin_override(void){
  const char *zSkin = find_option("skin",0,1);
  if( zSkin ){
    char *zErr = skin_use_alternative(zSkin);
    if( zErr ) fossil_fatal("%s", zErr);
  }
}

/*
** The following routines return the various components of the skin
** that should be used for the current run.
*/
const char *skin_get(const char *zWhat){
  const char *zOut;
  char *z;
  if( zAltSkinDir ){
    char *z = mprintf("%s/%s.txt", zAltSkinDir, zWhat);
    if( file_isfile(z) ){
      Blob x;
      blob_read_from_file(&x, z);
      fossil_free(z);
      return blob_str(&x);
    }
    fossil_free(z);
  }
  if( pAltSkin ){
    z = mprintf("skins/%s/%s.txt", pAltSkin->zLabel, zWhat);
    zOut = builtin_text(z);
    fossil_free(z);
  }else{
    zOut = db_get(zWhat, 0);
    if( zOut==0 ){
      z = mprintf("skins/default/%s.txt", zWhat);
      zOut = builtin_text(z);
      fossil_free(z);
    }
  }
  return zOut;
}

/*
** Return the command-line option used to set the skin, or return NULL
** if the default skin is being used.
*/
const char *skin_in_use(void){
  if( zAltSkinDir ) return zAltSkinDir;
  if( pAltSkin ) return pAltSkin->zLabel;
  return 0;
}

/*
** Return a pointer to a SkinDetail element.  Return 0 if not found.
*/
static struct SkinDetail *skin_detail_find(const char *zName){
  int lwr = 0;
  int upr = count(aSkinDetail);
  while( upr>=lwr ){
    int mid = (upr+lwr)/2;
    int c = fossil_strcmp(aSkinDetail[mid].zName, zName);
    if( c==0 ) return &aSkinDetail[mid];
    if( c<0 ){
      lwr = mid+1;
    }else{
      upr = mid-1;
    }
  }
  return 0;
}

/* Initialize the aSkinDetail array using the text in the details.txt
** file.
*/
static void skin_detail_initialize(void){
  static int isInit = 0;
  char *zDetail;
  Blob detail, line, key, value;
  if( isInit ) return;
  isInit = 1;
  zDetail = (char*)skin_get("details");
  if( zDetail==0 ) return;
  zDetail = fossil_strdup(zDetail);
  blob_init(&detail, zDetail, -1);
  while( blob_line(&detail, &line) ){
    char *zKey;
    int nKey;
    struct SkinDetail *pDetail;
    if( !blob_token(&line, &key) ) continue;
    zKey = blob_buffer(&key);
    if( zKey[0]=='#' ) continue;
    nKey = blob_size(&key);
    if( nKey<2 ) continue;
    if( zKey[nKey-1]!=':' ) continue;
    zKey[nKey-1] = 0;
    pDetail = skin_detail_find(zKey);
    if( pDetail==0 ) continue;
    if( !blob_token(&line, &value) ) continue;
    pDetail->zValue = fossil_strdup(blob_str(&value));
  }
  blob_reset(&detail);
  fossil_free(zDetail);
}

/*
** Return a skin detail setting
*/
const char *skin_detail(const char *zName){
  struct SkinDetail *pDetail;
  skin_detail_initialize();
  pDetail = skin_detail_find(zName);
  if( pDetail==0 ) fossil_fatal("no such skin detail: %s", zName);
  return pDetail->zValue;
}
int skin_detail_boolean(const char *zName){
  return !is_false(skin_detail(zName));
}

/*
** Hash function for computing a skin id.
*/
static unsigned int skin_hash(unsigned int h, const char *z){
  if( z==0 ) return h;
  while( z[0] ){
    h = (h<<11) ^ (h<<1) ^ (h>>3) ^ z[0];
    z++;
  }
  return h;
}

/*
** Return an identifier that is (probably) different for every skin
** but that is (probably) the same if the skin is unchanged.  This
** identifier can be attached to resource URLs to force reloading when
** the resources change but allow the resources to be read from cache
** as long as they are unchanged.
*/
unsigned int skin_id(const char *zResource){
  unsigned int h = 0;
  if( zAltSkinDir ){
    h = skin_hash(0, zAltSkinDir);
  }else if( pAltSkin ){
    h = skin_hash(0, pAltSkin->zLabel);
  }else{
    char *zMTime = db_get_mtime(zResource, 0, 0);
    h = skin_hash(0, zMTime);
    fossil_free(zMTime);
  }
  h = skin_hash(h, MANIFEST_UUID);
  return h;
}

/*
** For a skin named zSkinName, compute the name of the CONFIG table
** entry where that skin is stored and return it.
**
** Return NULL if zSkinName is NULL or an empty string.
**
** If ifExists is true, and the named skin does not exist, return NULL.
*/
static char *skinVarName(const char *zSkinName, int ifExists){
  char *z;
  if( zSkinName==0 || zSkinName[0]==0 ) return 0;
  z = mprintf("skin:%s", zSkinName);
  if( ifExists && !db_exists("SELECT 1 FROM config WHERE name=%Q", z) ){
    free(z);
    z = 0;
  }
  return z;
}

/*
** Return true if there exists a skin name "zSkinName".
*/
static int skinExists(const char *zSkinName){
  int i;
  if( zSkinName==0 ) return 0;
  for(i=0; i<count(aBuiltinSkin); i++){
    if( fossil_strcmp(zSkinName, aBuiltinSkin[i].zDesc)==0 ) return 1;
  }
  return db_exists("SELECT 1 FROM config WHERE name='skin:%q'", zSkinName);
}

/*
** Construct and return an string of SQL statements that represents
** a "skin" setting.  If zName==0 then return the skin currently
** installed.  Otherwise, return one of the built-in skins designated
** by zName.
**
** Memory to hold the returned string is obtained from malloc.
*/
static char *getSkin(const char *zName){
  const char *z;
  char *zLabel;
  static const char *azType[] = { "css", "header", "footer", "details" };
  int i;
  Blob val;
  blob_zero(&val);
  for(i=0; i<count(azType); i++){
    if( zName ){
      zLabel = mprintf("skins/%s/%s.txt", zName, azType[i]);
      z = builtin_text(zLabel);
      fossil_free(zLabel);
    }else{
      z = db_get(azType[i], 0);
      if( z==0 ){
        zLabel = mprintf("skins/default/%s.txt", azType[i]);
        z = builtin_text(zLabel);
        fossil_free(zLabel);
      }
    }
    blob_appendf(&val,
       "REPLACE INTO config(name,value,mtime) VALUES(%Q,%Q,now());\n",
       azType[i], z
    );
  }
  return blob_str(&val);
}

/*
** Respond to a Rename button press.  Return TRUE if a dialog was painted.
** Return FALSE to continue with the main Skins page.
*/
static int skinRename(void){
  const char *zOldName;
  const char *zNewName;
  int ex = 0;
  if( P("rename")==0 ) return 0;
  zOldName = P("sn");
  zNewName = P("newname");
  if( zOldName==0 ) return 0;
  if( zNewName==0 || zNewName[0]==0 || (ex = skinExists(zNewName))!=0 ){
    if( zNewName==0 ) zNewName = zOldName;
    style_header("Rename A Skin");
    if( ex ){
      @ <p><span class="generalError">There is already another skin
      @ named "%h(zNewName)".  Choose a different name.</span></p>
    }
    @ <form action="%s(g.zTop)/setup_skin" method="post"><div>
    @ <table border="0"><tr>
    @ <tr><td align="right">Current name:<td align="left"><b>%h(zOldName)</b>
    @ <tr><td align="right">New name:<td align="left">
    @ <input type="text" size="35" name="newname" value="%h(zNewName)">
    @ <tr><td><td>
    @ <input type="hidden" name="sn" value="%h(zOldName)">
    @ <input type="submit" name="rename" value="Rename">
    @ <input type="submit" name="canren" value="Cancel">
    @ </table>
    login_insert_csrf_secret();
    @ </div></form>
    style_footer();
    return 1;
  }
  db_multi_exec(
    "UPDATE config SET name='skin:%q' WHERE name='skin:%q';",
    zNewName, zOldName
  );
  return 0;
}

/*
** Respond to a Save button press.  Return TRUE if a dialog was painted.
** Return FALSE to continue with the main Skins page.
*/
static int skinSave(const char *zCurrent){
  const char *zNewName;
  int ex = 0;
  if( P("save")==0 ) return 0;
  zNewName = P("svname");
  if( zNewName && zNewName[0]!=0 ){
  }
  if( zNewName==0 || zNewName[0]==0 || (ex = skinExists(zNewName))!=0 ){
    if( zNewName==0 ) zNewName = "";
    style_header("Save Current Skin");
    if( ex ){
      @ <p><span class="generalError">There is already another skin
      @ named "%h(zNewName)".  Choose a different name.</span></p>
    }
    @ <form action="%s(g.zTop)/setup_skin" method="post"><div>
    @ <table border="0"><tr>
    @ <tr><td align="right">Name for this skin:<td align="left">
    @ <input type="text" size="35" name="svname" value="%h(zNewName)">
    @ <tr><td><td>
    @ <input type="submit" name="save" value="Save">
    @ <input type="submit" name="cansave" value="Cancel">
    @ </table>
    login_insert_csrf_secret();
    @ </div></form>
    style_footer();
    return 1;
  }
  db_multi_exec(
    "INSERT OR IGNORE INTO config(name, value, mtime)"
    "VALUES('skin:%q',%Q,now())",
    zNewName, zCurrent
  );
  return 0;
}

/*
** WEBPAGE: setup_skin
**
** Show a list of available skins with buttons for selecting which
** skin to use.  Requires Admin privilege.
*/
void setup_skin(void){
  const char *z;
  char *zName;
  char *zErr = 0;
  const char *zCurrent = 0;  /* Current skin */
  int i;                     /* Loop counter */
  Stmt q;
  int seenCurrent = 0;

  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }
  db_begin_transaction();
  zCurrent = getSkin(0);
  for(i=0; i<count(aBuiltinSkin); i++){
    aBuiltinSkin[i].zSQL = getSkin(aBuiltinSkin[i].zLabel);
  }

  /* Process requests to delete a user-defined skin */
  if( P("del1") && (zName = skinVarName(P("sn"), 1))!=0 ){
    style_header("Confirm Custom Skin Delete");
    @ <form action="%s(g.zTop)/setup_skin" method="post"><div>
    @ <p>Deletion of a custom skin is a permanent action that cannot
    @ be undone.  Please confirm that this is what you want to do:</p>
    @ <input type="hidden" name="sn" value="%h(P("sn"))" />
    @ <input type="submit" name="del2" value="Confirm - Delete The Skin" />
    @ <input type="submit" name="cancel" value="Cancel - Do Not Delete" />
    login_insert_csrf_secret();
    @ </div></form>
    style_footer();
    return;
  }
  if( P("del2")!=0 && (zName = skinVarName(P("sn"), 1))!=0 ){
    db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
  }
  if( skinRename() ) return;
  if( skinSave(zCurrent) ) return;

  /* The user pressed one of the "Install" buttons. */
  if( P("load") && (z = P("sn"))!=0 && z[0] ){
    int seen = 0;

    /* Check to see if the current skin is already saved.  If it is, there
    ** is no need to create a backup */
    zCurrent = getSkin(0);
    for(i=0; i<count(aBuiltinSkin); i++){
      if( fossil_strcmp(aBuiltinSkin[i].zSQL, zCurrent)==0 ){
        seen = 1;
        break;
      }
    }
    if( !seen ){
      seen = db_exists("SELECT 1 FROM config WHERE name GLOB 'skin:*'"
                       " AND value=%Q", zCurrent);
      if( !seen ){
        db_multi_exec(
          "INSERT INTO config(name,value,mtime) VALUES("
          "  strftime('skin:Backup On %%Y-%%m-%%d %%H:%%M:%%S'),"
          "  %Q,now())", zCurrent
        );
      }
    }
    seen = 0;
    for(i=0; i<count(aBuiltinSkin); i++){
      if( fossil_strcmp(aBuiltinSkin[i].zDesc, z)==0 ){
        seen = 1;
        zCurrent = aBuiltinSkin[i].zSQL;
        db_multi_exec("%s", zCurrent/*safe-for-%s*/);
        break;
      }
    }
    if( !seen ){
      zName = skinVarName(z,0);
      zCurrent = db_get(zName, 0);
      db_multi_exec("%s", zCurrent/*safe-for-%s*/);
    }
  }

  style_header("Skins");
  if( zErr ){
    @ <p style="color:red">%h(zErr)</p>
  }
  @ <p>A "skin" is a combination of
  @ <a href="setup_skinedit?w=0">CSS</a>,
  @ <a href="setup_skinedit?w=2">Header</a>,
  @ <a href="setup_skinedit?w=1">Footer</a>, and
  @ <a href="setup_skinedit?w=3">Details</a>
  @ that determines the look and feel
  @ of the web interface.</p>
  @
  if( pAltSkin ){
    @ <p class="generalError">
    @ This page is generated using an skin override named
    @ "%h(pAltSkin->zLabel)".  You can change the skin configuration
    @ below, but the changes will not take effect until the Fossil server
    @ is restarted without the override.</p>
    @
  }
  @ <h2>Available Skins:</h2>
  @ <table border="0">
  for(i=0; i<count(aBuiltinSkin); i++){
    z = aBuiltinSkin[i].zDesc;
    @ <tr><td>%d(i+1).<td>%h(z)<td>&nbsp;&nbsp;<td>
    if( fossil_strcmp(aBuiltinSkin[i].zSQL, zCurrent)==0 ){
      @ (Currently In Use)
      seenCurrent = 1;
    }else{
      @ <form action="%s(g.zTop)/setup_skin" method="post">
      @ <input type="hidden" name="sn" value="%h(z)" />
      @ <input type="submit" name="load" value="Install" />
      if( pAltSkin==&aBuiltinSkin[i] ){
        @ (Current override)
      }
      @ </form>
    }
    @ </tr>
  }
  db_prepare(&q,
     "SELECT substr(name, 6), value FROM config"
     " WHERE name GLOB 'skin:*'"
     " ORDER BY name"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zN = db_column_text(&q, 0);
    const char *zV = db_column_text(&q, 1);
    i++;
    @ <tr><td>%d(i).<td>%h(zN)<td>&nbsp;&nbsp;<td>
    @ <form action="%s(g.zTop)/setup_skin" method="post">
    if( fossil_strcmp(zV, zCurrent)==0 ){
      @ (Currently In Use)
      seenCurrent = 1;
    }else{
      @ <input type="submit" name="load" value="Install">
      @ <input type="submit" name="del1" value="Delete">
    }
    @ <input type="submit" name="rename" value="Rename">
    @ <input type="hidden" name="sn" value="%h(zN)">
    @ </form></tr>
  }
  db_finalize(&q);
  if( !seenCurrent ){
    i++;
    @ <tr><td>%d(i).<td><i>Current Configuration</i><td>&nbsp;&nbsp;<td>
    @ <form action="%s(g.zTop)/setup_skin" method="post">
    @ <input type="submit" name="save" value="Save">
    @ </form>
  }
  @ </table>
  style_footer();
  db_end_transaction(0);
}


/*
** WEBPAGE: setup_skinedit
**
** Edit aspects of a skin determined by the w= query parameter.
** Requires Admin privileges.
**
**    w=N     -- 0=CSS, 1=footer, 2=header, 3=details
*/
void setup_skinedit(void){
  static const struct sSkinAddr {
    const char *zFile;
    const char *zTitle;
    const char *zSubmenu;
  } aSkinAttr[] = {
    /* 0 */ { "css",     "CSS",             "CSS",     },
    /* 1 */ { "footer",  "Page Footer",     "Footer",  },
    /* 2 */ { "header",  "Page Header",     "Header",  },
    /* 3 */ { "details", "Display Details", "Details", },
  };
  const char *zBasis;
  const char *zContent;
  char *zDflt;
  int ii;
  int j;

  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }
  ii = atoi(PD("w","0"));
  if( ii<0 || ii>count(aSkinAttr) ) ii = 0;
  zBasis = PD("basis","default");
  zDflt = mprintf("skins/%s/%s.txt", zBasis, aSkinAttr[ii].zFile);
  db_begin_transaction();
  if( P("revert")!=0 ){
    db_multi_exec("DELETE FROM config WHERE name=%Q", aSkinAttr[ii].zFile);
    cgi_replace_parameter(aSkinAttr[ii].zFile, builtin_text(zDflt));
  }
  style_header("%s", aSkinAttr[ii].zTitle);
  for(j=0; j<count(aSkinAttr); j++){
    if( j==ii ) continue;
    style_submenu_element(aSkinAttr[j].zSubmenu,
          "%R/setup_skinedit?w=%d&basis=%h",j,zBasis);
  }
  style_submenu_element("Skins", "%R/setup_skin");
  @ <form action="%s(g.zTop)/setup_skinedit" method="post"><div>
  login_insert_csrf_secret();
  @ <input type='hidden' name='w' value='%d(ii)'>
  @ <h2>Edit %s(aSkinAttr[ii].zTitle):</h2>
  zContent  = textarea_attribute("", 10, 80, aSkinAttr[ii].zFile,
                        aSkinAttr[ii].zFile, builtin_text(zDflt), 0);
  @ <br />
  @ <input type="submit" name="submit" value="Apply Changes" />
  @ <hr />
  @ Baseline: <select size='1' name='basis'>
  for(j=0; j<count(aBuiltinSkin); j++){
    cgi_printf("<option value='%h'%s>%h</option>\n",
       aBuiltinSkin[j].zLabel,
       fossil_strcmp(zBasis,aBuiltinSkin[j].zLabel)==0 ? " selected" : "",
       aBuiltinSkin[j].zDesc
    );
  }
  @ </select>
  @ <input type="submit" name="diff" value="Diff" />
  if( P("diff")!=0 ){
    u64 diffFlags = construct_diff_flags(0,0) |
                        DIFF_STRIP_EOLCR;
    Blob from, to, out;
    blob_init(&to, zContent, -1);
    blob_init(&from, builtin_text(zDflt), -1);
    blob_zero(&out);
    @ <input type="submit" name="revert" value="Revert" /><p>
    if( diffFlags & DIFF_SIDEBYSIDE ){
      text_diff(&from, &to, &out, 0, diffFlags | DIFF_HTML | DIFF_NOTTOOBIG);
      @ %s(blob_str(&out))
    }else{
      text_diff(&from, &to, &out, 0,
             diffFlags | DIFF_LINENO | DIFF_HTML | DIFF_NOTTOOBIG);
      @ <pre class="udiff">
      @ %s(blob_str(&out))
      @ </pre>
    }
    blob_reset(&from);
    blob_reset(&to);
    blob_reset(&out);
  }
  @ </div></form>
  style_footer();
  db_end_transaction(0);
}
