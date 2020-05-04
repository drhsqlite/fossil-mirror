/*
** Copyright (c) 2009 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
  { "Ardoise",                           "ardoise",           0 },
};

/*
** A skin consists of five "files" named here:
*/
static const char *const azSkinFile[] = {
  "css", "header", "footer", "details", "js"
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
static int iDraftSkin = 0;

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
  if( sqlite3_strglob("draft[1-9]", zName)==0 ){
    skin_use_draft(zName[5] - '0');
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
** Use one of the draft skins.
*/
void skin_use_draft(int i){
  iDraftSkin = i;
}

/*
** The following routines return the various components of the skin
** that should be used for the current run.
**
** zWhat is one of:  "css", "header", "footer", "details", "js"
*/
const char *skin_get(const char *zWhat){
  const char *zOut;
  char *z;
  if( iDraftSkin ){
    z = mprintf("draft%d-%s", iDraftSkin, zWhat);
    zOut = db_get(z, 0);
    fossil_free(z);
    if( zOut ) return zOut;
  }
  if( zAltSkinDir ){
    char *z = mprintf("%s/%s.txt", zAltSkinDir, zWhat);
    if( file_isfile(z, ExtFILE) ){
      Blob x;
      blob_read_from_file(&x, z, ExtFILE);
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
  int i;
  Blob val;
  blob_zero(&val);
  for(i=0; i<count(azSkinFile); i++){
    if( zName ){
      zLabel = mprintf("skins/%s/%s.txt", zName, azSkinFile[i]);
      z = builtin_text(zLabel);
      fossil_free(zLabel);
    }else{
      z = db_get(azSkinFile[i], 0);
      if( z==0 ){
        zLabel = mprintf("skins/default/%s.txt", azSkinFile[i]);
        z = builtin_text(zLabel);
        fossil_free(zLabel);
      }
    }
    blob_appendf(&val,
       "REPLACE INTO config(name,value,mtime) VALUES(%Q,%Q,now());\n",
       azSkinFile[i], z
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
    @ <form action="%s(g.zTop)/setup_skin_admin" method="post"><div>
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
    @ <form action="%s(g.zTop)/setup_skin_admin" method="post"><div>
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
** WEBPAGE: setup_skin_admin
**
** Administrative actions on skins.  For administrators only.
*/
void setup_skin_admin(void){
  const char *z;
  char *zName;
  char *zErr = 0;
  const char *zCurrent = 0;  /* Current skin */
  int i;                     /* Loop counter */
  Stmt q;
  int seenCurrent = 0;
  int once;

  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  db_begin_transaction();
  zCurrent = getSkin(0);
  for(i=0; i<count(aBuiltinSkin); i++){
    aBuiltinSkin[i].zSQL = getSkin(aBuiltinSkin[i].zLabel);
  }

  if( cgi_csrf_safe(1) ){
    /* Process requests to delete a user-defined skin */
    if( P("del1") && (zName = skinVarName(P("sn"), 1))!=0 ){
      style_header("Confirm Custom Skin Delete");
      @ <form action="%s(g.zTop)/setup_skin_admin" method="post"><div>
      @ <p>Deletion of a custom skin is a permanent action that cannot
      @ be undone.  Please confirm that this is what you want to do:</p>
      @ <input type="hidden" name="sn" value="%h(P("sn"))" />
      @ <input type="submit" name="del2" value="Confirm - Delete The Skin" />
      @ <input type="submit" name="cancel" value="Cancel - Do Not Delete" />
      login_insert_csrf_secret();
      @ </div></form>
      style_footer();
      db_end_transaction(1);
      return;
    }
    if( P("del2")!=0 && (zName = skinVarName(P("sn"), 1))!=0 ){
      db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
    }
    if( P("draftdel")!=0 ){
      const char *zDraft = P("name");
      if( sqlite3_strglob("draft[1-9]",zDraft)==0 ){
        db_multi_exec("DELETE FROM config WHERE name GLOB '%q-*'", zDraft);
      }
    }
    if( skinRename() || skinSave(zCurrent) ){
      db_end_transaction(0);
      return;
    }

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
  }

  style_header("Skins");
  if( zErr ){
    @ <p style="color:red">%h(zErr)</p>
  }
  @ <table border="0">
  @ <tr><td colspan=4><h2>Built-in Skins:</h2></td></th>
  for(i=0; i<count(aBuiltinSkin); i++){
    z = aBuiltinSkin[i].zDesc;
    @ <tr><td>%d(i+1).<td>%h(z)<td>&nbsp;&nbsp;<td>
    if( fossil_strcmp(aBuiltinSkin[i].zSQL, zCurrent)==0 ){
      @ (Currently In Use)
      seenCurrent = 1;
    }else{
      @ <form action="%s(g.zTop)/setup_skin_admin" method="post">
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
  once = 1;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zN = db_column_text(&q, 0);
    const char *zV = db_column_text(&q, 1);
    i++;
    if( once ){
      once = 0;
      @ <tr><td colspan=4><h2>Skins saved as "skin:*' entries \
      @ in the CONFIG table:</h2></td></tr>
    }
    @ <tr><td>%d(i).<td>%h(zN)<td>&nbsp;&nbsp;<td>
    @ <form action="%s(g.zTop)/setup_skin_admin" method="post">
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
    @ <tr><td colspan=4><h2>Current skin in css/header/footer/details entries \
    @ in the CONFIG table:</h2></td></tr>
    @ <tr><td>%d(i).<td><i>Current</i><td>&nbsp;&nbsp;<td>
    @ <form action="%s(g.zTop)/setup_skin_admin" method="post">
    @ <input type="submit" name="save" value="Backup">
    @ </form>
  }
  db_prepare(&q,
     "SELECT DISTINCT substr(name, 1, 6) FROM config"
     " WHERE name GLOB 'draft[1-9]-*'"
     " ORDER BY name"
  );
  once = 1;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zN = db_column_text(&q, 0);
    i++;
    if( once ){
      once = 0;
      @ <tr><td colspan=4><h2>Draft skins stored as "draft[1-9]-*' entries \
      @ in the CONFIG table:</h2></td></tr>
    }
    @ <tr><td>%d(i).<td>%h(zN)<td>&nbsp;&nbsp;<td>
    @ <form action="%s(g.zTop)/setup_skin_admin" method="post">
    @ <input type="submit" name="draftdel" value="Delete">
    @ <input type="hidden" name="name" value="%h(zN)">
    @ </form></tr>
  }
  db_finalize(&q);

  @ </table>
  style_footer();
  db_end_transaction(0);
}

/*
** Generate HTML for a <select> that lists all the available skin names,
** except for zExcept if zExcept!=NULL.
*/
static void skin_emit_skin_selector(
  const char *zVarName,      /* Variable name for the <select> */
  const char *zDefault,      /* The default value, if not NULL */
  const char *zExcept        /* Omit this skin if not NULL */
){
  int i;
  @ <select size='1' name='%s(zVarName)'>
  if( fossil_strcmp(zExcept, "current")!=0 ){
    @ <option value='current'>Currently In Use</option>
  }
  for(i=0; i<count(aBuiltinSkin); i++){
    const char *zName = aBuiltinSkin[i].zLabel;
    if( fossil_strcmp(zName, zExcept)==0 ) continue;
    if( fossil_strcmp(zDefault, zName)==0 ){
      @ <option value='%s(zName)' selected>\
      @ %h(aBuiltinSkin[i].zDesc) (built-in)</option>
    }else{
      @ <option value='%s(zName)'>\
      @ %h(aBuiltinSkin[i].zDesc) (built-in)</option>
    }
  }
  for(i=1; i<=9; i++){
    char zName[20];
    sqlite3_snprintf(sizeof(zName), zName, "draft%d", i);
    if( fossil_strcmp(zName, zExcept)==0 ) continue;
    if( fossil_strcmp(zDefault, zName)==0 ){
      @ <option value='%s(zName)' selected>%s(zName)</option>
    }else{
      @ <option value='%s(zName)'>%s(zName)</option>
    }
  }
  @ </select>
}

/*
** Return the text of one of the skin files.
*/
static const char *skin_file_content(const char *zLabel, const char *zFile){
  const char *zResult;
  if( fossil_strcmp(zLabel, "current")==0 ){
    zResult = db_get(zFile, "");
  }else if( sqlite3_strglob("draft[1-9]", zLabel)==0 ){
    zResult = db_get_mprintf("", "%s-%s", zLabel, zFile);
  }else{
    int i;
    for(i=0; i<2; i++){
      char *zKey = mprintf("skins/%s/%s.txt", zLabel, zFile);
      zResult = builtin_text(zKey);
      fossil_free(zKey);
      if( zResult!=0 ) break;
      zLabel = "default";
    }
  }
  return zResult;
}

extern const struct strctCssDefaults {
/* From the generated default_css.h, which we cannot #include here
** without causing an ODR violation.
*/
  const char *elementClass;  /* Name of element needed */
  const char *value;         /* CSS text */
} cssDefaultList[];

/*
** Emits the list of built-in default CSS selectors. Intended
** for use only from the /setup_skinedit page.
*/
static void skin_emit_css_defaults(){
  struct strctCssDefaults const * pCss;
  fossil_print("<h1>CSS Defaults</h1>");
  fossil_print("If a skin defines any of the following CSS selectors, "
               "that definition replaces the default, as opposed to "
               "cascading from it. ");
  fossil_print("See <a href=\"https://fossil-scm.org/fossil/"
               "doc/trunk/www/css-tricks.md\">this "
               "document</a> for more details.");
  /* To discuss: do we want to list only the default selectors or
  ** also their default values? The latter increases the size of the
  ** page considerably, but is arguably more useful. We could, of
  ** course, offer a URL param to toggle the view, but that currently
  ** seems like overkill.
  **
  ** Be sure to adjust the default_css.txt #setup_skinedit_css entry
  ** for whichever impl ends up being selected.
  */
#if 1
  /* List impl which elides style values */
  fossil_print("<div class=\"columns\" "
               "id=\"setup_skinedit_css_defaults\"><ul>");
  for(pCss = &cssDefaultList[0]; pCss->value!=0; ++pCss){
    fossil_print("<li>%s</li>", pCss->elementClass);
  }
  fossil_print("</ul>");
#else
  /* Table impl which also includes style values. */
  fossil_print("<table id=\"setup_skinedit_css_defaults\"><tbody>");
  for(pCss = &cssDefaultList[0]; pCss->value!=0; ++pCss){
    fossil_print("<tr><td>%s</td>", pCss->elementClass);
    /* A TD element apparently cannot be told to scroll its contents,
    ** so we require a DIV inside the value TD to scroll the long
    ** url(data:...) entries. */
    fossil_print("<td><div>%s</div></td>", pCss->value);
    fossil_print("</td></tr>");
  }
  fossil_print("</tbody></table>");
#endif
}

/*
** WEBPAGE: setup_skinedit
**
** Edit aspects of a skin determined by the w= query parameter.
** Requires Admin or Setup privileges.
**
**    w=NUM     -- 0=CSS, 1=footer, 2=header, 3=details, 4=js
**    sk=NUM    -- the draft skin number
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
    /* 4 */ { "js",      "JavaScript",      "Script",  },
  };
  const char *zBasis;         /* The baseline file */
  const char *zOrig;          /* Original content prior to editing */
  const char *zContent;       /* Content after editing */
  const char *zDflt;          /* Default content */
  char *zDraft;               /* Which draft:  "draft%d" */
  char *zKey;                 /* CONFIG table key name: "draft%d-%s" */
  char *zTitle;               /* Title of this page */
  const char *zFile;          /* One of "css", "footer", "header", "details" */
  int iSkin;                  /* draft number.  1..9 */
  int ii;                     /* Index in aSkinAttr[] of this file */
  int j;                      /* Loop counter */
  int isRevert = 0;           /* True if Revert-to-Baseline was pressed */

  login_check_credentials();

  /* Figure out which skin we are editing */
  iSkin = atoi(PD("sk","1"));
  if( iSkin<1 || iSkin>9 ) iSkin = 1;

  /* Check that the user is authorized to edit this skin. */
  if( !g.perm.Admin ){
    char *zAllowedEditors = "";
    Glob *pAllowedEditors;
    int isMatch = 0;
    if( login_is_individual() ){
      zAllowedEditors = db_get_mprintf("", "draft%d-users", iSkin);
    }
    if( zAllowedEditors[0] ){
      pAllowedEditors = glob_create(zAllowedEditors);
      isMatch = glob_match(pAllowedEditors, g.zLogin);
      glob_free(pAllowedEditors);
    }
    if( isMatch==0 ){
      login_needed(0);
      return;
    }
  }

  /* figure out which file is to be edited */
  ii = atoi(PD("w","0"));
  if( ii<0 || ii>count(aSkinAttr) ) ii = 0;
  zFile = aSkinAttr[ii].zFile;
  zDraft = mprintf("draft%d", iSkin);
  zKey = mprintf("draft%d-%s", iSkin, zFile);
  zTitle = mprintf("%s for Draft%d", aSkinAttr[ii].zTitle, iSkin);
  zBasis = PD("basis","current");
  zDflt = skin_file_content(zBasis, zFile);
  zOrig = db_get(zKey, zDflt);
  zContent = PD(zFile,zOrig);
  if( P("revert")!=0 && cgi_csrf_safe(0) ){
    zContent = zDflt;
    isRevert = 1;
  }

  db_begin_transaction();
  style_header("%s", zTitle);
  for(j=0; j<count(aSkinAttr); j++){
    style_submenu_element(aSkinAttr[j].zSubmenu,
          "%R/setup_skinedit?w=%d&basis=%h&sk=%d",j,zBasis,iSkin);
  }
  @ <form action="%s(g.zTop)/setup_skinedit" method="post"><div>
  login_insert_csrf_secret();
  @ <input type='hidden' name='w' value='%d(ii)'>
  @ <input type='hidden' name='sk' value='%d(iSkin)'>
  @ <h2>Edit %s(zTitle):</h2>
  if( P("submit") && cgi_csrf_safe(0) && strcmp(zOrig,zContent)!=0 ){
    db_set(zKey, zContent, 0);
  }
  @ <textarea name="%s(zFile)" rows="10" cols="80">\
  @ %h(zContent)</textarea>
  @ <br />
  @ <input type="submit" name="submit" value="Apply Changes" />
  if( isRevert ){
    @ &larr; Press to complete reversion to "%s(zBasis)"
  }else if( fossil_strcmp(zContent,zDflt)!=0 ){
    @ <input type="submit" name="revert" value='Revert To "%s(zBasis)"' />
  }
  @ <hr />
  @ Baseline: \
  skin_emit_skin_selector("basis", zBasis, zDraft);
  @ <input type="submit" name="diff" value="Unified Diff" />
  @ <input type="submit" name="sbsdiff" value="Side-by-Side Diff" />
  if( P("diff")!=0 || P("sbsdiff")!=0 ){
    u64 diffFlags = construct_diff_flags(1) | DIFF_STRIP_EOLCR;
    Blob from, to, out;
    if( P("sbsdiff")!=0 ) diffFlags |= DIFF_SIDEBYSIDE;
    blob_init(&to, zContent, -1);
    blob_init(&from, skin_file_content(zBasis, zFile), -1);
    blob_zero(&out);
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
  if(ii==0/*CSS*/){
    skin_emit_css_defaults();
  }
  style_footer();
  db_end_transaction(0);
}

/*
** Try to initialize draft skin iSkin to the built-in or preexisting
** skin named by zTemplate.
*/
static void skin_initialize_draft(int iSkin, const char *zTemplate){
  int i;
  if( zTemplate==0 ) return;
  for(i=0; i<count(azSkinFile); i++){
    const char *z = skin_file_content(zTemplate, azSkinFile[i]);
    db_set_mprintf(z, 0, "draft%d-%s", iSkin, azSkinFile[i]);
  }
}

/*
** Publish the draft skin iSkin as the new default.
*/
static void skin_publish(int iSkin){
  char *zCurrent;    /* SQL description of the current skin */
  char *zBuiltin;    /* SQL description of a built-in skin */
  int i;
  int seen = 0;      /* True if no need to make a backup */

  /* Check to see if the current skin is already saved.  If it is, there
  ** is no need to create a backup */
  zCurrent = getSkin(0);
  for(i=0; i<count(aBuiltinSkin); i++){
    zBuiltin = getSkin(aBuiltinSkin[i].zLabel);
    if( fossil_strcmp(zBuiltin, zCurrent)==0 ){
      seen = 1;
      break;
    }
  }
  if( !seen ){
    seen = db_exists("SELECT 1 FROM config WHERE name GLOB 'skin:*'"
                       " AND value=%Q", zCurrent);
  }
  if( !seen ){
    db_multi_exec(
      "INSERT INTO config(name,value,mtime) VALUES("
      "  strftime('skin:Backup On %%Y-%%m-%%d %%H:%%M:%%S'),"
      "  %Q,now())", zCurrent
    );
  }

  /* Publish draft iSkin */
  for(i=0; i<count(azSkinFile); i++){
    char *zNew = db_get_mprintf("", "draft%d-%s", iSkin, azSkinFile[i]);
    db_set(azSkinFile[i], zNew, 0);
  }
}

/*
** WEBPAGE: setup_skin
**
** Generate a page showing the steps needed to customize a skin.
*/
void setup_skin(void){
  int i;          /* Loop counter */
  int iSkin;      /* Which draft skin is being edited */
  int isSetup;    /* True for an administrator */
  int isEditor;   /* Others authorized to make edits */
  char *zAllowedEditors;   /* Who may edit the draft skin */
  char *zBase;             /* Base URL for draft under test */
  static const char *const azTestPages[] = {
     "home",
     "timeline",
     "dir?ci=tip",
     "dir?ci=tip&type=tree",
     "brlist",
     "info/trunk",
  };

  /* Figure out which skin we are editing */
  iSkin = atoi(PD("sk","1"));
  if( iSkin<1 || iSkin>9 ) iSkin = 1;

  /* Figure out if the current user is allowed to make administrative
  ** changes and/or edits
  */
  login_check_credentials();
  if( !login_is_individual() ){
    login_needed(0);
    return;
  }
  zAllowedEditors = db_get_mprintf("", "draft%d-users", iSkin);
  if( g.perm.Admin ){
    isSetup = isEditor = 1;
  }else{
    Glob *pAllowedEditors;
    isSetup = isEditor = 0;
    if( zAllowedEditors[0] ){
      pAllowedEditors = glob_create(zAllowedEditors);
      isEditor = glob_match(pAllowedEditors, g.zLogin);
      glob_free(pAllowedEditors);
    }
  }

  /* Initialize the skin, if requested and authorized. */
  if( P("init3")!=0 && isEditor ){
    skin_initialize_draft(iSkin, P("initskin"));
  }
  if( P("submit2")!=0 && isSetup ){
    db_set_mprintf(PD("editors",""), 0, "draft%d-users", iSkin);
    zAllowedEditors = db_get_mprintf("", "draft%d-users", iSkin);
  }

  /* Publish the draft skin */
  if( P("pub7")!=0 && PB("pub7ck1") && PB("pub7ck2") ){
    skin_publish(iSkin);
  }

  style_header("Customize Skin");

  @ <p>Customize the look of this Fossil repository by making changes
  @ to the CSS, Header, Footer, and Detail Settings in one of nine "draft"
  @ configurations.  Then, after verifying that all is working correctly,
  @ publish the draft to become the new main Skin.<p>
  @
  @ <a name='step1'></a>
  @ <h1>Step 1: Identify Which Draft To Use</h1>
  @
  @ <p>The main skin of Fossil cannot be edited directly.  Instead,
  @ edits are made to one of nine draft skins.  A draft skin can then
  @ be published to become the default skin.
  @ Nine separate drafts are available to facilitate A/B testing.</p>
  @
  @ <form method='POST' action='%R/setup_skin#step2' id='f01'>
  @ <p class='skinInput'>Draft skin to edit:
  @ <select size='1' name='sk' id='skStep1'>
  for(i=1; i<=9; i++){
    if( i==iSkin ){
      @ <option value='%d(i)' selected>draft%d(i)</option>
    }else{
      @ <option value='%d(i)'>draft%d(i)</option>
    }
  }
  @ </select>
  @ </p>
  @
  @ <a name='step2'></a>
  @ <h1>Step 2: Authenticate</h1>
  @
  if( isSetup ){
    @ <p>As an administrator, you can make any edits you like to this or
    @ any other skin.  You can also authorize other users to edit this
    @ skin.  Any user whose login name matches the comma-separated list
    @ of GLOB expressions below is given special permission to edit
    @ the draft%d(iSkin) skin:
    @
    @ <form method='POST' action='%R/setup_skin#step2' id='f02'>
    @ <p class='skinInput'>
    @ <input type='hidden' name='sk' value='%d(iSkin)'>
    @ Authorized editors for skin draft%d(iSkin):
    @ <input type='text' name='editors' value='%h(zAllowedEditors)'\
    @  width='40'>
    @ <input type='submit' name='submit2' value='Change'>
    @ </p>
    @ </form>
  }else if( isEditor ){
    @ <p>You are authorized to make changes to the draft%d(iSkin) skin.
    @ Continue to the <a href='#step3'>next step</a>.</p>
  }else{
    @ <p>You are not authorized to make changes to the draft%d(iSkin)
    @ skin.  Contact the administrator of this Fossil repository for
    @ further information.</p>
  }
  @
  @ <a name='step3'></a>
  @ <h1>Step 3: Initialize The Draft</h1>
  @
  if( !isEditor ){
    @ <p>You are not allowed to initialize draft%d(iSkin).  Contact
    @ the administrator for this repository for more information.
  }else{
    @ <p>Initialize the draft%d(iSkin) skin to one of the built-in skins
    @ or a preexisting skin, to use as a baseline.</p>
    @
    @ <form method='POST' action='%R/setup_skin#step4' id='f03'>
    @ <p class='skinInput'>
    @ <input type='hidden' name='sk' value='%d(iSkin)'>
    @ Initialize skin <b>draft%d(iSkin)</b> using
    skin_emit_skin_selector("initskin", "current", 0);
    @ <input type='submit' name='init3' value='Go'>
    @ </p>
    @ </form>
  }
  @
  @ <a name='step4'></a>
  @ <h1>Step 4: Make Edits</h1>
  @
  if( !isEditor ){
    @ <p>You are not authorized to make edits to the draft%d(iSkin) skin.
    @ Contact the administrator of this Fossil repository for help.</p>
  }else{
    @ <p>Edit the components of the draft%d(iSkin) skin:
    @ <ul>
    @ <li><a href='%R/setup_skinedit?w=0&sk=%d(iSkin)' target='_blank'>CSS</a>
    @ <li><a href='%R/setup_skinedit?w=2&sk=%d(iSkin)' target='_blank'>\
    @ Header</a>
    @ <li><a href='%R/setup_skinedit?w=1&sk=%d(iSkin)' target='_blank'>\
    @ Footer</a>
    @ <li><a href='%R/setup_skinedit?w=3&sk=%d(iSkin)' target='_blank'>\
    @ Details</a>
    @ <li><a href='%R/setup_skinedit?w=4&sk=%d(iSkin)' target='_blank'>\
    @ Javascript</a> (optional)
    @ </ul>
  }
  @
  @ <a name='step5'></a>
  @ <h1>Step 5: Verify The Draft Skin</h1>
  @
  @ <p>To test this draft skin, insert text "/draft%d(iSkin)/" just before the
  @ operation name in the URL.  Here are a few links to try:
  @ <ul>
  if( iDraftSkin && sqlite3_strglob("*/draft[1-9]", g.zBaseURL)==0 ){
    zBase = mprintf("%.*s/draft%d", (int)strlen(g.zBaseURL)-7,g.zBaseURL,iSkin);
  }else{
    zBase = mprintf("%s/draft%d", g.zBaseURL, iSkin);
  }
  for(i=0; i<count(azTestPages); i++){
    @ <li><a href='%s(zBase)/%s(azTestPages[i])' target='_blank'>\
    @ %s(zBase)/%s(azTestPages[i])</a>
  }
  fossil_free(zBase);
  @ </ul>
  @
  @ <p>You will probably need to press Reload on your browser before any
  @ CSS changes will take effect.</p>
  @
  @ <a hame='step6'></a>
  @ <h1>Step 6: Iterate</h1>
  @
  @ <p>Repeat <a href='#step4'>step 4</a> and
  @ <a href='#step5'>step 5</a> as many times as necessary to create
  @ a production-ready skin.
  @
  @ <a name='step7'></a>
  @ <h1>Step 7: Publish</h1>
  @
  if( !g.perm.Admin ){
    @ <p>Only administrators are allowed to publish draft skins.  Contact
    @ an administrator to get this "draft%d(iSkin)" skin published.</p>
  }else{
    @ <p>When the draft%d(iSkin) skin is ready for production use,
    @ make it the default scan by clicking the acknowledgements and
    @ pressing the button below:</p>
    @
    @ <form method='POST' action='%R/setup_skin#step7'>
    @ <p class='skinInput'>
    @ <input type='hidden' name='sk' value='%d(iSkin)'>
    @ <input type='checkbox' name='pub7ck1' value='yes'>\
    @ Skin draft%d(iSkin) has been tested and found ready for production.<br>
    @ <input type='checkbox' name='pub7ck2' value='yes'>\
    @ The current skin should be overwritten with draft%d(iSkin).<br>
    @ <input type='submit' name='pub7' value='Publish Draft%d(iSkin)'>
    @ </p></form>
    @
    @ <p>You will probably need to press Reload on your browser after
    @ publishing the new skin.</p>
  }
  @
  @ <a name='step8'></a>
  @ <h1>Step 8: Cleanup and Undo Actions</h1>
  @
  if( !g.perm.Admin ){
    @ <p>Administrators can optionally save or restore legacy skins, and/or
    @ undo a prior publish.
  }else{
    @ <p>Visit the <a href='%R/setup_skin_admin'>Skin Admin</a> page
    @ for cleanup and recovery actions.
  }
  style_load_one_js_file("skin.js");
  style_footer();
}
