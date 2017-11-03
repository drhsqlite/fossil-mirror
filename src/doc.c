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
** This file contains code to implement the "/doc" web page and related
** pages.
*/
#include "config.h"
#include "doc.h"
#include <assert.h>

/*
** Try to guess the mimetype from content.
**
** If the content is pure text, return NULL.
**
** For image types, attempt to return an appropriate mimetype
** name like "image/gif" or "image/jpeg".
**
** For any other binary type, return "unknown/unknown".
*/
const char *mimetype_from_content(Blob *pBlob){
  int i;
  int n;
  const unsigned char *x;

  /* A table of mimetypes based on file content prefixes
  */
  static const struct {
    const char *zPrefix;       /* The file prefix */
    int size;                  /* Length of the prefix */
    const char *zMimetype;     /* The corresponding mimetype */
  } aMime[] = {
    { "GIF87a",                  6, "image/gif"  },
    { "GIF89a",                  6, "image/gif"  },
    { "\211PNG\r\n\032\n",       8, "image/png"  },
    { "\377\332\377",            3, "image/jpeg" },
    { "\377\330\377",            3, "image/jpeg" },
  };

  if( !looks_like_binary(pBlob) ) {
    return 0;   /* Plain text */
  }
  x = (const unsigned char*)blob_buffer(pBlob);
  n = blob_size(pBlob);
  for(i=0; i<count(aMime); i++){
    if( n>=aMime[i].size && memcmp(x, aMime[i].zPrefix, aMime[i].size)==0 ){
      return aMime[i].zMimetype;
    }
  }
  return "unknown/unknown";
}

