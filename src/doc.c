/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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

  static const char isBinary[] = {
     1, 1, 1, 1,  1, 1, 1, 1,    1, 0, 0, 1,  0, 0, 1, 1,
     1, 1, 1, 1,  1, 1, 1, 1,    1, 1, 1, 0,  1, 1, 1, 1,
  };

  /* A table of mimetypes based on file content prefixes
  */
  static const struct {
    const char *zPrefix;       /* The file prefix */
    int size;                  /* Length of the prefix */
    const char *zMimetype;     /* The corresponding mimetype */
  } aMime[] = {
    { "GIF87a",                  6, "image/gif"  },
    { "GIF89a",                  6, "image/gif"  },
    { "\211PNG\r\n\032\r",       8, "image/png"  },
    { "\377\332\377",            3, "image/jpeg" },
  };

  x = (const unsigned char*)blob_buffer(pBlob);
  n = blob_size(pBlob);
  for(i=0; i<n; i++){
    unsigned char c = x[i];
    if( c<=0x1f && isBinary[c] ){
      break;
    }
  }
  if( i>=n ){
    return 0;   /* Plain text */
  }
  for(i=0; i<sizeof(aMime)/sizeof(aMime[0]); i++){
    if( n>=aMime[i].size && memcmp(x, aMime[i].zPrefix, aMime[i].size)==0 ){
      return aMime[i].zMimetype;
    }
  }
  return "unknown/unknown";
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
    { "c",          1, "text/plain"                        },
    { "cc",         2, "text/plain"                        },
    { "ccad",       4, "application/clariscad"             },
    { "cdf",        3, "application/x-netcdf"              },
    { "class",      5, "application/octet-stream"          },
    { "cod",        3, "application/vnd.rim.cod"           },
    { "com",        3, "application/x-msdos-program"       },
    { "cpio",       4, "application/x-cpio"                },
    { "cpt",        3, "application/mac-compactpro"        },
    { "csh",        3, "application/x-csh"                 },
    { "css",        3, "text/css"                          },
    { "dcr",        3, "application/x-director"            },
    { "deb",        3, "application/x-debian-package"      },
    { "dir",        3, "application/x-director"            },
    { "dl",         2, "video/dl"                          },
    { "dms",        3, "application/octet-stream"          },
    { "doc",        3, "application/msword"                },
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
    { "hdf",        3, "application/x-hdf"                 },
    { "hh",         2, "text/plain"                        },
    { "hqx",        3, "application/mac-binhex40"          },
    { "h",          1, "text/plain"                        },
    { "htm",        3, "text/html"                         },
    { "html",       4, "text/html"                         },
    { "ice",        3, "x-conference/x-cooltalk"           },
    { "ief",        3, "image/ief"                         },
    { "iges",       4, "model/iges"                        },
    { "igs",        3, "model/iges"                        },
    { "ips",        3, "application/x-ipscript"            },
    { "ipx",        3, "application/x-ipix"                },
    { "jad",        3, "text/vnd.sun.j2me.app-descriptor"  },
    { "jar",        3, "application/java-archive"          },
    { "jpeg",       4, "image/jpeg"                        },
    { "jpe",        3, "image/jpeg"                        },
    { "jpg",        3, "image/jpeg"                        },
    { "js",         2, "application/x-javascript"          },
    { "kar",        3, "audio/midi"                        },
    { "latex",      5, "application/x-latex"               },
    { "lha",        3, "application/octet-stream"          },
    { "lsp",        3, "application/x-lisp"                },
    { "lzh",        3, "application/octet-stream"          },
    { "m",          1, "text/plain"                        },
    { "m3u",        3, "audio/x-mpegurl"                   },
    { "man",        3, "application/x-troff-man"           },
    { "me",         2, "application/x-troff-me"            },
    { "mesh",       4, "model/mesh"                        },
    { "mid",        3, "audio/midi"                        },
    { "midi",       4, "audio/midi"                        },
    { "mif",        3, "application/x-mif"                 },
    { "mime",       4, "www/mime"                          },
    { "movie",      5, "video/x-sgi-movie"                 },
    { "mov",        3, "video/quicktime"                   },
    { "mp2",        3, "audio/mpeg"                        },
    { "mp2",        3, "video/mpeg"                        },
    { "mp3",        3, "audio/mpeg"                        },
    { "mpeg",       4, "video/mpeg"                        },
    { "mpe",        3, "video/mpeg"                        },
    { "mpga",       4, "audio/mpeg"                        },
    { "mpg",        3, "video/mpeg"                        },
    { "ms",         2, "application/x-troff-ms"            },
    { "msh",        3, "model/mesh"                        },
    { "nc",         2, "application/x-netcdf"              },
    { "oda",        3, "application/oda"                   },
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
    { "ppm",        3, "image/x-portable-pixmap"           },
    { "pps",        3, "application/mspowerpoint"          },
    { "ppt",        3, "application/mspowerpoint"          },
    { "ppz",        3, "application/mspowerpoint"          },
    { "pre",        3, "application/x-freelance"           },
    { "prt",        3, "application/pro_eng"               },
    { "ps",         2, "application/postscript"            },
    { "qt",         2, "video/quicktime"                   },
    { "ra",         2, "audio/x-realaudio"                 },
    { "ram",        3, "audio/x-pn-realaudio"              },
    { "rar",        3, "application/x-rar-compressed"      },
    { "ras",        3, "image/cmu-raster"                  },
    { "ras",        3, "image/x-cmu-raster"                },
    { "rgb",        3, "image/x-rgb"                       },
    { "rm",         2, "audio/x-pn-realaudio"              },
    { "roff",       4, "application/x-troff"               },
    { "rpm",        3, "audio/x-pn-realaudio-plugin"       },
    { "rtf",        3, "application/rtf"                   },
    { "rtf",        3, "text/rtf"                          },
    { "rtx",        3, "text/richtext"                     },
    { "scm",        3, "application/x-lotusscreencam"      },
    { "set",        3, "application/set"                   },
    { "sgml",       4, "text/sgml"                         },
    { "sgm",        3, "text/sgml"                         },
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
    { "swf",        3, "application/x-shockwave-flash"     },
    { "t",          1, "application/x-troff"               },
    { "tar",        3, "application/x-tar"                 },
    { "tcl",        3, "application/x-tcl"                 },
    { "tex",        3, "application/x-tex"                 },
    { "texi",       4, "application/x-texinfo"             },
    { "texinfo",    7, "application/x-texinfo"             },
    { "tgz",        3, "application/x-tar-gz"              },
    { "tiff",       4, "image/tiff"                        },
    { "tif",        3, "image/tiff"                        },
    { "tr",         2, "application/x-troff"               },
    { "tsi",        3, "audio/TSP-audio"                   },
    { "tsp",        3, "application/dsptype"               },
    { "tsv",        3, "text/tab-separated-values"         },
    { "txt",        3, "text/plain"                        },
    { "unv",        3, "application/i-deas"                },
    { "ustar",      5, "application/x-ustar"               },
    { "vcd",        3, "application/x-cdlink"              },
    { "vda",        3, "application/vda"                   },
    { "viv",        3, "video/vnd.vivo"                    },
    { "vivo",       4, "video/vnd.vivo"                    },
    { "vrml",       4, "model/vrml"                        },
    { "wav",        3, "audio/x-wav"                       },
    { "wax",        3, "audio/x-ms-wax"                    },
    { "wiki",       4, "application/x-fossil-wiki"         },
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
    { "xlw",        3, "application/vnd.ms-excel"          },
    { "xml",        3, "text/xml"                          },
    { "xpm",        3, "image/x-xpixmap"                   },
    { "xwd",        3, "image/x-xwindowdump"               },
    { "xyz",        3, "chemical/x-pdb"                    },
    { "zip",        3, "application/zip"                   },
  };

  z = zName;
  for(i=0; zName[i]; i++){
    if( zName[i]=='.' ) z = &zName[i+1];
  }
  len = strlen(z);
  if( len<sizeof(zSuffix)-1 ){
    strcpy(zSuffix, z);
    for(i=0; zSuffix[i]; i++) zSuffix[i] = tolower(zSuffix[i]);
    first = 0;
    last = sizeof(aMime)/sizeof(aMime[0]);
    while( first<=last ){
      int c;
      i = (first+last)/2;
      c = strcmp(zSuffix, aMime[i].zSuffix);
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
** WEBPAGE: doc
** URL: /doc?name=BASELINE/PATH
**
** BASELINE can be either a baseline uuid prefix or magic words "tip"
** to me the most recently checked in baseline or "ckout" to mean the
** content of the local checkout, if any.  PATH is the relative pathname
** of some file.  This method returns the file content.
**
** If PATH matches the patterns *.wiki or *.txt then formatting content
** is added before returning the file.  For all other names, the content
** is returned straight without any interpretation or processing.
*/
void doc_page(void){
  const char *zName;                /* Argument to the /doc page */
  const char *zMime;                /* Document MIME type */
  int vid = 0;                      /* Artifact of baseline */
  int rid = 0;                      /* Artifact of file */
  int i;                            /* Loop counter */
  Blob filebody;                    /* Content of the documentation file */
  char zBaseline[UUID_SIZE+1];      /* Baseline UUID */

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  zName = PD("name", "tip/index.wiki");
  for(i=0; zName[i] && zName[i]!='/'; i++){}
  if( zName[i]==0 || i>UUID_SIZE ){
    goto doc_not_found;
  }
  memcpy(zBaseline, zName, i);
  zBaseline[i] = 0;
  zName += i;
  while( zName[0]=='/' ){ zName++; }
  if( !file_is_simple_pathname(zName) ){
    goto doc_not_found;
  }
  if( strcmp(zBaseline,"ckout")==0 && db_open_local()==0 ){
    strcpy(zBaseline,"tip");
  }
  if( strcmp(zBaseline,"ckout")==0 ){
    /* Read from the local checkout */
    char *zFullpath;
    db_must_be_within_tree();
    zFullpath = mprintf("%s/%s", g.zLocalRoot, zName);
    if( !file_isfile(zFullpath) ){
      goto doc_not_found;
    }
    if( blob_read_from_file(&filebody, zFullpath)<0 ){
      goto doc_not_found;
    }
  }else{
    db_begin_transaction();
    if( strcmp(zBaseline,"tip")==0 ){
      vid = db_int(0, "SELECT objid FROM event WHERE type='ci'"
                      " ORDER BY mtime DESC LIMIT 1");
    }else{
      vid = name_to_rid(zBaseline);
    }

    /* Create the baseline cache if it does not already exist */
    db_multi_exec(
      "CREATE TABLE IF NOT EXISTS vcache(\n"
      "  vid INTEGER,         -- baseline ID\n"
      "  fname TEXT,          -- filename\n"
      "  rid INTEGER,         -- artifact ID\n"
      "  UNIQUE(vid,fname,rid)\n"
      ")"
    );

    /* Check to see if the documentation file artifact ID is contained
    ** in the baseline cache */
    rid = db_int(0, "SELECT rid FROM vcache"
                    " WHERE vid=%d AND fname=%Q", vid, zName);
    if( rid==0 && db_exists("SELECT 1 FROM vcache WHERE vid=%d", vid) ){
      goto doc_not_found;
    }

    if( rid==0 ){
      Stmt s;
      Blob baseline;
      Manifest m;

      /* Add the vid baseline to the cache */
      if( db_int(0, "SELECT count(*) FROM vcache")>10000 ){
        db_multi_exec("DELETE FROM vcache");
      }
      if( content_get(vid, &baseline)==0 ){
        goto doc_not_found;
      }
      if( manifest_parse(&m, &baseline)==0 || m.type!=CFTYPE_MANIFEST ){
        goto doc_not_found;
      }
      db_prepare(&s,
        "INSERT INTO vcache(vid,fname,rid)"
        " SELECT %d, :fname, rid FROM blob"
        "  WHERE uuid=:uuid",
        vid
      );
      for(i=0; i<m.nFile; i++){
        db_bind_text(&s, ":fname", m.aFile[i].zName);
        db_bind_text(&s, ":uuid", m.aFile[i].zUuid);
        db_step(&s);
        db_reset(&s);
      }
      db_finalize(&s);
      manifest_clear(&m);

      /* Try again to find the file */
      rid = db_int(0, "SELECT rid FROM vcache"
                      " WHERE vid=%d AND fname=%Q", vid, zName);
    }
    if( rid==0 ){
      goto doc_not_found;
    }

    /* Get the file content */
    if( content_get(rid, &filebody)==0 ){
      goto doc_not_found;
    }
    db_end_transaction(0);
  }

  /* The file is now contained in the filebody blob.  Deliver the
  ** file to the user 
  */
  zMime = mimetype_from_name(zName);
  if( strcmp(zMime, "application/x-fossil-wiki")==0 ){
    Blob title, tail;
    if( wiki_find_title(&filebody, &title, &tail) ){
      style_header(blob_str(&title));
      wiki_convert(&tail, 0, 0);
    }else{
      style_header("Documentation");
      wiki_convert(&filebody, 0, 0);
    }
    style_footer();
  }else if( strcmp(zMime, "text/plain")==0 ){
    style_header("Documentation");
    @ <blockquote><pre>
    @ %h(blob_str(&filebody))
    @ </pre></blockquote>
    style_footer();
  }else{
    cgi_set_content_type(zMime);
    cgi_set_content(&filebody);
  }
  return;

doc_not_found:
  /* Jump here when unable to locate the document */
  db_end_transaction(0);
  style_header("Document Not Found");
  @ <p>No such document: %h(PD("name","tip/index.wiki"))</p>
  style_footer();
  return;  
}
