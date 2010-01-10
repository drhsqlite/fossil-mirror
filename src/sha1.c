/*
** This implementation of SHA1 is adapted from the example implementation
** contained in RFC-3174.
*/
#include <stdint.h>
#include <sys/types.h>
#include "config.h"
#include "sha1.h"

/*
 * If you do not have the ISO standard stdint.h header file, then you
 * must typdef the following:
 *    name              meaning
 *  uint32_t         unsigned 32 bit integer
 *  uint8_t          unsigned 8 bit integer (i.e., unsigned char)
 *
 */
#define SHA1HashSize 20
#define shaSuccess 0
#define shaInputTooLong 1
#define shaStateError 2

/*
 *  This structure will hold context information for the SHA-1
 *  hashing operation
 */
typedef struct SHA1Context SHA1Context;
struct SHA1Context {
    uint32_t Intermediate_Hash[SHA1HashSize/4]; /* Message Digest  */

    uint32_t Length_Low;            /* Message length in bits      */
    uint32_t Length_High;           /* Message length in bits      */

    int Message_Block_Index;   /* Index into message block array   */
    uint8_t Message_Block[64];      /* 512-bit message blocks      */

    int Computed;               /* Is the digest computed?         */
    int Corrupted;             /* Is the message digest corrupted? */
};

/*
 *  sha1.c
 *
 *  Description:
 *      This file implements the Secure Hashing Algorithm 1 as
 *      defined in FIPS PUB 180-1 published April 17, 1995.
 *
 *      The SHA-1, produces a 160-bit message digest for a given
 *      data stream.  It should take about 2**n steps to find a
 *      message with the same digest as a given message and
 *      2**(n/2) to find any two messages with the same digest,
 *      when n is the digest size in bits.  Therefore, this
 *      algorithm can serve as a means of providing a
 *      "fingerprint" for a message.
 *
 *  Portability Issues:
 *      SHA-1 is defined in terms of 32-bit "words".  This code
 *      uses <stdint.h> (included via "sha1.h" to define 32 and 8
 *      bit unsigned integer types.  If your C compiler does not
 *      support 32 bit unsigned integers, this code is not
 *      appropriate.
 *
 *  Caveats:
 *      SHA-1 is designed to work with messages less than 2^64 bits
 *      long.  Although SHA-1 allows a message digest to be generated
 *      for messages of any number of bits less than 2^64, this
 *      implementation only works with messages with a length that is
 *      a multiple of the size of an 8-bit character.
 *
 */

/*
 *  Define the SHA1 circular left shift macro
 */
#define SHA1CircularShift(bits,word) \
                (((word) << (bits)) | ((word) >> (32-(bits))))

/* Local Function Prototyptes */
static void SHA1PadMessage(SHA1Context *);
static void SHA1ProcessMessageBlock(SHA1Context *);

/*
 *  SHA1Reset
 *
 *  Description:
 *      This function will initialize the SHA1Context in preparation
 *      for computing a new SHA1 message digest.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to reset.
 *
 *  Returns:
 *      sha Error Code.
 *
 */
static int SHA1Reset(SHA1Context *context)
{
    context->Length_Low             = 0;
    context->Length_High            = 0;
    context->Message_Block_Index    = 0;

    context->Intermediate_Hash[0]   = 0x67452301;
    context->Intermediate_Hash[1]   = 0xEFCDAB89;
    context->Intermediate_Hash[2]   = 0x98BADCFE;
    context->Intermediate_Hash[3]   = 0x10325476;
    context->Intermediate_Hash[4]   = 0xC3D2E1F0;

    context->Computed   = 0;
    context->Corrupted  = 0;

    return shaSuccess;
}

/*
 *  SHA1Result
 *
 *  Description:
 *      This function will return the 160-bit message digest into the
 *      Message_Digest array  provided by the caller.
 *      NOTE: The first octet of hash is stored in the 0th element,
 *            the last octet of hash in the 19th element.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to use to calculate the SHA-1 hash.
 *      Message_Digest: [out]
 *          Where the digest is returned.
 *
 *  Returns:
 *      sha Error Code.
 *
 */
