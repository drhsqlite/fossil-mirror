/*
** Test MSVC C89 compatible implementations of the following C99 functions:
** - double rint( double x )
**   Rounds a floating-point value to the nearest integer in floating-point
**   format.
** - int snprintf( char *buffer, size_t count, const char *format, ... )
**   Writes formatted data to a string.
**
** NOTE: These implementations aim to provide the main functionality, not
** the exact behavior as specified in C99 standard.
**
** BUILD: cl test-msc98-rint-snprintf.c
**        gcc test-msc98-rint-snprintf.c -lm ## for reference vs. non-MSVC
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#define TEST_MSC89  1

#if defined(_MSC_VER)
#if (defined(TEST_MSC89) || (_MSC_VER < 1900)) /* before MSVC 2015 */
#include <stdarg.h>

/* NOTE: On truncation, this version of snprintf returns the input count, not
** the expected number of chars to fully output the requested format (as
** done in the C99 standard implementation). However the truncation test should
** still be applicable (nret >= count).
*/
static __forceinline
int c89_snprintf(char *buf, size_t count, const char *fmt, ...){
  va_list argptr;
  int n;
  if( count==0 ) return 0;
  va_start(argptr, fmt);
  n = _vsprintf_p(buf, count, fmt, argptr);
  va_end(argptr);

  /* force zero-termination to avoid some known MSVC bugs */
  if( count>0 ){
    buf[count - 1] = '\0';
    if( n<0 ) n = count;
  }
  return n;
}

#if defined(_WIN64)
#include <emmintrin.h>
#include <limits.h>

static __forceinline
double c89_rint(double v){
  return ( v<0.0 && v>=-0.5 ? -0.0
           : ( v!=0 && v>LLONG_MIN && v<LLONG_MAX
               ? _mm_cvtsd_si64(_mm_load_sd(&v)) : v ) );  /* SSE2 */
}
#else
static __forceinline
double c89_rint(double v){
  double rn;
  __asm
  {
    FLD      v
    FRNDINT
    FSTP     rn
    FWAIT
  };
  return rn;
}
#endif    /* _WIN64 */
#endif    /* (defined(TEST_MSC89) || (_MSC_VER < 1900)) */

#if (_MSC_VER < 1900) /* before MSVC 2015 */
#  define snprintf c89_snprintf
#  define rint c89_rint
#else
#  define HAVE_C99_RINT  1
#endif

#elif !defined(_MSC_VER)
#  define HAVE_C99_RINT  1
#  define c89_snprintf snprintf
#  define c89_rint rint
#endif    /* defined(_MSC_VER) */


#include <assert.h>

#define SNPRINTF c89_snprintf
#define RINT c89_rint