/* A table of mimetypes based on file suffixes.
** Suffixes must be in sorted order so that we can do a binary
** search to find the mime-type
*/
static const struct {
  const char *zSuffix;       /* The file suffix */
  int size;                  /* Length of the suffix */
  const char *zMimetype;     /* The corresponding mimetype */
} aMime[] = {
  { "ai",         2, "application/postscript"            },
  { "aif",        3, "audio/x-aiff"                      },
  { "aifc",       4, "audio/x-aiff"                      },
  { "aiff",       4, "audio/x-aiff"                      },
  { "arj",        3, "application/x-arj-compressed"      },
  { "asc",        3, "text/plain"                        },
  { "asf",        3, "video/x-ms-asf"                    },
  { "asx",        3, "video/x-ms-asx"                    },
  { "au",         2, "audio/ulaw"                        },
  { "avi",        3, "video/x-msvideo"                   },
  { "bat",        3, "application/x-msdos-program"       },
  { "bcpio",      5, "application/x-bcpio"               },
  { "bin",        3, "application/octet-stream"          },
  { "bmp",        3, "image/bmp"                         },
  { "bz2",        3, "application/x-bzip2"               },
  { "bzip",       4, "application/x-bzip"                },
  { "c",          1, "text/plain"                        },
  { "cc",         2, "text/plain"                        },
  { "ccad",       4, "application/clariscad"             },
  { "cdf",        3, "application/x-netcdf"              },
  { "class",      5, "application/octet-stream"          },
  { "cod",        3, "application/vnd.rim.cod"           },
  { "com",        3, "application/x-msdos-program"       },
  { "cpio",       4, "application/x-cpio"                },
  { "cpt",        3, "application/mac-compactpro"        },
  { "cs",         2, "text/plain"                        },
  { "csh",        3, "application/x-csh"                 },
  { "css",        3, "text/css"                          },
  { "csv",        3, "text/csv"                          },
  { "dcr",        3, "application/x-director"            },
  { "deb",        3, "application/x-debian-package"      },
  { "dib",        3, "image/bmp"                         },
  { "dir",        3, "application/x-director"            },
  { "dl",         2, "video/dl"                          },
  { "dms",        3, "application/octet-stream"          },
  { "doc",        3, "application/msword"                },
  { "docx",       4, "application/vnd.openxmlformats-"
                     "officedocument.wordprocessingml.document"},
  { "dot",        3, "application/msword"                },
  { "dotx",       4, "application/vnd.openxmlformats-"
                     "officedocument.wordprocessingml.template"},
  { "drw",        3, "application/drafting"              },
  { "dvi",        3, "application/x-dvi"                 },
  { "dwg",        3, "application/acad"                  },
  { "dxf",        3, "application/dxf"                   },
  { "dxr",        3, "application/x-director"            },
  { "eps",        3, "application/postscript"            },
  { "etx",        3, "text/x-setext"                     },
  { "exe",        3, "application/octet-stream"          },
  { "ez",         2, "application/andrew-inset"          },
  { "f",          1, "text/plain"                        },
  { "f90",        3, "text/plain"                        },
  { "fli",        3, "video/fli"                         },
  { "flv",        3, "video/flv"                         },
  { "gif",        3, "image/gif"                         },
  { "gl",         2, "video/gl"                          },
  { "gtar",       4, "application/x-gtar"                },
  { "gz",         2, "application/x-gzip"                },
  { "h",          1, "text/plain"                        },
  { "hdf",        3, "application/x-hdf"                 },
  { "hh",         2, "text/plain"                        },
  { "hqx",        3, "application/mac-binhex40"          },
  { "htm",        3, "text/html"                         },
  { "html",       4, "text/html"                         },
  { "ice",        3, "x-conference/x-cooltalk"           },
  { "ico",        3, "image/vnd.microsoft.icon"          },
  { "ief",        3, "image/ief"                         },
  { "iges",       4, "model/iges"                        },
  { "igs",        3, "model/iges"                        },
  { "ips",        3, "application/x-ipscript"            },
  { "ipx",        3, "application/x-ipix"                },
  { "jad",        3, "text/vnd.sun.j2me.app-descriptor"  },
  { "jar",        3, "application/java-archive"          },
  { "jpe",        3, "image/jpeg"                        },
  { "jpeg",       4, "image/jpeg"                        },
  { "jpg",        3, "image/jpeg"                        },
  { "js",         2, "application/x-javascript"          },
  { "kar",        3, "audio/midi"                        },
  { "latex",      5, "application/x-latex"               },
  { "lha",        3, "application/octet-stream"          },
  { "lsp",        3, "application/x-lisp"                },
  { "lzh",        3, "application/octet-stream"          },
  { "m",          1, "text/plain"                        },
  { "m3u",        3, "audio/x-mpegurl"                   },
  { "man",        3, "text/plain"                        },
  { "markdown",   8, "text/x-markdown"                   },
  { "md",         2, "text/x-markdown"                   },
  { "me",         2, "application/x-troff-me"            },
  { "mesh",       4, "model/mesh"                        },
  { "mid",        3, "audio/midi"                        },
  { "midi",       4, "audio/midi"                        },
  { "mif",        3, "application/x-mif"                 },
  { "mime",       4, "www/mime"                          },
  { "mkd",        3, "text/x-markdown"                   },
  { "mov",        3, "video/quicktime"                   },
  { "movie",      5, "video/x-sgi-movie"                 },
  { "mp2",        3, "audio/mpeg"                        },
  { "mp3",        3, "audio/mpeg"                        },
  { "mp4",        3, "video/mp4"                         },
  { "mpe",        3, "video/mpeg"                        },
  { "mpeg",       4, "video/mpeg"                        },
  { "mpg",        3, "video/mpeg"                        },
  { "mpga",       4, "audio/mpeg"                        },
  { "ms",         2, "application/x-troff-ms"            },
  { "msh",        3, "model/mesh"                        },
  { "n",          1, "text/plain"                        },
  { "nc",         2, "application/x-netcdf"              },
  { "oda",        3, "application/oda"                   },
  { "odp",        3, "application/vnd.oasis.opendocument.presentation" },
  { "ods",        3, "application/vnd.oasis.opendocument.spreadsheet" },
  { "odt",        3, "application/vnd.oasis.opendocument.text" },
  { "ogg",        3, "application/ogg"                   },
  { "ogm",        3, "application/ogg"                   },
  { "pbm",        3, "image/x-portable-bitmap"           },
  { "pdb",        3, "chemical/x-pdb"                    },
  { "pdf",        3, "application/pdf"                   },
  { "pgm",        3, "image/x-portable-graymap"          },
  { "pgn",        3, "application/x-chess-pgn"           },
  { "pgp",        3, "application/pgp"                   },
  { "pl",         2, "application/x-perl"                },
  { "pm",         2, "application/x-perl"                },
  { "png",        3, "image/png"                         },
  { "pnm",        3, "image/x-portable-anymap"           },
  { "pot",        3, "application/mspowerpoint"          },
  { "potx",       4, "application/vnd.openxmlformats-"
                     "officedocument.presentationml.template"},
  { "ppm",        3, "image/x-portable-pixmap"           },
  { "pps",        3, "application/mspowerpoint"          },
  { "ppsx",       4, "application/vnd.openxmlformats-"
                     "officedocument.presentationml.slideshow"},
  { "ppt",        3, "application/mspowerpoint"          },
  { "pptx",       4, "application/vnd.openxmlformats-"
                     "officedocument.presentationml.presentation"},
  { "ppz",        3, "application/mspowerpoint"          },
  { "pre",        3, "application/x-freelance"           },
  { "prt",        3, "application/pro_eng"               },
  { "ps",         2, "application/postscript"            },
  { "qt",         2, "video/quicktime"                   },
  { "ra",         2, "audio/x-realaudio"                 },
  { "ram",        3, "audio/x-pn-realaudio"              },
  { "rar",        3, "application/x-rar-compressed"      },
  { "ras",        3, "image/cmu-raster"                  },
  { "rgb",        3, "image/x-rgb"                       },
  { "rm",         2, "audio/x-pn-realaudio"              },
  { "roff",       4, "application/x-troff"               },
  { "rpm",        3, "audio/x-pn-realaudio-plugin"       },
  { "rtf",        3, "text/rtf"                          },
  { "rtx",        3, "text/richtext"                     },
  { "scm",        3, "application/x-lotusscreencam"      },
  { "set",        3, "application/set"                   },
  { "sgm",        3, "text/sgml"                         },
  { "sgml",       4, "text/sgml"                         },
  { "sh",         2, "application/x-sh"                  },
  { "shar",       4, "application/x-shar"                },
  { "silo",       4, "model/mesh"                        },
  { "sit",        3, "application/x-stuffit"             },
  { "skd",        3, "application/x-koan"                },
  { "skm",        3, "application/x-koan"                },
  { "skp",        3, "application/x-koan"                },
  { "skt",        3, "application/x-koan"                },
  { "smi",        3, "application/smil"                  },
  { "smil",       4, "application/smil"                  },
  { "snd",        3, "audio/basic"                       },
  { "sol",        3, "application/solids"                },
  { "spl",        3, "application/x-futuresplash"        },
  { "src",        3, "application/x-wais-source"         },
  { "step",       4, "application/STEP"                  },
  { "stl",        3, "application/SLA"                   },
  { "stp",        3, "application/STEP"                  },
  { "sv4cpio",    7, "application/x-sv4cpio"             },
  { "sv4crc",     6, "application/x-sv4crc"              },
  { "svg",        3, "image/svg+xml"                     },
  { "swf",        3, "application/x-shockwave-flash"     },
  { "t",          1, "application/x-troff"               },
  { "tar",        3, "application/x-tar"                 },
  { "tcl",        3, "application/x-tcl"                 },
  { "tex",        3, "application/x-tex"                 },
  { "texi",       4, "application/x-texinfo"             },
  { "texinfo",    7, "application/x-texinfo"             },
  { "tgz",        3, "application/x-tar-gz"              },
  { "th1",        3, "application/x-th1"                 },
  { "tif",        3, "image/tiff"                        },
  { "tiff",       4, "image/tiff"                        },
  { "tr",         2, "application/x-troff"               },
  { "tsi",        3, "audio/TSP-audio"                   },
  { "tsp",        3, "application/dsptype"               },
  { "tsv",        3, "text/tab-separated-values"         },
  { "txt",        3, "text/plain"                        },
  { "unv",        3, "application/i-deas"                },
  { "ustar",      5, "application/x-ustar"               },
  { "vb",         2, "text/plain"                        },
  { "vcd",        3, "application/x-cdlink"              },
  { "vda",        3, "application/vda"                   },
  { "viv",        3, "video/vnd.vivo"                    },
  { "vivo",       4, "video/vnd.vivo"                    },
  { "vrml",       4, "model/vrml"                        },
  { "wav",        3, "audio/x-wav"                       },
  { "wax",        3, "audio/x-ms-wax"                    },
  { "wiki",       4, "text/x-fossil-wiki"                },
  { "wma",        3, "audio/x-ms-wma"                    },
  { "wmv",        3, "video/x-ms-wmv"                    },
  { "wmx",        3, "video/x-ms-wmx"                    },
  { "wrl",        3, "model/vrml"                        },
  { "wvx",        3, "video/x-ms-wvx"                    },
  { "xbm",        3, "image/x-xbitmap"                   },
  { "xlc",        3, "application/vnd.ms-excel"          },
  { "xll",        3, "application/vnd.ms-excel"          },
  { "xlm",        3, "application/vnd.ms-excel"          },
  { "xls",        3, "application/vnd.ms-excel"          },
  { "xlsx",       4, "application/vnd.openxmlformats-"
                     "officedocument.spreadsheetml.sheet"},
  { "xlw",        3, "application/vnd.ms-excel"          },
  { "xml",        3, "text/xml"                          },
  { "xpm",        3, "image/x-xpixmap"                   },
  { "xwd",        3, "image/x-xwindowdump"               },
  { "xyz",        3, "chemical/x-pdb"                    },
  { "zip",        3, "application/zip"                   },
};

