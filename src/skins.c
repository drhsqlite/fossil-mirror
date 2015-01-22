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
  { "Plain Gray, No Logo",               "plain_gray",        0 },
  { "Khaki, No Logo",                    "khaki",             0 },
  { "Black & White, Menu on Left",       "black_and_white",   0 }, 
  { "Shadow boxes & Rounded Corners",    "rounded1",          0 },
  { "Enhanced Default",                  "enhanced1",         0 },
  { "Similar to GitHub",                 "etienne1",          0 },
};

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
  static const char *azType[] = { "css", "header", "footer" };
  int i;
  Blob val;
  blob_zero(&val);
  for(i=0; i<sizeof(azType)/sizeof(azType[0]); i++){
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
** WEBPAGE: setup_skin
*/
void setup_skin(void){
  const char *z;
  char *zName;
  char *zErr = 0;
  const char *zCurrent = 0;  /* Current skin */
  int i;                     /* Loop counter */
  Stmt q;

  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }
  db_begin_transaction();
  zCurrent = getSkin(0);
  for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
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
  if( P("save")!=0 && (zName = skinVarName(P("save"),0))!=0 ){
    if( db_exists("SELECT 1 FROM config WHERE name=%Q", zName)
          || fossil_strcmp(zName, "Default")==0 ){
      zErr = mprintf("Skin name \"%h\" already exists. "
                     "Choose a different name.", P("sn"));
    }else{
      db_multi_exec("INSERT INTO config(name,value,mtime) VALUES(%Q,%Q,now())",
         zName, zCurrent
      );
    }
  }

  /* The user pressed one of the "Install" buttons. */
  if( P("load") && (z = P("sn"))!=0 && z[0] ){
    int seen = 0;

    /* Check to see if the current skin is already saved.  If it is, there
    ** is no need to create a backup */
    zCurrent = getSkin(0);
    for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
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
    for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
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
    @ <p><font color="red">%h(zErr)</font></p>
  }
  @ <p>A "skin" is a combination of
  @ <a href="setup_editcss">CSS</a>,
  @ <a href="setup_header">Header</a>, and
  @ <a href="setup_footer">Footer</a> that determines the look and feel
  @ of the web interface.</p>
  @
  @ <h2>Available Skins:</h2>
  @ <table border="0">
  for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
    z = aBuiltinSkin[i].zDesc;
    @ <tr><td>%d(i+1).<td>%h(z)<td>&nbsp;&nbsp;<td>
    if( fossil_strcmp(aBuiltinSkin[i].zSQL, zCurrent)==0 ){
      @ (Currently In Use)
    }else{
      @ <form action="%s(g.zTop)/setup_skin" method="post">
      @ <input type="hidden" name="sn" value="%h(z)" />
      @ <input type="submit" name="load" value="Install" />
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
    if( fossil_strcmp(zV, zCurrent)==0 ){
      @ (Currently In Use)
    }else{
      @ <form action="%s(g.zTop)/setup_skin" method="post">
      @ <input type="hidden" name="sn" value="%h(zN)">
      @ <input type="submit" name="load" value="Install">
      @ <input type="submit" name="del1" value="Delete">
      @ </form>
    }
    @ </tr>
  }
  db_finalize(&q);
  @ </table>
  style_footer();
  db_end_transaction(0);
}