static int SHA1Result( SHA1Context *context,
                uint8_t Message_Digest[SHA1HashSize])
{
    int i;

    if (context->Corrupted)
    {
        return context->Corrupted;
    }

    if (!context->Computed)
    {
        SHA1PadMessage(context);
        for(i=0; i<64; ++i)
        {
            /* message may be sensitive, clear it out */
            context->Message_Block[i] = 0;
        }
        context->Length_Low = 0;    /* and clear length */
        context->Length_High = 0;
        context->Computed = 1;

    }

    for(i = 0; i < SHA1HashSize; ++i)
    {
        Message_Digest[i] = context->Intermediate_Hash[i>>2]
                            >> 8 * ( 3 - ( i & 0x03 ) );
    }

    return shaSuccess;
}

/*
 *  SHA1Input
 *
 *  Description:
 *      This function accepts an array of octets as the next portion
 *      of the message.
 *
 *  Parameters:
 *      context: [in/out]
 *          The SHA context to update
 *      message_array: [in]
 *          An array of characters representing the next portion of
 *          the message.
 *      length: [in]
 *          The length of the message in message_array
 *
 *  Returns:
 *      sha Error Code.
 *
 */
static
int SHA1Input(    SHA1Context    *context,
                  const uint8_t  *message_array,
                  unsigned       length)
{
    if (!length)
    {
        return shaSuccess;
    }

    if (context->Computed)
    {
        context->Corrupted = shaStateError;

        return shaStateError;
    }

    if (context->Corrupted)
    {
         return context->Corrupted;
    }
    while(length-- && !context->Corrupted)
    {
    context->Message_Block[context->Message_Block_Index++] =
                    (*message_array & 0xFF);

    context->Length_Low += 8;
    if (context->Length_Low == 0)
    {
        context->Length_High++;
        if (context->Length_High == 0)
        {
            /* Message is too long */
            context->Corrupted = 1;
        }
    }

    if (context->Message_Block_Index == 64)
    {
        SHA1ProcessMessageBlock(context);
    }

    message_array++;
    }

    return shaSuccess;
}

/*
 *  SHA1ProcessMessageBlock
 *
 *  Description:
 *      This function will process the next 512 bits of the message
 *      stored in the Message_Block array.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      Many of the variable names in this code, especially the
 *      single character names, were used because those were the
 *      names used in the publication.
 *
 *
 */
static void SHA1ProcessMessageBlock(SHA1Context *context)
{
    const uint32_t K[] =    {       /* Constants defined in SHA-1   */
                            0x5A827999,
                            0x6ED9EBA1,
                            0x8F1BBCDC,
                            0xCA62C1D6
                            };
    int           t;                 /* Loop counter                */
    uint32_t      temp;              /* Temporary word value        */
    uint32_t      W[80];             /* Word sequence               */
    uint32_t      A, B, C, D, E;     /* Word buffers                */

    /*
     *  Initialize the first 16 words in the array W
     */
    for(t = 0; t < 16; t++)
    {
        W[t] = context->Message_Block[t * 4] << 24;
        W[t] |= context->Message_Block[t * 4 + 1] << 16;
        W[t] |= context->Message_Block[t * 4 + 2] << 8;
        W[t] |= context->Message_Block[t * 4 + 3];
    }

    for(t = 16; t < 80; t++)
    {
       W[t] = SHA1CircularShift(1,W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]);
    }

    A = context->Intermediate_Hash[0];
    B = context->Intermediate_Hash[1];
    C = context->Intermediate_Hash[2];
    D = context->Intermediate_Hash[3];
    E = context->Intermediate_Hash[4];

    for(t = 0; t < 20; t++)
    {
        temp =  SHA1CircularShift(5,A) +
                ((B & C) | ((~B) & D)) + E + W[t] + K[0];
        E = D;
        D = C;
        C = SHA1CircularShift(30,B);

        B = A;
        A = temp;
    }

    for(t = 20; t < 40; t++)
    {
        temp = SHA1CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[1];
        E = D;
        D = C;
        C = SHA1CircularShift(30,B);
        B = A;
        A = temp;
    }

    for(t = 40; t < 60; t++)
    {
        temp = SHA1CircularShift(5,A) +
               ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
        E = D;
        D = C;
        C = SHA1CircularShift(30,B);
        B = A;
        A = temp;
    }

    for(t = 60; t < 80; t++)
    {
        temp = SHA1CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[3];
        E = D;
        D = C;
        C = SHA1CircularShift(30,B);
        B = A;
        A = temp;
    }

    context->Intermediate_Hash[0] += A;
    context->Intermediate_Hash[1] += B;
    context->Intermediate_Hash[2] += C;
    context->Intermediate_Hash[3] += D;
    context->Intermediate_Hash[4] += E;

    context->Message_Block_Index = 0;
}