int test_rint()
{
   const char *TESTNAME = "rint";
   const struct test_data {
     double v, expected;
   } data[] = {  /* round to the nearest or even integer */
#ifdef HAVE_C99_RINT
     {INFINITY,INFINITY},
#endif
     {(double)(LLONG_MAX/10000LL) + 0.7,(double)(LLONG_MAX/10000LL) + 1.},
     {5.5,6.},{5.4,5.},{5.2,5.},{5.,5.},
     {4.9,5.},{4.5,4.},{4.4,4.},{4.0,4.},
     {3.7,4.},{3.5,4.},{3.2,3.},{3.0,3.},
     {2.7,3.},{2.5,2.},{2.2,2.},{2.0,2.},
     {1.6,2.},{1.5,2.0},{1.3,1.0},{1.0,1.0},
     {0.9,1.},{0.8,1.},{0.5,0.},{0.49999999999999994,0.},
     {0.4,0.},{0.1,0.},{0.,0.}
   };
   const size_t ndata = sizeof(data)/sizeof(data[0]);
   int nfailed = 0;
   int start = 0, end = ndata;
   int i;
   int done = 0;

   /* do two passes over the test data (positives, negatives) */
   do {
     double sign = ( start<end ? 1. : -1. );
     for(i=start; ( start< end ? i<end : i>end ); ( start< end ? ++i : --i )){
        int passed = 0;
        double v = sign*data[i].v;
        double rn = c89_rint(v);
        double expected = sign*data[i].expected;
#ifdef HAVE_C99_RINT
        {
           double rint_expected = rint(v);
           int matched = ( expected==rint_expected );
           if( !matched ){
             fprintf(stderr, "E:%s|Expected test data[%d]={%.17lf,%.1lf} does not match the actual rint() value=%.1lf\n",__FUNCTION__,
               i, v, expected, rint_expected);
           }
           assert(matched);
           expected = rint_expected;
        }
#endif
        passed = ( rn==expected );
        fprintf(( passed ? stdout : stderr ),
          "T:%s|c89_rint(%.17lf)=%.1lf expected=%.1lf\t[%s]\n", TESTNAME,
          v, rn, expected,
          ( passed ? "PASS" : "FAIL" ));
        if( !passed ) ++nfailed;
     }

     if( start<end ){
       int swap = start;
       start = end - 1; end = swap - 1;
     }else{
       done = 1;
     }
   }while( !done );

   if( nfailed ){
     fprintf(stderr,"T:%s|FAILED %d test\n\n", TESTNAME, nfailed);
   }else{
     printf("T:%s|PASSED\n\n", TESTNAME);
   }

   return nfailed;
}

int test_snprintf()
{
   const char *TESTNAME = "snprintf";
   int nfailed = 0;
#define TEST_BUF_MAXSIZE  256
   const struct test_data {
     size_t bufsize;
     const char *fmt, *expected, *full;
   } data[] = {
     {TEST_BUF_MAXSIZE,"snprintf(buf, %d)","snprintf(buf, 17)","snprintf(buf, 17)"},
     {17,"snprintf(buf, %d)","snprintf(buf, 17","snprintf(buf, 17)"},
     {2,"snprintf(buf, %d)","s","snprintf(buf, 17)"},
     {0,"snprintf(buf, %d)","","snprintf(buf, 17)"},
   };
   const size_t ndata = sizeof(data)/sizeof(data[0]);
   char buf[TEST_BUF_MAXSIZE] = {0};
   int i;

   for(i=0; i<ndata; ++i){
      int passed = 0;
      size_t count = data[i].bufsize;
      const char *fmt = data[i].fmt;
      const char *full = data[i].full;
      const char *expected = data[i].expected;
      const int truncate_expected = ( count<=strlen(full) );
      const int expected_nret = ( !truncate_expected
                                  ? strlen(expected) : count );
      const int expected_zero_at = ( !truncate_expected
                                     ? expected_nret : expected_nret - 1 );
      int nret;
      buf[( count>0 ? count - 1 : 0 )] = '\0';
      nret = c89_snprintf(buf, count, fmt, strlen(fmt));
#ifdef HAVE_C99_RINT
      {
         char snprintf_expected[TEST_BUF_MAXSIZE] = {0};
         int snprintf_expected_nret = snprintf(snprintf_expected, count, fmt, strlen(fmt));
         int matched = ( strcmp(expected, snprintf_expected)==0 );
         int matched_nret = ( expected_nret==snprintf_expected_nret );
         if( !matched ){
           fprintf(stderr, "E:%s|Expected test data[%d]={'%s','%s'} does not match the actual snprintf() value='%s'\n",__FUNCTION__,
             i, buf, expected, snprintf_expected);
         }

         /* NOTE: This implementation of c89_snprintf() on truncation returns
         ** the input count, not the expected number of chars needed to fully
         ** output the requested format. So warn, only ifthe expected nret is
         ** less than the count.
         */
         if( !matched_nret && expected_nret<count ){
           fprintf(stderr, "W:%s|Expected return value=%d for test data[%d]={'%s','%s'} does not match the actual snprintf() return value=%d\n",__FUNCTION__,
             expected_nret, i, buf, expected, snprintf_expected_nret);
         }
         assert(matched);
         expected = snprintf_expected;
      }
#endif
      passed = ( nret==expected_nret );
      fprintf(( passed ? stdout : stderr ),
        "T:%s|c89_snprintf(%lu,\"%s\", %d) nret=%d expected=%d\t[%s]\n", TESTNAME,
        (unsigned long)count, fmt, (int)strlen(fmt), nret, expected_nret,
        ( passed ? "PASS" : "FAIL" ));
      if( !passed ) ++nfailed;

      if( count ){
        passed = ( buf[expected_zero_at]=='\0' );
        fprintf(( passed ? stdout : stderr ),
          "T:%s|c89_snprintf(%lu,\"%s\", %d) s[%d]=%d expected=%d\t[%s]\n", TESTNAME,
          (unsigned long)count, fmt, (int)strlen(fmt), expected_zero_at, buf[expected_zero_at],'\0',
          ( passed ? "PASS" : "FAIL" ));
        if( !passed ) ++nfailed;
      }

      passed = ( strcmp(buf, expected)==0 );
      fprintf((passed ? stdout : stderr),
        "T:%s|c89_snprintf(%lu,\"%s\", %d)=\"%s\" expected=\"%s\"\t[%s]\n", TESTNAME,
        (unsigned long)count, fmt, (int)strlen(fmt), buf, expected,
        ( passed ? "PASS" : "FAIL" ));
      if( !passed ) ++nfailed;
   }

   if( nfailed ){
     fprintf(stderr,"T:%s|FAILED %d tests\n\n", TESTNAME, nfailed);
   }else{
     printf("T:%s|PASSED\n\n", TESTNAME);
   }
   return nfailed;
}