/*
** Verify that all entries in the aMime[] table are in sorted order.
** Abort with a fatal error if any is out-of-order.
*/
static void mimetype_verify(void){
  int i;
  for(i=1; i<count(aMime); i++){
    if( fossil_strcmp(aMime[i-1].zSuffix,aMime[i].zSuffix)>=0 ){
      fossil_fatal("mimetypes out of sequence: %s before %s",
                   aMime[i-1].zSuffix, aMime[i].zSuffix);
    }
  }
}

/*
** Guess the mime-type of a document based on its name.
*/
const char *mimetype_from_name(const char *zName){
  const char *z;
  int i;
  int first, last;
  int len;
  char zSuffix[20];


#ifdef FOSSIL_DEBUG
  /* This is test code to make sure the table above is in the correct
  ** order
  */
  if( fossil_strcmp(zName, "mimetype-test")==0 ){
    mimetype_verify();
    return "ok";
  }
#endif

  z = zName;
  for(i=0; zName[i]; i++){
    if( zName[i]=='.' ) z = &zName[i+1];
  }
  len = strlen(z);
  if( len<sizeof(zSuffix)-1 ){
    sqlite3_snprintf(sizeof(zSuffix), zSuffix, "%s", z);
    for(i=0; zSuffix[i]; i++) zSuffix[i] = fossil_tolower(zSuffix[i]);
    first = 0;
    last = count(aMime) - 1;
    while( first<=last ){
      int c;
      i = (first+last)/2;
      c = fossil_strcmp(zSuffix, aMime[i].zSuffix);
      if( c==0 ) return aMime[i].zMimetype;
      if( c<0 ){
        last = i-1;
      }else{
        first = i+1;
      }
    }
  }
  return "application/x-fossil-artifact";
}

