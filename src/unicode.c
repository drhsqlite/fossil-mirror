/*
** Copyright (c) 2013 D. Richard Hipp
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
** This file is copied from ext/fts5/fts5_unicode2.c of SQLite3 with
** minor changes.
*/
#include "config.h"
#include "unicode.h"

/*
** Return true if the argument corresponds to a unicode codepoint
** classified as either a letter or a number. Otherwise false.
**
** The results are undefined if the value passed to this function
** is less than zero.
*/
int unicode_isalnum(int c){
  /* Each unsigned integer in the following array corresponds to a contiguous
  ** range of unicode codepoints that are not either letters or numbers (i.e.
  ** codepoints for which this function should return 0).
  **
  ** The most significant 22 bits in each 32-bit value contain the first
  ** codepoint in the range. The least significant 10 bits are used to store
  ** the size of the range (always at least 1). In other words, the value
  ** ((C<<22) + N) represents a range of N codepoints starting with codepoint
  ** C. It is not possible to represent a range larger than 1023 codepoints
  ** using this format.
  */
  static const unsigned int aEntry[] = {
    0x00000030, 0x0000E807, 0x00016C06, 0x0001EC2F, 0x0002AC07,
    0x0002D001, 0x0002D803, 0x0002EC01, 0x0002FC01, 0x00035C01,
    0x0003DC01, 0x000B0804, 0x000B480E, 0x000B9407, 0x000BB401,
    0x000BBC81, 0x000DD401, 0x000DF801, 0x000E1002, 0x000E1C01,
    0x000FD801, 0x00120808, 0x00156806, 0x00162402, 0x00163403,
    0x00164437, 0x0017CC02, 0x0018001D, 0x00187802, 0x00192C15,
    0x0019A804, 0x0019C001, 0x001B5001, 0x001B580F, 0x001B9C07,
    0x001BF402, 0x001C000E, 0x001C3C01, 0x001C4401, 0x001CC01B,
    0x001E980B, 0x001FAC09, 0x001FD804, 0x001FF403, 0x00205804,
    0x00206C09, 0x00209403, 0x0020A405, 0x0020C00F, 0x00216403,
    0x00217801, 0x00234C31, 0x0024E803, 0x0024F812, 0x00254407,
    0x00258804, 0x0025C001, 0x00260403, 0x0026F001, 0x0026F807,
    0x00271C02, 0x00272C03, 0x00275C01, 0x00278802, 0x0027C802,
    0x0027E802, 0x0027F402, 0x00280403, 0x0028F001, 0x0028F805,
    0x00291C02, 0x00292C03, 0x00294401, 0x0029C002, 0x0029D402,
    0x002A0403, 0x002AF001, 0x002AF808, 0x002B1C03, 0x002B2C03,
    0x002B8802, 0x002BC002, 0x002BE806, 0x002C0403, 0x002CF001,
    0x002CF807, 0x002D1C02, 0x002D2C03, 0x002D5403, 0x002D8802,
    0x002DC001, 0x002E0801, 0x002EF805, 0x002F1803, 0x002F2804,
    0x002F5C01, 0x002FCC08, 0x00300005, 0x0030F807, 0x00311803,
    0x00312804, 0x00315402, 0x00318802, 0x0031DC01, 0x0031FC01,
    0x00320404, 0x0032F001, 0x0032F807, 0x00331803, 0x00332804,
    0x00335402, 0x00338802, 0x00340004, 0x0034EC02, 0x0034F807,
    0x00351803, 0x00352804, 0x00353C01, 0x00355C01, 0x00358802,
    0x0035E401, 0x00360403, 0x00372801, 0x00373C06, 0x00375801,
    0x00376008, 0x0037C803, 0x0038C401, 0x0038D007, 0x0038FC01,
    0x00391C09, 0x00396802, 0x003AC401, 0x003AD009, 0x003B2006,
    0x003C041F, 0x003CD00C, 0x003DC417, 0x003E340B, 0x003E6424,
    0x003EF80F, 0x003F380D, 0x0040AC14, 0x00412806, 0x00415804,
    0x00417803, 0x00418803, 0x00419C07, 0x0041C404, 0x0042080C,
    0x00423C01, 0x00426806, 0x0043EC01, 0x004D740C, 0x004E400A,
    0x00500001, 0x0059B402, 0x005A0001, 0x005A6C02, 0x005BAC03,
    0x005C4803, 0x005CC805, 0x005D4802, 0x005DC802, 0x005ED023,
    0x005F6004, 0x005F7401, 0x0060000F, 0x00621402, 0x0062A401,
    0x0064800C, 0x0064C00C, 0x00650001, 0x00651002, 0x00677822,
    0x00685C05, 0x00687802, 0x0069540A, 0x0069801D, 0x0069FC01,
    0x006A8007, 0x006AA006, 0x006AC011, 0x006C0005, 0x006CD011,
    0x006D6823, 0x006E0003, 0x006E840D, 0x006F980E, 0x006FF004,
    0x00709014, 0x0070EC05, 0x0071F802, 0x00730008, 0x00734019,
    0x0073B401, 0x0073D001, 0x0073DC03, 0x0077003A, 0x0077EC05,
    0x007EF401, 0x007EFC03, 0x007F3403, 0x007F7403, 0x007FB403,
    0x007FF402, 0x00800065, 0x0081980A, 0x0081E805, 0x00822805,
    0x00828020, 0x00834021, 0x00840002, 0x00840C04, 0x00842002,
    0x00845001, 0x00845803, 0x00847806, 0x00849401, 0x00849C01,
    0x0084A401, 0x0084B801, 0x0084E802, 0x00850005, 0x00852804,
    0x00853C01, 0x00862802, 0x00864297, 0x0091000B, 0x0092704E,
    0x00940276, 0x009E53E0, 0x00ADD820, 0x00AE5C69, 0x00B39406,
    0x00B3BC03, 0x00B3E404, 0x00B3F802, 0x00B5C001, 0x00B5FC01,
    0x00B7804F, 0x00B8C023, 0x00BA001A, 0x00BA6C59, 0x00BC00D6,
    0x00BFC00C, 0x00C00005, 0x00C02019, 0x00C0A807, 0x00C0D802,
    0x00C0F403, 0x00C26404, 0x00C28001, 0x00C3EC01, 0x00C64002,
    0x00C6580A, 0x00C70024, 0x00C8001F, 0x00C8A81E, 0x00C94001,
    0x00C98020, 0x00CA2827, 0x00CB0140, 0x01370040, 0x02924037,
    0x0293F802, 0x02983403, 0x0299BC10, 0x029A7802, 0x029BC008,
    0x029C0017, 0x029C8002, 0x029E2402, 0x02A00801, 0x02A01801,
    0x02A02C01, 0x02A08C0A, 0x02A0D804, 0x02A1D004, 0x02A20002,
    0x02A2D012, 0x02A33802, 0x02A38012, 0x02A3E003, 0x02A3F001,
    0x02A3FC01, 0x02A4980A, 0x02A51C0D, 0x02A57C01, 0x02A60004,
    0x02A6CC1B, 0x02A77802, 0x02A79401, 0x02A8A40E, 0x02A90C01,
    0x02A93002, 0x02A97004, 0x02A9DC03, 0x02A9EC03, 0x02AAC001,
    0x02AAC803, 0x02AADC02, 0x02AAF802, 0x02AB0401, 0x02AB7802,
    0x02ABAC07, 0x02ABD402, 0x02AD6C01, 0x02ADA802, 0x02AF8C0B,
    0x03600001, 0x036DFC02, 0x036FFC02, 0x037FFC01, 0x03EC7801,
    0x03ECA401, 0x03EEC810, 0x03F4F802, 0x03F7F002, 0x03F8001A,
    0x03F88033, 0x03F95013, 0x03F9A004, 0x03FBFC01, 0x03FC040F,
    0x03FC6807, 0x03FCEC06, 0x03FD6C0B, 0x03FF8007, 0x03FFA007,
    0x03FFE405, 0x04040003, 0x0404DC09, 0x0405E411, 0x04063003,
    0x0406400D, 0x04068001, 0x0407402E, 0x040B8001, 0x040DD805,
    0x040E7C01, 0x040F4001, 0x0415BC01, 0x04215C01, 0x0421DC02,
    0x04247C01, 0x0424FC01, 0x04280403, 0x04281402, 0x04283004,
    0x0428E003, 0x0428FC01, 0x04294009, 0x0429FC01, 0x042B2001,
    0x042B9402, 0x042BC007, 0x042CE407, 0x042E6404, 0x04349004,
    0x043AAC03, 0x043D180B, 0x043D5405, 0x04400003, 0x0440E016,
    0x0441FC04, 0x0442C012, 0x04433401, 0x04440003, 0x04449C0E,
    0x04450004, 0x04451402, 0x0445CC03, 0x04460003, 0x0446CC0E,
    0x0447140B, 0x04476C01, 0x04477403, 0x0448B013, 0x044AA401,
    0x044B7C0C, 0x044C0004, 0x044CEC02, 0x044CF807, 0x044D1C02,
    0x044D2C03, 0x044D5C01, 0x044D8802, 0x044D9807, 0x044DC005,
    0x0450D412, 0x04512C05, 0x04516802, 0x04517402, 0x0452C014,
    0x04531801, 0x0456BC07, 0x0456E020, 0x04577002, 0x0458C014,
    0x0459800D, 0x045AAC0D, 0x045C740F, 0x045CF004, 0x0460B010,
    0x0464C006, 0x0464DC02, 0x0464EC04, 0x04650001, 0x04650805,
    0x04674407, 0x04676807, 0x04678801, 0x04679001, 0x0468040A,
    0x0468CC07, 0x0468EC0D, 0x0469440B, 0x046A2813, 0x046A7805,
    0x0470BC08, 0x0470E008, 0x04710405, 0x0471C002, 0x04724816,
    0x0472A40E, 0x0474C406, 0x0474E801, 0x0474F002, 0x0474FC07,
    0x04751C01, 0x04762805, 0x04764002, 0x04764C05, 0x047BCC06,
    0x047F541D, 0x047FFC01, 0x0491C005, 0x04D0C009, 0x05A9B802,
    0x05ABC006, 0x05ACC010, 0x05AD1002, 0x05BA5C04, 0x05BD3C01,
    0x05BD4437, 0x05BE3C04, 0x05BF8801, 0x05BF9001, 0x05BFC002,
    0x06F27008, 0x074000F6, 0x07440027, 0x0744A4C0, 0x07480046,
    0x074C0057, 0x075B0401, 0x075B6C01, 0x075BEC01, 0x075C5401,
    0x075CD401, 0x075D3C01, 0x075DBC01, 0x075E2401, 0x075EA401,
    0x075F0C01, 0x0760028C, 0x076A6C05, 0x076A840F, 0x07800007,
    0x07802011, 0x07806C07, 0x07808C02, 0x07809805, 0x0784C007,
    0x07853C01, 0x078BB004, 0x078BFC01, 0x07A34007, 0x07A51007,
    0x07A57802, 0x07B2B001, 0x07B2C001, 0x07B4B801, 0x07BBC002,
    0x07C0002C, 0x07C0C064, 0x07C2800F, 0x07C2C40F, 0x07C3040F,
    0x07C34425, 0x07C434A1, 0x07C7981D, 0x07C8402C, 0x07C90009,
    0x07C94002, 0x07C98006, 0x07CC03D8, 0x07DB800D, 0x07DBC00D,
    0x07DC0074, 0x07DE0059, 0x07DF800C, 0x07E0000C, 0x07E04038,
    0x07E1400A, 0x07E18028, 0x07E2401E, 0x07E2C002, 0x07E40079,
    0x07E5E852, 0x07E73487, 0x07E9800E, 0x07E9C005, 0x07E9E003,
    0x07EA0007, 0x07EA4019, 0x07EAC007, 0x07EB0003, 0x07EB4007,
    0x07EC0093, 0x07EE5037, 0x38000401, 0x38008060, 0x380400F0,
  };
  static const unsigned int aAscii[4] = {
    0xFFFFFFFF, 0xFC00FFFF, 0xF8000001, 0xF8000001,
  };

  if( (unsigned int)c<128 ){
    return ( (aAscii[c >> 5] & ((unsigned int)1 << (c & 0x001F)))==0 );
  }else if( (unsigned int)c<(1<<22) ){
    unsigned int key = (((unsigned int)c)<<10) | 0x000003FF;
    int iRes = 0;
    int iHi = sizeof(aEntry)/sizeof(aEntry[0]) - 1;
    int iLo = 0;
    while( iHi>=iLo ){
      int iTest = (iHi + iLo) / 2;
      if( key >= aEntry[iTest] ){
        iRes = iTest;
        iLo = iTest+1;
      }else{
        iHi = iTest-1;
      }
    }
    assert( aEntry[0]<key );
    assert( key>=aEntry[iRes] );
    return (((unsigned int)c) >= ((aEntry[iRes]>>10) + (aEntry[iRes]&0x3FF)));
  }
  return 1;
}