int main(int argc, char *argv[])
{
  int iarg;
  struct testrun {
    char rint, snprintf;
    int status;
  } testrun = {0, 0, 0};

  printf("Usage: %s [TEST]...\n", argv[0]);
  printf("Test MSVC C89 compatible implementations of selected C99 functions.\n"
         "Run the selected TEST, by default 'all'; optionally exclude tests from the run.\n");
  printf("Example: %s all -snprintf\n", argv[0]);
  printf("\nTests:\n"
         "  all, rint, snprintf\n"
         "\n  -TEST\t\texclude the test from the run\n"
         "\n"
  );
  if( argc>1
      && ( strcmp(argv[1], "/?")==0 || strcmp(argv[1], "/h")==0
          || strcmp(argv[1], "/H")==0 || strcmp(argv[1], "--help")==0 ) ){
    return 0;
  }

  testrun.rint = testrun.snprintf = ( argc==1 );
  for(iarg=1; iarg<argc; ++iarg){
    const char *name = argv[iarg];
    char runit = 1;
    if( argv[iarg][0]=='-' ){
       runit = 0;
       name = &(argv[iarg][1]);
    }

    if( strcmp(name, "all")==0 ){
      testrun.rint = testrun.snprintf = runit;
    }else if( strcmp(name, "rint")==0 ){
      testrun.rint = runit;
    }else if( strcmp(name, "snprintf")==0 ){
      testrun.snprintf = runit;
    }else{
      testrun.status = 2;
      fprintf(stderr, "\nE|Invalid test requested: '%s'\n", name);
    }
  }

  if( testrun.status==2 ){
    return testrun.status;
  }

#ifndef _MSC_VER
  fprintf(stderr, "W|Non-MSVC mode: testing against the native implementations\n\n");
#endif

  if( testrun.rint )
    testrun.status |= test_rint();

  if( testrun.snprintf )
    testrun.status |= test_snprintf();

  if( testrun.status==0 )
    printf("\nI|All selected tests completed successfully\n");
  else
    fprintf(stderr, "\nE|Some of the selected tests failed\n");
  return testrun.status;
}