/*
** COMMAND: test-mimetype
**
** Usage: %fossil test-mimetype FILENAME...
**
** Return the deduced mimetype for each file listed.
**
** If Fossil is compiled with -DFOSSIL_DEBUG then the "mimetype-test"
** filename is special and verifies the integrity of the mimetype table.
** It should return "ok".
*/
void mimetype_test_cmd(void){
  int i;
  mimetype_verify();
  for(i=2; i<g.argc; i++){
    fossil_print("%-20s -> %s\n", g.argv[i], mimetype_from_name(g.argv[i]));
  }
}

/*
** WEBPAGE: mimetype_list
**
** Show the built-in table used to guess embedded document mimetypes
** from file suffixes.
*/
void mimetype_list_page(void){
  int i;
  mimetype_verify();
  style_header("Mimetype List");
  @ <p>The Fossil <a href="%R/help?cmd=/doc">/doc</a> page uses filename
  @ suffixes and the following table to guess at the appropriate mimetype
  @ for each document.</p>
  @ <table id='mimeTable' border=1 cellpadding=0 class='mimetypetable'>
  @ <thead>
  @ <tr><th>Suffix<th>Mimetype
  @ </thead>
  @ <tbody>
  for(i=0; i<count(aMime); i++){
    @ <tr><td>%h(aMime[i].zSuffix)<td>%h(aMime[i].zMimetype)</tr>
  }
  @ </tbody></table>
  output_table_sorting_javascript("mimeTable","tt",1);
  style_footer();
}

/*
** Check to see if the file in the pContent blob is "embedded HTML".  Return
** true if it is, and fill pTitle with the document title.
**
** An "embedded HTML" file is HTML that lacks a header and a footer.  The
** standard Fossil header is prepended and the standard Fossil footer is
** appended.  Otherwise, the file is displayed without change.
**
** Embedded HTML must be contained in a <div class='fossil-doc'> element.
** If that <div> also contains a data-title attribute, then the
** value of that attribute is extracted into pTitle and becomes the title
** of the document.
*/
int doc_is_embedded_html(Blob *pContent, Blob *pTitle){
  const char *zIn = blob_str(pContent);
  const char *zAttr;
  const char *zValue;
  int nAttr, nValue;
  int seenClass = 0;
  int seenTitle = 0;

  while( fossil_isspace(zIn[0]) ) zIn++;
  if( fossil_strnicmp(zIn,"<div",4)!=0 ) return 0;
  zIn += 4;
  while( zIn[0] ){
    if( fossil_isspace(zIn[0]) ) zIn++;
    if( zIn[0]=='>' ) return 0;
    zAttr = zIn;
    while( fossil_isalnum(zIn[0]) || zIn[0]=='-' ) zIn++;
    nAttr = (int)(zIn - zAttr);
    while( fossil_isspace(zIn[0]) ) zIn++;
    if( zIn[0]!='=' ) continue;
    zIn++;
    while( fossil_isspace(zIn[0]) ) zIn++;
    if( zIn[0]=='"' || zIn[0]=='\'' ){
      char cDelim = zIn[0];
      zIn++;
      zValue = zIn;
      while( zIn[0] && zIn[0]!=cDelim ) zIn++;
      if( zIn[0]==0 ) return 0;
      nValue = (int)(zIn - zValue);
      zIn++;
    }else{
      zValue = zIn;
      while( zIn[0]!=0 && zIn[0]!='>' && zIn[0]!='/'
            && !fossil_isspace(zIn[0]) ) zIn++;
      if( zIn[0]==0 ) return 0;
      nValue = (int)(zIn - zValue);
    }
    if( nAttr==5 && fossil_strnicmp(zAttr,"class",5)==0 ){
      if( nValue!=10 || fossil_strnicmp(zValue,"fossil-doc",10)!=0 ) return 0;
      seenClass = 1;
      if( seenTitle ) return 1;
    }
    if( nAttr==10 && fossil_strnicmp(zAttr,"data-title",10)==0 ){
      blob_append(pTitle, zValue, nValue);
      seenTitle = 1;
      if( seenClass ) return 1;
    }
  }
  return seenClass;
}

