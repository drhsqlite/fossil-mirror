/*
** This program reads a raw email file and attempts to decode it into
** a more human-readable format.  The following decodings are done:
**
**  (1) Header values are prefixed by "| " at the left margin.
**
**  (2) Content-Transfer-Encoding is recognized and the content is
**      decoded for display.
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define BINARY 0
#define BASE64 1
#define QUOTED 2

static int decode_hex(char c){
  if( c>='0' && c<='9' ) return c - '0';
  if( c>='A' && c<='F' ) return c - 'A' + 10;
  if( c>='a' && c<='f' ) return c - 'a' + 10;
  return -1;
}

static void convert_file(const char *zFilename, FILE *in){
  int inHdr = 1;
  int n;
  int nBoundary;
  int decodeType = 0;
  int textMimetype = 1;
  char *zB;
  char zBoundary[200];
  char zLine[5000];
  char zOut[5000];
  while( fgets(zLine, sizeof(zLine), in) ){
    if( !inHdr
     && zLine[0]=='-'
     && zLine[1]=='-'
     && strncmp(zLine+2,zBoundary,nBoundary)==0
    ){
      printf("|----------------- end of body section ---------|\n");
      inHdr = 1;
    }
    if( !inHdr ){
      if( textMimetype && decodeType==BASE64 ){
        int ii, jj, c, x, y;
        int bits = 0;
        for(ii=jj=0; (c = zLine[ii])!=0; ii++){
          if( c>='A' && c<='Z' ){
            x = c - 'A';
          }else if( c>='a' && c<='z' ){
            x = c - 'a' + 26;
          }else if( c>='0' && c<='9' ){
            x = c - '0' + 52;
          }else if( c=='+' ){
            x = 62;
          }else if( c=='/' ){
            x = 63;
          }else if( c=='=' ){
            x = 0;
          }else{
            continue;
          }
          if( bits==0 ){
            y = x;
            bits = 6;
          }else if( bits==6 ){
            zOut[jj++] = ((y<<2) & 0xfc) | ((x>>4) & 0x03);
            y = x & 0xf;
            bits = 4;
          }else if( bits==4 ){
            zOut[jj++] = ((y<<4) & 0xf0) | ((x>>2) & 0x0f);
            y = x & 0x3;
            bits = 2;
          }else if( bits==2 ){
            zOut[jj++] = ((y<<6) & 0xc0) | (x & 0x3f);
            bits = 0;
          }
        }
        zOut[jj] = 0;
        printf("%s", zOut);
      }else if( textMimetype && decodeType==QUOTED ){
        int ii, jj, c;
        for(ii=jj=0; (c = zLine[ii])!=0; ii++){
          if( c=='=' ){
            int x1 = decode_hex(zLine[ii+1]);
            int x2 = decode_hex(zLine[ii+2]);
            if( x1>=0 && x2>=0 ){
              zOut[jj++] = (x1<<4) | x2;
              ii += 2;
            }else if( zLine[ii+1]=='\r' && zLine[ii+2]=='\n' ){
              ii += 2;
            }
          }else{
            zOut[jj++] = c;
          }
        }
        zOut[jj] = 0;
        printf("%s", zOut);
      }else{
        printf("%s", zLine);
      }
      continue;
    }
    n = (int)strlen(zLine);
    while( n>0 && isspace(zLine[n-1]) ){ n--; }
    zLine[n] = 0;
    if( n==0 ){
      inHdr = 0;
      printf("|----------------- end of header ---------------|\n");
      continue;
    }
    printf("| %s\n", zLine);
    if( strncasecmp(zLine,"Content-Type:", 13)==0 ){
      textMimetype = strstr(zLine, "text/")!=0;
      printf("|** %s content type **|\n",
          textMimetype ? "Text" : "Non-text");
    }
    if( strncasecmp(zLine,"Content-Transfer-Encoding:", 26)==0 ){
      if( strcasestr(zLine, "base64") ){
        decodeType = BASE64;
      }else if( strcasestr(zLine, "quoted-printable") ){
        decodeType = QUOTED;
      }else{
        decodeType = BINARY;
      }
      printf("|** Content encoding %s **|\n",
        decodeType==BASE64 ? "BASE64" :
        decodeType==QUOTED ? "QUOTED" : "BINARY");
    }
    zB = strstr(zLine, "boundary=\"");
    if( zB ){
      int kk;
      zB += 10;
      for(kk=0; zB[kk] && zB[kk]!='"' && kk<sizeof(zBoundary)-1; kk++){
        zBoundary[kk] = zB[kk];
      }
      zBoundary[kk] = 0;
      nBoundary = kk;
      printf("|** boundary [%s] **|\n", zBoundary);
    }
  }
}

int main(int argc, char **argv){
  if( argc==1 ){
    convert_file("<stdin>", stdin);
    return 0;
  }else{
    int i;
    for(i=1; i<argc; i++){
      FILE *in = fopen(argv[i], "rb");
      if( in==0 ){
        fprintf(stderr, "cannot open \"%s\"", argv[i]);
      }else{
        convert_file(argv[i], in);
        fclose(in);
      }
    }
  }
  return 0;
}