/*
** If the argument is a codepoint corresponding to a lowercase letter
** in the ASCII range with a diacritic added, return the codepoint
** of the ASCII letter only. For example, if passed 235 - "LATIN
** SMALL LETTER E WITH DIAERESIS" - return 65 ("LATIN SMALL LETTER
** E"). The resuls of passing a codepoint that corresponds to an
** uppercase letter are undefined.
*/
static int unicode_remove_diacritic(int c, int bComplex){
  static const unsigned short aDia[] = {
        0,  1797,  1848,  1859,  1891,  1928,  1940,  1995,
     2024,  2040,  2060,  2110,  2168,  2206,  2264,  2286,
     2344,  2383,  2472,  2488,  2516,  2596,  2668,  2732,
     2782,  2842,  2894,  2954,  2984,  3000,  3028,  3336,
     3456,  3696,  3712,  3728,  3744,  3766,  3832,  3896,
     3912,  3928,  3944,  3968,  4008,  4040,  4056,  4106,
     4138,  4170,  4202,  4234,  4266,  4296,  4312,  4344,
     4408,  4424,  4442,  4472,  4488,  4504,  6148,  6198,
     6264,  6280,  6360,  6429,  6505,  6529, 61448, 61468,
    61512, 61534, 61592, 61610, 61642, 61672, 61688, 61704,
    61726, 61784, 61800, 61816, 61836, 61880, 61896, 61914,
    61948, 61998, 62062, 62122, 62154, 62184, 62200, 62218,
    62252, 62302, 62364, 62410, 62442, 62478, 62536, 62554,
    62584, 62604, 62640, 62648, 62656, 62664, 62730, 62766,
    62830, 62890, 62924, 62974, 63032, 63050, 63082, 63118,
    63182, 63242, 63274, 63310, 63368, 63390,
  };
#define HIBIT ((unsigned char)0x80)
  static const unsigned char aChar[] = {
    '\0',      'a',       'c',       'e',       'i',       'n',
    'o',       'u',       'y',       'y',       'a',       'c',
    'd',       'e',       'e',       'g',       'h',       'i',
    'j',       'k',       'l',       'n',       'o',       'r',
    's',       't',       'u',       'u',       'w',       'y',
    'z',       'o',       'u',       'a',       'i',       'o',
    'u',       'u'|HIBIT, 'a'|HIBIT, 'g',       'k',       'o',
    'o'|HIBIT, 'j',       'g',       'n',       'a'|HIBIT, 'a',
    'e',       'i',       'o',       'r',       'u',       's',
    't',       'h',       'a',       'e',       'o'|HIBIT, 'o',
    'o'|HIBIT, 'y',       '\0',      '\0',      '\0',      '\0',
    '\0',      '\0',      '\0',      '\0',      'a',       'b',
    'c'|HIBIT, 'd',       'd',       'e'|HIBIT, 'e',       'e'|HIBIT,
    'f',       'g',       'h',       'h',       'i',       'i'|HIBIT,
    'k',       'l',       'l'|HIBIT, 'l',       'm',       'n',
    'o'|HIBIT, 'p',       'r',       'r'|HIBIT, 'r',       's',
    's'|HIBIT, 't',       'u',       'u'|HIBIT, 'v',       'w',
    'w',       'x',       'y',       'z',       'h',       't',
    'w',       'y',       'a',       'a'|HIBIT, 'a'|HIBIT, 'a'|HIBIT,
    'e',       'e'|HIBIT, 'e'|HIBIT, 'i',       'o',       'o'|HIBIT,
    'o'|HIBIT, 'o'|HIBIT, 'u',       'u'|HIBIT, 'u'|HIBIT, 'y',
  };

  unsigned int key = (((unsigned int)c)<<3) | 0x00000007;
  int iRes = 0;
  int iHi = sizeof(aDia)/sizeof(aDia[0]) - 1;
  int iLo = 0;
  while( iHi>=iLo ){
    int iTest = (iHi + iLo) / 2;
    if( key >= aDia[iTest] ){
      iRes = iTest;
      iLo = iTest+1;
    }else{
      iHi = iTest-1;
    }
  }
  assert( key>=aDia[iRes] );
  if( bComplex==0 && (aChar[iRes] & 0x80) ) return c;
  return (c > (aDia[iRes]>>3) + (aDia[iRes]&0x07)) ? c :
                                                     ((int)aChar[iRes] & 0x7F);
}