/*
** Look for a file named zName in the check-in with RID=vid.  Load the content
** of that file into pContent and return the RID for the file.  Or return 0
** if the file is not found or could not be loaded.
*/
int doc_load_content(int vid, const char *zName, Blob *pContent){
  int writable = db_is_writeable("repository");
  int rid;   /* The RID of the file being loaded */
  if( !db_table_exists("repository", "vcache") || !writable ){
    db_multi_exec(
      "CREATE %s TABLE IF NOT EXISTS vcache(\n"
      "  vid INTEGER,         -- check-in ID\n"
      "  fname TEXT,          -- filename\n"
      "  rid INTEGER,         -- artifact ID\n"
      "  PRIMARY KEY(vid,fname)\n"
      ") WITHOUT ROWID", writable ? "" : "TEMPORARY"
    );
  }
  if( !db_exists("SELECT 1 FROM vcache WHERE vid=%d", vid) ){
    db_multi_exec(
      "DELETE FROM vcache;\n"
      "CREATE VIRTUAL TABLE IF NOT EXISTS temp.foci USING files_of_checkin;\n"
      "INSERT INTO vcache(vid,fname,rid)"
      "  SELECT checkinID, filename, blob.rid FROM foci, blob"
      "   WHERE blob.uuid=foci.uuid"
      "     AND foci.checkinID=%d;",
      vid
    );
  }
  rid = db_int(0, "SELECT rid FROM vcache"
                  " WHERE vid=%d AND fname=%Q", vid, zName);
  if( rid && content_get(rid, pContent)==0 ){
    rid = 0;
  }
  return rid;
}

/*
** Transfer content to the output.  During the transfer, when text of
** the following form is seen:
**
**       href="$ROOT/
**       action="$ROOT/
**
** Convert $ROOT to the root URI of the repository.  Allow ' in place of "
** and any case for href or action.
*/
static void convert_href_and_output(Blob *pIn){
  int i, base;
  int n = blob_size(pIn);
  char *z = blob_buffer(pIn);
  for(base=0, i=7; i<n; i++){
    if( z[i]=='$'
     && strncmp(&z[i],"$ROOT/", 6)==0
     && (z[i-1]=='\'' || z[i-1]=='"')
     && i-base>=9
     && (fossil_strnicmp(&z[i-7]," href=", 6)==0 ||
           fossil_strnicmp(&z[i-9]," action=", 8)==0)
    ){
      blob_append(cgi_output_blob(), &z[base], i-base);
      blob_appendf(cgi_output_blob(), "%R");
      base = i+5;
    }
  }
  blob_append(cgi_output_blob(), &z[base], i-base);
}

