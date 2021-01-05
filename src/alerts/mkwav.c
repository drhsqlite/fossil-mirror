/*
** This C program was used to generate the "g-minor-triad.wav" file.
** A small modification generated the "b-flat.wav" file.
**
** This code is saved as an historical reference.  It is not part
** of Fossil.
*/
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

/*
** Write a four-byte little-endian integer value to out.
*/
void write_int4(FILE *out, unsigned int i){
  unsigned char z[4];
  z[0] = i&0xff;
  z[1] = (i>>8)&0xff;
  z[2] = (i>>16)&0xff;
  z[3] = (i>>24)&0xff;
  fwrite(z, 4, 1, out);
}

/*
** Write out the WAV file
*/
void write_wave(
  const char *zFilename,    /* The file to write */
  unsigned int nData,       /* Bytes of data */
  unsigned char *aData      /* 8000 samples/sec, 8 bit samples */
){
  const unsigned char aWavFmt[] = {
    0x57,  0x41,  0x56, 0x45,    /* "WAVE" */
    0x66,  0x6d,  0x74, 0x20,    /* "fmt " */
    0x10,  0x00,  0x00, 0x00,    /* 16 bytes in the "fmt " section */
    0x01,  0x00,                 /* FormatTag: WAVE_FORMAT_PCM */
    0x01,  0x00,                 /* 1 channel */
    0x40,  0x1f,  0x00, 0x00,    /* 8000 samples/second */
    0x40,  0x1f,  0x00, 0x00,    /* 8000 bytes/second */
    0x01,  0x00,                 /* Block alignment */
    0x08,  0x00,                 /* bits/sample */
    0x64,  0x61,  0x74, 0x61,    /* "data" */
  };
  FILE *out = fopen(zFilename,"wb");
  if( out==0 ){
    fprintf(stderr, "cannot open \"%s\" for writing\n", zFilename);
    exit(1);
  }
  fwrite("RIFF", 4, 1, out);
  write_int4(out, nData+4+20+8);
  fwrite(aWavFmt, sizeof(aWavFmt), 1, out);
  write_int4(out, nData);
  fwrite(aData, nData, 1, out);
  fclose(out);
}

int main(int argc, char **argv){
  int i = 0;
  unsigned char aBuf[800];
# define N      sizeof(aBuf)
# define pitch1 195.9977*2   /* G */
# define pitch2 233.0819*2   /* B-flat */
# define pitch3 293.6648*2   /* D */
  while( i<N/2 ){
    double v;
    v = 99.0*sin((2*M_PI*pitch3*i)/8000);
    if( i<200 ){
      v = v*i/200.0;
    }else if( i>N-200 ){
      v = v*(N-i)/200.0;
    }
    aBuf[i] = (char)(v+99.0);
    i++;
  }
  while( i<N ){
    double v;
    v = 99.0*sin((2*M_PI*pitch1*i)/8000);
    if( i<200 ){
      v = v*i/200.0;
    }else if( i>N-200 ){
      v = v*(N-i)/200.0;
    }
    aBuf[i] = (char)(v+99.0);
    i++;
  }
  write_wave("out.wav", N, aBuf);
  return 0;
}