/*
 *  SHA1PadMessage
 *

 *  Description:
 *      According to the standard, the message must be padded to an even
 *      512 bits.  The first padding bit must be a '1'.  The last 64
 *      bits represent the length of the original message.  All bits in
 *      between should be 0.  This function will pad the message
 *      according to those rules by filling the Message_Block array
 *      accordingly.  It will also call the ProcessMessageBlock function
 *      provided appropriately.  When it returns, it can be assumed that
 *      the message digest has been computed.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to pad
 *      ProcessMessageBlock: [in]
 *          The appropriate SHA*ProcessMessageBlock function
 *  Returns:
 *      Nothing.
 *
 */
static void SHA1PadMessage(SHA1Context *context)
{
    /*
     *  Check to see if the current message block is too small to hold
     *  the initial padding bits and length.  If so, we will pad the
     *  block, process it, and then continue padding into a second
     *  block.
     */
    if (context->Message_Block_Index > 55)
    {
        context->Message_Block[context->Message_Block_Index++] = 0x80;
        while(context->Message_Block_Index < 64)
        {
            context->Message_Block[context->Message_Block_Index++] = 0;
        }

        SHA1ProcessMessageBlock(context);

        while(context->Message_Block_Index < 56)
        {
            context->Message_Block[context->Message_Block_Index++] = 0;
        }
    }
    else
    {
        context->Message_Block[context->Message_Block_Index++] = 0x80;
        while(context->Message_Block_Index < 56)
        {

            context->Message_Block[context->Message_Block_Index++] = 0;
        }
    }

    /*
     *  Store the message length as the last 8 octets
     */
    context->Message_Block[56] = context->Length_High >> 24;
    context->Message_Block[57] = context->Length_High >> 16;
    context->Message_Block[58] = context->Length_High >> 8;
    context->Message_Block[59] = context->Length_High;
    context->Message_Block[60] = context->Length_Low >> 24;
    context->Message_Block[61] = context->Length_Low >> 16;
    context->Message_Block[62] = context->Length_Low >> 8;
    context->Message_Block[63] = context->Length_Low;

    SHA1ProcessMessageBlock(context);
}


/*
** Convert a digest into base-16.  digest should be declared as
** "unsigned char digest[20]" in the calling function.  The SHA1
** digest is stored in the first 20 bytes.  zBuf should
** be "char zBuf[41]".
*/
static void DigestToBase16(unsigned char *digest, char *zBuf){
  static char const zEncode[] = "0123456789abcdef";
  int i, j;

  for(j=i=0; i<20; i++){
    int a = digest[i];
    zBuf[j++] = zEncode[(a>>4)&0xf];
    zBuf[j++] = zEncode[a & 0xf];
  }
  zBuf[j] = 0;
}

/*
** The state of a incremental SHA1 checksum computation.  Only one
** such computation can be underway at a time, of course.
*/
static SHA1Context incrCtx;
static int incrInit = 0;

/*
** Add more text to the incremental SHA1 checksum.
*/
void sha1sum_step_text(const char *zText, int nBytes){
  if( !incrInit ){
    SHA1Reset(&incrCtx);
    incrInit = 1;
  }
  if( nBytes<=0 ){
    if( nBytes==0 ) return;
    nBytes = strlen(zText);
  }
  SHA1Input(&incrCtx, (unsigned char*)zText, nBytes);
}

/*
** Add the content of a blob to the incremental SHA1 checksum.
*/
void sha1sum_step_blob(Blob *p){
  sha1sum_step_text(blob_buffer(p), blob_size(p));
}

/*
** Finish the incremental SHA1 checksum.  Store the result in blob pOut
** if pOut!=0.  Also return a pointer to the result.  
**
** This resets the incremental checksum preparing for the next round
** of computation.  The return pointer points to a static buffer that
** is overwritten by subsequent calls to this function.
*/
char *sha1sum_finish(Blob *pOut){
  unsigned char zResult[20];
  static char zOut[41];
  sha1sum_step_text(0,0);
  SHA1Result(&incrCtx, zResult);
  incrInit = 0;
  DigestToBase16(zResult, zOut);
  if( pOut ){
    blob_zero(pOut);
    blob_append(pOut, zOut, 40);
  }
  return zOut;
}