/*
** WEBPAGE: uv
** WEBPAGE: doc
** URL: /uv/FILE
** URL: /doc/CHECKIN/FILE
**
** CHECKIN can be either tag or hash prefix or timestamp identifying a
** particular check, or the name of a branch (meaning the most recent
** check-in on that branch) or one of various magic words:
**
**     "tip"      means the most recent check-in
**
**     "ckout"    means the current check-out, if the server is run from
**                within a check-out, otherwise it is the same as "tip"
**
** FILE is the name of a file to delivered up as a webpage.  FILE is relative
** to the root of the source tree of the repository. The FILE must
** be a part of CHECKIN, except when CHECKIN=="ckout" when FILE is read
** directly from disk and need not be a managed file.
**
** The "ckout" CHECKIN is intended for development - to provide a mechanism
** for looking at what a file will look like using the /doc webpage after
** it gets checked in.
**
** The file extension is used to decide how to render the file.
**
** If FILE ends in "/" then the names "FILE/index.html", "FILE/index.wiki",
** and "FILE/index.md" are tried in that order.  If the binary was compiled
** with TH1 embedded documentation support and the "th1-docs" setting is
** enabled, the name "FILE/index.th1" is also tried.  If none of those are
** found, then FILE is completely replaced by "404.md" and tried.  If that
** is not found, then a default 404 screen is generated.
**
** If the file's mimetype is "text/x-fossil-wiki" or "text/x-markdown"
** then headers and footers are added. If the document has mimetype
** text/html then headers and footers are usually not added.  However,
** if a "text/html" document begins with the following div:
**
**       <div class='fossil-doc' data-title='TEXT'>
**
** then headers and footers are supplied.  The optional data-title field
** specifies the title of the document in that case.
**
** For fossil-doc documents and for markdown documents, text of the
** form:  "href='$ROOT/" or "action='$ROOT" has the $ROOT name expanded
** to the top-level of the repository.
*/
void doc_page(void){
  const char *zName = 0;            /* Argument to the /doc page */
  const char *zOrigName = "?";      /* Original document name */
  const char *zMime;                /* Document MIME type */
  char *zCheckin = "tip";           /* The check-in holding the document */
  char *zPathSuffix = "";           /* Text to append to g.zPath */
  int vid = 0;                      /* Artifact of check-in */
  int rid = 0;                      /* Artifact of file */
  int i;                            /* Loop counter */
  Blob filebody;                    /* Content of the documentation file */
  Blob title;                       /* Document title */
  int nMiss = (-1);                 /* Failed attempts to find the document */
  int isUV = g.zPath[0]=='u';       /* True for /uv.  False for /doc */
  const char *zDfltTitle;
  static const char *const azSuffix[] = {
     "index.html", "index.wiki", "index.md"
#ifdef FOSSIL_ENABLE_TH1_DOCS
      , "index.th1"
#endif
  };

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  blob_init(&title, 0, 0);
  zDfltTitle = isUV ? "" : "Documentation";
  db_begin_transaction();
  while( rid==0 && (++nMiss)<=count(azSuffix) ){
    zName = P("name");
    if( isUV ){
      if( zName==0 ) zName = "index.wiki";
      i = 0;
    }else{
      if( zName==0 || zName[0]==0 ) zName = "tip/index.wiki";
      for(i=0; zName[i] && zName[i]!='/'; i++){}
      zCheckin = mprintf("%.*s", i, zName);
      if( fossil_strcmp(zCheckin,"ckout")==0 && g.localOpen==0 ){
        zCheckin = "tip";
      }
    }
    if( nMiss==count(azSuffix) ){
      zName = "404.md";
    }else if( zName[i]==0 ){
      assert( nMiss>=0 && nMiss<count(azSuffix) );
      zName = azSuffix[nMiss];
    }else if( !isUV ){
      zName += i;
    }
    while( zName[0]=='/' ){ zName++; }
    if( isUV ){
      zPathSuffix = fossil_strdup(zName);
    }else{
      zPathSuffix = mprintf("%s/%s", zCheckin, zName);
    }
    if( nMiss==0 ) zOrigName = zName;
    if( !file_is_simple_pathname(zName, 1) ){
      if( sqlite3_strglob("*/", zName)==0 ){
        assert( nMiss>=0 && nMiss<count(azSuffix) );
        zName = mprintf("%s%s", zName, azSuffix[nMiss]);
        if( !file_is_simple_pathname(zName, 1) ){
          goto doc_not_found;
        }
      }else{
        goto doc_not_found;
      }
    }
    if( isUV ){
      if( db_table_exists("repository","unversioned")
       && unversioned_content(zName, &filebody)==0
      ){
        rid = 1;
        zDfltTitle = zName;
      }
    }else if( fossil_strcmp(zCheckin,"ckout")==0 ){
      /* Read from the local checkout */
      char *zFullpath;
      db_must_be_within_tree();
      zFullpath = mprintf("%s/%s", g.zLocalRoot, zName);
      if( file_isfile(zFullpath)
       && blob_read_from_file(&filebody, zFullpath)>0 ){
        rid = 1;  /* Fake RID just to get the loop to end */
      }
      fossil_free(zFullpath);
    }else{
      vid = name_to_typed_rid(zCheckin, "ci");
      rid = doc_load_content(vid, zName, &filebody);
    }
  }
  g.zPath = mprintf("%s/%s", g.zPath, zPathSuffix);
  if( rid==0 ) goto doc_not_found;
  blob_to_utf8_no_bom(&filebody, 0);

  /* The file is now contained in the filebody blob.  Deliver the
  ** file to the user
  */
  zMime = nMiss==0 ? P("mimetype") : 0;
  if( zMime==0 ){
    zMime = mimetype_from_name(zName);
  }
  Th_Store("doc_name", zName);
  if( vid ){
    Th_Store("doc_version", db_text(0, "SELECT '[' || substr(uuid,1,10) || ']'"
                                       "  FROM blob WHERE rid=%d", vid));
    Th_Store("doc_date", db_text(0, "SELECT datetime(mtime) FROM event"
                                    " WHERE objid=%d AND type='ci'", vid));
  }
  if( fossil_strcmp(zMime, "text/x-fossil-wiki")==0 ){
    Blob tail;
    style_adunit_config(ADUNIT_RIGHT_OK);
    if( wiki_find_title(&filebody, &title, &tail) ){
      style_header("%s", blob_str(&title));
      wiki_convert(&tail, 0, WIKI_BUTTONS);
    }else{
      style_header("%s", zDfltTitle);
      wiki_convert(&filebody, 0, WIKI_BUTTONS);
    }
    style_footer();
  }else if( fossil_strcmp(zMime, "text/x-markdown")==0 ){
    Blob tail = BLOB_INITIALIZER;
    markdown_to_html(&filebody, &title, &tail);
    if( blob_size(&title)>0 ){
      style_header("%s", blob_str(&title));
    }else{
      style_header("%s", nMiss>=count(azSuffix)?
                        "Not Found" : zDfltTitle);
    }
    convert_href_and_output(&tail);
    style_footer();
  }else if( fossil_strcmp(zMime, "text/plain")==0 ){
    style_header("%s", zDfltTitle);
    @ <blockquote><pre>
    @ %h(blob_str(&filebody))
    @ </pre></blockquote>
    style_footer();
  }else if( fossil_strcmp(zMime, "text/html")==0
            && doc_is_embedded_html(&filebody, &title) ){
    if( blob_size(&title)==0 ) blob_append(&title,zName,-1);
    style_header("%s", blob_str(&title));
    convert_href_and_output(&filebody);
    style_footer();
#ifdef FOSSIL_ENABLE_TH1_DOCS
  }else if( Th_AreDocsEnabled() &&
            fossil_strcmp(zMime, "application/x-th1")==0 ){
    int raw = P("raw")!=0;
    if( !raw ){
      Blob tail;
      blob_zero(&tail);
      if( wiki_find_title(&filebody, &title, &tail) ){
        style_header("%s", blob_str(&title));
        Th_Render(blob_str(&tail));
        blob_reset(&tail);
      }else{
        style_header("%h", zName);
        Th_Render(blob_str(&filebody));
      }
    }else{
      Th_Render(blob_str(&filebody));
    }
    if( !raw ){
      style_footer();
    }
#endif
  }else{
    cgi_set_content_type(zMime);
    cgi_set_content(&filebody);
  }
  if( nMiss>=count(azSuffix) ) cgi_set_status(404, "Not Found");
  db_end_transaction(0);
  return;

  /* Jump here when unable to locate the document */
doc_not_found:
  db_end_transaction(0);
  if( isUV && P("name")==0 ){
    uvlist_page();
    return;
  }
  cgi_set_status(404, "Not Found");
  style_header("Not Found");
  @ <p>Document %h(zOrigName) not found
  if( fossil_strcmp(zCheckin,"ckout")!=0 ){
    @ in %z(href("%R/tree?ci=%T",zCheckin))%h(zCheckin)</a>
  }
  style_footer();
  db_end_transaction(0);
  return;
}