/*
** Return true if the argument interpreted as a unicode codepoint
** is a diacritical modifier character.
*/
int unicode_is_diacritic(int c){
  unsigned int mask0 = 0x08029FDF;
  unsigned int mask1 = 0x000361F8;
  if( c<768 || c>817 ) return 0;
  return (c < 768+32) ?
      (mask0 & ((unsigned int)1 << (c-768))) :
      (mask1 & ((unsigned int)1 << (c-768-32)));
}


/*
** Interpret the argument as a unicode codepoint. If the codepoint
** is an upper case character that has a lower case equivalent,
** return the codepoint corresponding to the lower case version.
** Otherwise, return a copy of the argument.
**
** The results are undefined if the value passed to this function
** is less than zero.
*/
int unicode_fold(int c, int eRemoveDiacritic){
  /* Each entry in the following array defines a rule for folding a range
  ** of codepoints to lower case. The rule applies to a range of nRange
  ** codepoints starting at codepoint iCode.
  **
  ** If the least significant bit in flags is clear, then the rule applies
  ** to all nRange codepoints (i.e. all nRange codepoints are upper case and
  ** need to be folded). Or, if it is set, then the rule only applies to
  ** every second codepoint in the range, starting with codepoint C.
  **
  ** The 7 most significant bits in flags are an index into the aiOff[]
  ** array. If a specific codepoint C does require folding, then its lower
  ** case equivalent is ((C + aiOff[flags>>1]) & 0xFFFF).
  **
  ** The contents of this array are generated by parsing the CaseFolding.txt
  ** file distributed as part of the "Unicode Character Database". See
  ** http://www.unicode.org for details.
  */
  static const struct TableEntry {
    unsigned short iCode;
    unsigned char flags;
    unsigned char nRange;
  } aEntry[] = {
    {65, 14, 26},          {181, 66, 1},          {192, 14, 23},
    {216, 14, 7},          {256, 1, 48},          {306, 1, 6},
    {313, 1, 16},          {330, 1, 46},          {376, 156, 1},
    {377, 1, 6},           {383, 144, 1},         {385, 52, 1},
    {386, 1, 4},           {390, 46, 1},          {391, 0, 1},
    {393, 44, 2},          {395, 0, 1},           {398, 34, 1},
    {399, 40, 1},          {400, 42, 1},          {401, 0, 1},
    {403, 44, 1},          {404, 48, 1},          {406, 54, 1},
    {407, 50, 1},          {408, 0, 1},           {412, 54, 1},
    {413, 56, 1},          {415, 58, 1},          {416, 1, 6},
    {422, 62, 1},          {423, 0, 1},           {425, 62, 1},
    {428, 0, 1},           {430, 62, 1},          {431, 0, 1},
    {433, 60, 2},          {435, 1, 4},           {439, 64, 1},
    {440, 0, 1},           {444, 0, 1},           {452, 2, 1},
    {453, 0, 1},           {455, 2, 1},           {456, 0, 1},
    {458, 2, 1},           {459, 1, 18},          {478, 1, 18},
    {497, 2, 1},           {498, 1, 4},           {502, 162, 1},
    {503, 174, 1},         {504, 1, 40},          {544, 150, 1},
    {546, 1, 18},          {570, 74, 1},          {571, 0, 1},
    {573, 148, 1},         {574, 72, 1},          {577, 0, 1},
    {579, 146, 1},         {580, 30, 1},          {581, 32, 1},
    {582, 1, 10},          {837, 38, 1},          {880, 1, 4},
    {886, 0, 1},           {895, 38, 1},          {902, 20, 1},
    {904, 18, 3},          {908, 28, 1},          {910, 26, 2},
    {913, 14, 17},         {931, 14, 9},          {962, 0, 1},
    {975, 4, 1},           {976, 180, 1},         {977, 182, 1},
    {981, 186, 1},         {982, 184, 1},         {984, 1, 24},
    {1008, 176, 1},        {1009, 178, 1},        {1012, 170, 1},
    {1013, 168, 1},        {1015, 0, 1},          {1017, 192, 1},
    {1018, 0, 1},          {1021, 150, 3},        {1024, 36, 16},
    {1040, 14, 32},        {1120, 1, 34},         {1162, 1, 54},
    {1216, 6, 1},          {1217, 1, 14},         {1232, 1, 96},
    {1329, 24, 38},        {4256, 70, 38},        {4295, 70, 1},
    {4301, 70, 1},         {5112, 190, 6},        {7296, 126, 1},
    {7297, 128, 1},        {7298, 130, 1},        {7299, 134, 2},
    {7301, 132, 1},        {7302, 136, 1},        {7303, 138, 1},
    {7304, 100, 1},        {7312, 142, 43},       {7357, 142, 3},
    {7680, 1, 150},        {7835, 172, 1},        {7838, 120, 1},
    {7840, 1, 96},         {7944, 190, 8},        {7960, 190, 6},
    {7976, 190, 8},        {7992, 190, 8},        {8008, 190, 6},
    {8025, 191, 8},        {8040, 190, 8},        {8072, 190, 8},
    {8088, 190, 8},        {8104, 190, 8},        {8120, 190, 2},
    {8122, 166, 2},        {8124, 188, 1},        {8126, 124, 1},
    {8136, 164, 4},        {8140, 188, 1},        {8152, 190, 2},
    {8154, 160, 2},        {8168, 190, 2},        {8170, 158, 2},
    {8172, 192, 1},        {8184, 152, 2},        {8186, 154, 2},
    {8188, 188, 1},        {8486, 122, 1},        {8490, 116, 1},
    {8491, 118, 1},        {8498, 12, 1},         {8544, 8, 16},
    {8579, 0, 1},          {9398, 10, 26},        {11264, 24, 47},
    {11360, 0, 1},         {11362, 112, 1},       {11363, 140, 1},
    {11364, 114, 1},       {11367, 1, 6},         {11373, 108, 1},
    {11374, 110, 1},       {11375, 104, 1},       {11376, 106, 1},
    {11378, 0, 1},         {11381, 0, 1},         {11390, 102, 2},
    {11392, 1, 100},       {11499, 1, 4},         {11506, 0, 1},
    {42560, 1, 46},        {42624, 1, 28},        {42786, 1, 14},
    {42802, 1, 62},        {42873, 1, 4},         {42877, 98, 1},
    {42878, 1, 10},        {42891, 0, 1},         {42893, 88, 1},
    {42896, 1, 4},         {42902, 1, 20},        {42922, 80, 1},
    {42923, 76, 1},        {42924, 78, 1},        {42925, 84, 1},
    {42926, 80, 1},        {42928, 92, 1},        {42929, 86, 1},
    {42930, 90, 1},        {42931, 68, 1},        {42932, 1, 12},
    {42946, 0, 1},         {42948, 178, 1},       {42949, 82, 1},
    {42950, 96, 1},        {42951, 1, 4},         {42997, 0, 1},
    {43888, 94, 80},       {65313, 14, 26},
  };
  static const unsigned short aiOff[] = {
   1,     2,     8,     15,    16,    26,    28,    32,
   34,    37,    38,    40,    48,    63,    64,    69,
   71,    79,    80,    116,   202,   203,   205,   206,
   207,   209,   210,   211,   213,   214,   217,   218,
   219,   775,   928,   7264,  10792, 10795, 23217, 23221,
   23228, 23229, 23231, 23254, 23256, 23275, 23278, 26672,
   30152, 30204, 35267, 54721, 54753, 54754, 54756, 54787,
   54793, 54809, 57153, 57274, 57921, 58019, 58363, 59314,
   59315, 59324, 59325, 59326, 59332, 59356, 61722, 62528,
   65268, 65341, 65373, 65406, 65408, 65410, 65415, 65424,
   65436, 65439, 65450, 65462, 65472, 65476, 65478, 65480,
   65482, 65488, 65506, 65511, 65514, 65521, 65527, 65528,
   65529,
  };

  int ret = c;

  assert( sizeof(unsigned short)==2 && sizeof(unsigned char)==1 );

  if( c<128 ){
    if( c>='A' && c<='Z' ) ret = c + ('a' - 'A');
  }else if( c<65536 ){
    const struct TableEntry *p;
    int iHi = sizeof(aEntry)/sizeof(aEntry[0]) - 1;
    int iLo = 0;
    int iRes = -1;

    assert( c>aEntry[0].iCode );
    while( iHi>=iLo ){
      int iTest = (iHi + iLo) / 2;
      int cmp = (c - aEntry[iTest].iCode);
      if( cmp>=0 ){
        iRes = iTest;
        iLo = iTest+1;
      }else{
        iHi = iTest-1;
      }
    }

    assert( iRes>=0 && c>=aEntry[iRes].iCode );
    p = &aEntry[iRes];
    if( c<(p->iCode + p->nRange) && 0==(0x01 & p->flags & (p->iCode ^ c)) ){
      ret = (c + (aiOff[p->flags>>1])) & 0x0000FFFF;
      assert( ret>0 );
    }

    if( eRemoveDiacritic ){
      ret = unicode_remove_diacritic(ret, eRemoveDiacritic==2);
    }
  }

  else if( c>=66560 && c<66600 ){
    ret = c + 40;
  }
  else if( c>=66736 && c<66772 ){
    ret = c + 40;
  }
  else if( c>=68736 && c<68787 ){
    ret = c + 64;
  }
  else if( c>=71840 && c<71872 ){
    ret = c + 32;
  }
  else if( c>=93760 && c<93792 ){
    ret = c + 32;
  }
  else if( c>=125184 && c<125218 ){
    ret = c + 34;
  }

  return ret;
}