/*
** Compute the SHA1 checksum of a file on disk.  Store the resulting
** checksum in the blob pCksum.  pCksum is assumed to be ininitialized.
**
** Return the number of errors.
*/
int sha1sum_file(const char *zFilename, Blob *pCksum){
  FILE *in;
  SHA1Context ctx;
  unsigned char zResult[20];
  char zBuf[10240];

  in = fopen(zFilename,"rb");
  if( in==0 ){
    return 1;
  }
  SHA1Reset(&ctx);
  for(;;){
    int n;
    n = fread(zBuf, 1, sizeof(zBuf), in);
    if( n<=0 ) break;
    SHA1Input(&ctx, (unsigned char*)zBuf, (unsigned)n);
  }
  fclose(in);
  blob_zero(pCksum);
  blob_resize(pCksum, 40);
  SHA1Result(&ctx, zResult);
  DigestToBase16(zResult, blob_buffer(pCksum));
  return 0;
}

/*
** Compute the SHA1 checksum of a blob in memory.  Store the resulting
** checksum in the blob pCksum.  pCksum is assumed to be either
** uninitialized or the same blob as pIn.
**
** Return the number of errors.
*/
int sha1sum_blob(const Blob *pIn, Blob *pCksum){
  SHA1Context ctx;
  unsigned char zResult[20];

  SHA1Reset(&ctx);
  SHA1Input(&ctx, (unsigned char*)blob_buffer(pIn), blob_size(pIn));
  if( pIn==pCksum ){
    blob_reset(pCksum);
  }else{
    blob_zero(pCksum);
  }
  blob_resize(pCksum, 40);
  SHA1Result(&ctx, zResult);
  DigestToBase16(zResult, blob_buffer(pCksum));
  return 0;
}

/*
** Compute the SHA1 checksum of a zero-terminated string.  The
** result is held in memory obtained from mprintf().
*/
char *sha1sum(const char *zIn){
  SHA1Context ctx;
  unsigned char zResult[20];
  char zDigest[41];

  SHA1Reset(&ctx);
  SHA1Input(&ctx, (unsigned const char*)zIn, strlen(zIn));
  SHA1Result(&ctx, zResult);
  DigestToBase16(zResult, zDigest);
  return mprintf("%s", zDigest);
}

/*
** Convert a cleartext password for a specific user into a SHA1 hash.
** 
** The algorithm here is:
**
**       SHA1( project-code + "/" + login + "/" + password )
**
** In words: The users login name and password are appended to the
** project ID code and the SHA1 hash of the result is computed.
**
** The result of this function is the shared secret used by a client
** to authenticate to a server for the sync protocol.  It is also the
** value stored in the USER.PW field of the database.  By mixing in the
** login name and the project id with the hash, different shared secrets
** are obtained even if two users select the same password, or if a 
** single user selects the same password for multiple projects.
*/
char *sha1_shared_secret(const char *zPw, const char *zLogin){
  static char *zProjectId = 0;
  SHA1Context ctx;
  unsigned char zResult[20];
  char zDigest[41];

  SHA1Reset(&ctx);
  if( zProjectId==0 ){
    zProjectId = db_get("project-code", 0);

    /* On the first xfer request of a clone, the project-code is not yet
    ** known.  Use the cleartext password, since that is all we have.
    */
    if( zProjectId==0 ){
      return mprintf("%s", zPw);
    }
  }
  SHA1Input(&ctx, (unsigned char*)zProjectId, strlen(zProjectId));
  SHA1Input(&ctx, (unsigned char*)"/", 1);
  SHA1Input(&ctx, (unsigned char*)zLogin, strlen(zLogin));
  SHA1Input(&ctx, (unsigned char*)"/", 1);
  SHA1Input(&ctx, (unsigned const char*)zPw, strlen(zPw));
  SHA1Result(&ctx, zResult);
  DigestToBase16(zResult, zDigest);
  return mprintf("%s", zDigest);
}

/*
** COMMAND: sha1sum
** %fossil sha1sum FILE...
**
** Compute an SHA1 checksum of all files named on the command-line.
** If an file is named "-" then take its content from standard input.
*/
void sha1sum_test(void){
  int i;
  Blob in;
  Blob cksum;
  
  for(i=2; i<g.argc; i++){
    if( g.argv[i][0]=='-' && g.argv[i][1]==0 ){
      blob_read_from_channel(&in, stdin, -1);
      sha1sum_blob(&in, &cksum);
    }else{
      sha1sum_file(g.argv[i], &cksum);
    }
    printf("%s  %s\n", blob_str(&cksum), g.argv[i]);
    blob_reset(&cksum);
  }
}