/*
** The default logo.
*/
static const unsigned char aLogo[] = {
    71,  73,  70,  56,  55,  97,  62,   0,  71,   0, 244,   0,   0,  85,
   129, 149,  95, 136, 155,  99, 139, 157, 106, 144, 162, 113, 150, 166,
   116, 152, 168, 127, 160, 175, 138, 168, 182, 148, 176, 188, 159, 184,
   195, 170, 192, 202, 180, 199, 208, 184, 202, 210, 191, 207, 215, 201,
   215, 221, 212, 223, 228, 223, 231, 235, 226, 227, 226, 226, 234, 237,
   233, 239, 241, 240, 244, 246, 244, 247, 248, 255, 255, 255,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  44,   0,   0,
     0,   0,  62,   0,  71,   0,   0,   5, 255,  96, 100, 141, 100, 105,
   158, 168,  37,  41, 132, 192, 164, 112,  44, 207, 102,  99,   0,  56,
    16,  84, 116, 239, 199, 141,  65, 110, 232, 248,  25, 141, 193, 161,
    82, 113, 108, 202,  32,  55, 229, 210,  73,  61,  41, 164,  88, 102,
   181,  10,  41,  96, 179,  91, 106,  35, 240,   5, 135, 143, 137, 242,
    87, 123, 246,  33, 190,  81, 108, 163, 237, 198,  14,  30, 113, 233,
   131,  78, 115,  72,  11, 115,  87, 101,  19, 124,  51,  66,  74,   8,
    19,  16,  67, 100,  74, 133,  50,  15, 101, 135,  56,  11,  74,   6,
   143,  49, 126, 106,  56,   8, 145,  67,   9, 152,  48, 139, 155,   5,
    22,  13,  74, 115, 161,  41, 147, 101,  13, 130,  57, 132, 170,  40,
   167, 155,   0,  94,  57,   3, 178,  48, 183, 181,  57, 160, 186,  40,
    19, 141, 189,   0,  69, 192,  40,  16, 195, 155, 185, 199,  41, 201,
   189, 191, 205, 193, 188, 131, 210,  49, 175,  88, 209, 214,  38,  19,
     3,  11,  19, 111, 127,  60, 219,  39,  55, 204,  19,  11,   6, 100,
     5,  10, 227, 228,  37, 163,   0, 239, 117,  56, 238, 243,  49, 195,
   177, 247,  48, 158,  56, 251,  50, 216, 254, 197,  56, 128, 107, 158,
     2, 125, 171, 114,  92, 218, 246,  96,  66,   3,   4,  50, 134, 176,
   145,   6,  97,  64, 144,  24,  19, 136, 108,  91, 177, 160,   0, 194,
    19, 253,   0, 216, 107, 214, 224, 192, 129,   5,  16,  83, 255, 244,
    43, 213, 195,  24, 159,  27, 169,  64, 230,  88, 208, 227, 129, 182,
    54,   4,  89, 158,  24, 181, 163, 199,   1, 155,  52, 233,   8, 130,
   176,  83,  24, 128, 137,  50,  18,  32,  48,  48, 114,  11, 173, 137,
    19, 110,   4,  64, 105,   1, 194,  30, 140,  68,  15,  24,  24, 224,
    50,  76,  70,   0,  11, 171,  54,  26, 160, 181, 194, 149, 148,  40,
   174, 148, 122,  64, 180, 208, 161,  17, 207, 112, 164,   1, 128,  96,
   148,  78,  18,  21, 194,  33, 229,  51, 247,  65, 133,  97,   5, 250,
    69, 229, 100,  34, 220, 128, 166, 116, 190,  62,   8, 167, 195, 170,
    47, 163,   0, 130,  90, 152,  11, 160, 173, 170,  27, 154,  26,  91,
   232, 151, 171,  18,  14, 162, 253,  98, 170,  18,  70, 171,  64, 219,
    10,  67, 136, 134, 187, 116,  75, 180,  46, 179, 174, 135,   4, 189,
   229, 231,  78,  40,  10,  62, 226, 164, 172,  64, 240, 167, 170,  10,
    18, 124, 188,  10, 107,  65, 193,  94,  11,  93, 171,  28, 248,  17,
   239,  46, 140,  78,  97,  34,  25, 153,  36,  99,  65, 130,   7, 203,
   183, 168,  51,  34, 136,  25, 140,  10,   6,  16,  28, 255, 145, 241,
   230, 140,  10,  66, 178, 167, 112,  48, 192, 128, 129,   9,  31, 141,
    84, 138,  63, 163, 162,   2, 203, 206, 240,  56,  55,  98, 192, 188,
    15, 185,  50, 160,   6,   0, 125,  62,  33, 214, 195,  33,   5,  24,
   184,  25, 231,  14, 201, 245, 144,  23, 126, 104, 228,   0, 145,   2,
    13, 140, 244, 212,  17,  21,  20, 176, 159,  17,  95, 225, 160, 128,
    16,   1,  32, 224, 142,  32, 227, 125,  87,  64,   0,  16,  54, 129,
   205,   2, 141,  76,  53, 130, 103,  37, 166,  64, 144, 107,  78, 196,
     5, 192,   0,  54,  50, 229,   9, 141,  49,  84, 194,  35,  12, 196,
   153,  48, 192, 137,  57,  84,  24,   7,  87, 159, 249, 240, 215, 143,
   105, 241, 118, 149,   9, 139,   4,  64, 203, 141,  35, 140, 129, 131,
    16, 222, 125, 231, 128,   2, 238,  17, 152,  66,   3,   5,  56, 224,
   159, 103,  16,  76,  25,  75,   5,  11, 164, 215,  96,   9,  14,  16,
    36, 225,  15,  11,  40, 144, 192, 156,  41,  10, 178, 199,   3,  66,
    64,  80, 193,   3, 124,  90,  48, 129, 129, 102, 177,  18, 192, 154,
    49,  84, 240, 208,  92,  22, 149,  96,  39,   9,  31,  74,  17,  94,
     3,   8, 177, 199,  72,  59,  85,  76,  25, 216,   8, 139, 194, 197,
   138, 163,  69,  96, 115,   0, 147,  72,  72,  84,  28,  14,  79,  86,
   233, 230,  23, 113,  26, 160, 128,   3,  10,  58, 129, 103,  14, 159,
   214, 163, 146, 117, 238, 213, 154, 128, 151, 109,  84,  64, 217,  13,
    27,  10, 228,  39,   2, 235, 164, 168,  74,   8,   0,  59,
};

/*
** WEBPAGE: logo
**
** Return the logo image.  This image is available to anybody who can see
** the login page.  It is designed for use in the upper left-hand corner
** of the header.
*/
void logo_page(void){
  Blob logo;
  char *zMime;

  zMime = db_get("logo-mimetype", "image/gif");
  blob_zero(&logo);
  db_blob(&logo, "SELECT value FROM config WHERE name='logo-image'");
  if( blob_size(&logo)==0 ){
    blob_init(&logo, (char*)aLogo, sizeof(aLogo));
  }
  cgi_set_content_type(zMime);
  cgi_set_content(&logo);
  g.isConst = 1;
}

/*
** The default background image:  a 16x16 white GIF
*/
static const unsigned char aBackground[] = {
    71,  73,  70,  56,  57,  97,  16,   0,  16,   0,
   240,   0,   0, 255, 255, 255,   0,   0,   0,  33,
   254,   4, 119, 105, 115, 104,   0,  44,   0,   0,
     0,   0,  16,   0,  16,   0,   0,   2,  14, 132,
   143, 169, 203, 237,  15, 163, 156, 180, 218, 139,
   179,  62,   5,   0,  59,
};


/*
** WEBPAGE: background
**
** Return the background image.  If no background image is defined, a
** built-in 16x16 pixel white GIF is returned.
*/
void background_page(void){
  Blob bgimg;
  char *zMime;

  zMime = db_get("background-mimetype", "image/gif");
  blob_zero(&bgimg);
  db_blob(&bgimg, "SELECT value FROM config WHERE name='background-image'");
  if( blob_size(&bgimg)==0 ){
    blob_init(&bgimg, (char*)aBackground, sizeof(aBackground));
  }
  cgi_set_content_type(zMime);
  cgi_set_content(&bgimg);
  g.isConst = 1;
}


/*
** WEBPAGE: docsrch
**
** Search for documents that match a user-supplied full-text search pattern.
** If no pattern is specified (by the s= query parameter) then the user
** is prompted to enter a search string.
**
** Query parameters:
**
**     s=PATTERN             Search for PATTERN
*/
void doc_search_page(void){
  login_check_credentials();
  style_header("Document Search");
  search_screen(SRCH_DOC, 0);
  style_footer();
}
