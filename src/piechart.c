/*
** Copyright (c) 2015 D. Richard Hipp
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
** This file contains code for generating pie charts on web pages.
**
*/
#include "config.h"
#include "piechart.h"
#include <math.h>

#ifndef M_PI
# define M_PI 3.1415926535897932385
#endif

/*
** Return an RGB color name given HSV values.  The HSV values
** must each be between between 0 and 255.  The string
** returned is held in a static buffer and is overwritten
** on each call.
*/
const char *rgbName(unsigned char h, unsigned char s, unsigned char v){
  static char zColor[8];
  unsigned char A, B, C, r, g, b;
  unsigned int i, m;
  if( s==0 ){
    r = g = b = v;
  }else{
    i = (h*6)/256;
    m = (h*6)&0xff;
    A = v*(256-s)/256;
    B = v*(65536-s*m)/65536;
    C = v*(65536-s*(256-m))/65536;
    @ <!-- hsv=%d(h),%d(s),%d(v) i=%d(i) m=%d(m) ABC=%d(A),%d(B),%d(C) -->
    switch( i ){
      case 0:  r=v; g=C; b=A;  break;
      case 1:  r=B; g=v; b=A;  break;
      case 2:  r=A; g=v; b=C;  break;
      case 3:  r=A; g=B; b=v;  break;
      case 4:  r=C; g=A; b=v;  break;
      default: r=v; g=A; b=B;  break;
    }
  }
  sqlite3_snprintf(sizeof(zColor),zColor,"#%02x%02x%02x",r,g,b);
  return zColor;
}

/*
** Flags that can be passed into the pie-chart generator
*/
#if INTERFACE
#define PIE_OTHER     0x0001    /* No wedge less than 1/60th of the circle */
#define PIE_CHROMATIC 0x0002    /* Wedge colors are in chromatic order */
#define PIE_PERCENT   0x0004    /* Add "(XX%)" marks on each label */
#endif

/*
** A pie-chart wedge label
*/
struct WedgeLabel {
  double rCos, rSin;   /* Sine and Cosine of center angle of wedge */
  char *z;             /* Label to draw on this wedge */
};
typedef struct WedgeLabel WedgeLabel;

/*
** Comparison callback for qsort() to sort labels in order of increasing
** distance above and below the horizontal centerline.
*/
static int wedgeCompare(const void *a, const void *b){
  const WedgeLabel *pA = (const WedgeLabel*)a;
  const WedgeLabel *pB = (const WedgeLabel*)b;
  double rA = fabs(pA->rCos);
  double rB = fabs(pB->rCos);
  if( rA<rB ) return -1;
  if( rA>rB ) return +1;
  return 0;
}

/*
** Output HTML that will render a pie chart using data from
** the PIECHART temporary table.
**
** The schema for the PIECHART table should be:
**
**     CREATE TEMP TABLE piechart(amt REAL, label TEXT);
*/
void piechart_render(int width, int height, unsigned int pieFlags){
  Stmt q;
  double cx, cy;          /* center of the pie */
  double r, r2;           /* Radius of the pie */
  double x1,y1;           /* Start of the slice */
  double x2,y2;           /* End of the slice */
  double x3,y3;           /* Middle point of the slice */
  double x4,y4;           /* End of line extending from x3,y3 */
  double x5,y5;           /* Text anchor */
  double d1;              /* radius to x4,y4 */
  const char *zAnc;       /* Anchor point for text */
  double a1 = 0.0;        /* Angle for first edge of slice */
  double a2;              /* Angle for second edge */
  double a3;              /* Angle at middle of slice */
  unsigned char h;        /* Hue */
  const char *zClr;       /* Color */
  int l;                  /* Large arc flag */
  int j;                  /* Wedge number */
  double rTotal;          /* Total piechart.amt */
  double rTooSmall;       /* Sum of pieChart.amt entries less than 1/60th */
  int nTotal;             /* Total number of entries in piechart */
  int nTooSmall;          /* Number of pieChart.amt entries less than 1/60th */
  const char *zFg;        /* foreground color for lines and text */
  int nWedgeAlloc = 0;    /* Slots allocated for aWedge[] */
  int nWedge = 0;         /* Slots used for aWedge[] */
  WedgeLabel *aWedge = 0; /* Labels */
  double rUprRight;       /* Floor for next label in the upper right quadrant */
  double rUprLeft;        /* Floor for next label in the upper left quadrant */
  double rLwrRight;       /* Ceiling for label in the lower right quadrant */
  double rLwrLeft;        /* Ceiling for label in the lower left quadrant */
  int i;                  /* Loop counter looping over wedge labels */

# define SATURATION    128
# define VALUE         192
# define OTHER_CUTOFF  90.0
# define TEXT_HEIGHT   15.0

  cx = 0.5*width;
  cy = 0.5*height;
  r2 = cx<cy ? cx : cy;
  r = r2 - 80.0;
  if( r<0.33333*r2 ) r = 0.33333*r2;
  h = 0;
  zFg = skin_detail_boolean("white-foreground") ? "white" : "black";

  db_prepare(&q, "SELECT sum(amt), count(*) FROM piechart");
  if( db_step(&q)!=SQLITE_ROW ) return;
  rTotal = db_column_double(&q, 0);
  nTotal = db_column_int(&q, 1);
  db_finalize(&q);
  rTooSmall = 0.0;
  nTooSmall = 0;
  if( (pieFlags & PIE_OTHER)!=0 && nTotal>1 ){
    db_prepare(&q, "SELECT sum(amt), count(*) FROM piechart WHERE amt<:amt");
    db_bind_double(&q, ":amt", rTotal/OTHER_CUTOFF);
    if( db_step(&q)==SQLITE_ROW ){
      rTooSmall = db_column_double(&q, 0);
      nTooSmall = db_column_double(&q, 1);
    }
    db_finalize(&q);
  }
  if( nTooSmall>1 ){
    db_prepare(&q, "SELECT amt, label FROM piechart WHERE amt>=:limit"
                   " UNION ALL SELECT %.17g, '%d others';",
                    rTooSmall, nTooSmall);
    db_bind_double(&q, ":limit", rTotal/OTHER_CUTOFF);
    nTotal += 1 - nTooSmall;
  }else{
    db_prepare(&q, "SELECT amt, label FROM piechart");
  }
  if( nTotal<=10 ) pieFlags |= PIE_CHROMATIC;
  for(j=0; db_step(&q)==SQLITE_ROW; j++){
    double x = db_column_double(&q,0)/rTotal;
    const char *zLbl = db_column_text(&q,1);
    /* @ <!-- x=%g(x) zLbl="%h(zLbl)" h=%d(h) --> */
    if( x<=0.0 ) continue;
    x1 = cx + sin(a1)*r;
    y1 = cy - cos(a1)*r;
    a2 = a1 + x*2.0*M_PI;
    x2 = cx + sin(a2)*r;
    y2 = cy - cos(a2)*r;
    a3 = 0.5*(a1+a2);
    if( nWedge+1>nWedgeAlloc ){
      nWedgeAlloc = nWedgeAlloc*2 + 40;
      aWedge = fossil_realloc(aWedge, sizeof(aWedge[0])*nWedgeAlloc);
    }
    if( pieFlags & PIE_PERCENT ){
      int pct = (int)(x*100.0 + 0.5);
      aWedge[nWedge].z = mprintf("%s (%d%%)", zLbl, pct);
    }else{
      aWedge[nWedge].z = fossil_strdup(zLbl);
    }
    aWedge[nWedge].rSin = sin(a3);
    aWedge[nWedge].rCos = cos(a3);
    nWedge++;
    if( (j&1)==0 || (pieFlags & PIE_CHROMATIC)!=0 ){
      h = 256*j/nTotal;
    }else if( j+2<nTotal ){
      h = 256*(j+2)/nTotal;
    }else{
      h = 256*((j+2+(nTotal&1))%nTotal)/nTotal;
    }
    zClr = rgbName(h,SATURATION,VALUE);
    l = x>=0.5;
    a1 = a2;
    @ <path class='piechartWedge'
    @  stroke="black" stroke-width="1" fill="%s(zClr)"
    @  d='M%g(cx),%g(cy)L%g(x1),%g(y1)A%g(r),%g(r) 0 %d(l),1 %g(x2),%g(y2)z'/>
  }
  qsort(aWedge, nWedge, sizeof(aWedge[0]), wedgeCompare);
  rUprLeft = height;
  rLwrLeft = 0;
  rUprRight = height;
  rLwrRight = 0;
  d1 = r*1.1;
  for(i=0; i<nWedge; i++){
    WedgeLabel *p = &aWedge[i];
    x3 = cx + p->rSin*r;
    y3 = cy - p->rCos*r;
    x4 = cx + p->rSin*d1;
    y4 = cy - p->rCos*d1;
    if( y4<=cy ){
      if( x4>=cx ){
        if( y4>rUprRight ){
          y4 = rUprRight;
        }
        rUprRight = y4 - TEXT_HEIGHT;
      }else{
        if( y4>rUprLeft ){
          y4 = rUprLeft;
        }
        rUprLeft = y4 - TEXT_HEIGHT;
      }
    }else{
      if( x4>=cx ){
        if( y4<rLwrRight ){
          y4 = rLwrRight;
        }
        rLwrRight = y4 + TEXT_HEIGHT;
      }else{
        if( y4<rLwrLeft ){
          y4 = rLwrLeft;
        }
        rLwrLeft = y4 + TEXT_HEIGHT;
      }
    }
    if( x4<cx ){
      x5 = x4 - 1.0;
      zAnc = "end";
    }else{
      x5 = x4 + 1.0;
      zAnc = "start";
    }
    y5 = y4 - 3.0 + 6.0*(1.0 - p->rCos);
    @ <line stroke-width='1' stroke='%s(zFg)' class='piechartLine'
    @  x1='%g(x3)' y1='%g(y3)' x2='%g(x4)' y2='%g(y4)'/>
    @ <text text-anchor="%s(zAnc)" fill='%s(zFg)' class="piechartLabel"
    @  x='%g(x5)' y='%g(y5)'>%h(p->z)</text>
    fossil_free(p->z);
  }
  db_finalize(&q);
  fossil_free(aWedge);
}

/*
** WEBPAGE: test-piechart
**
** Generate a pie-chart based on data input from a form.
*/
void piechart_test_page(void){
  const char *zData;
  Stmt ins;
  int n = 0;
  int width;
  int height;
  int i, j;

  login_check_credentials();
  style_header("Pie Chart Test");
  db_multi_exec("CREATE TEMP TABLE piechart(amt REAL, label TEXT);");
  db_prepare(&ins, "INSERT INTO piechart(amt,label) VALUES(:amt,:label)");
  zData = PD("data","");
  width = atoi(PD("width","800"));
  height = atoi(PD("height","400"));
  i = 0;
  while( zData[i] ){
    double rAmt;
    char *zLabel;
    while( fossil_isspace(zData[i]) ){ i++; }
    j = i;
    while( fossil_isdigit(zData[j]) ){ j++; }
    if( zData[j]=='.' ){
      j++;
      while( fossil_isdigit(zData[j]) ){ j++; }
    }
    if( i==j ) break;
    rAmt = atof(&zData[i]);
    i = j;
    while( zData[i]==',' || fossil_isspace(zData[i]) ){ i++; }
    n++;
    zLabel = mprintf("label%02d-%g", n, rAmt);
    db_bind_double(&ins, ":amt", rAmt);
    db_bind_text(&ins, ":label", zLabel);
    db_step(&ins);
    db_reset(&ins);
    fossil_free(zLabel);
  }
  db_finalize(&ins);
  if( n>1 ){
    @ <svg width=%d(width) height=%d(height) style="border:1px solid #d3d3d3;">
    piechart_render(width,height, PIE_OTHER|PIE_PERCENT);
    @ </svg>
    @ <hr />
  }
  @ <form method="POST" action='%R/test-piechart'>
  @ <p>Comma-separated list of slice widths:<br />
  @ <input type='text' name='data' size='80' value='%h(zData)'/><br />
  @ Width: <input type='text' size='8' name='width' value='%d(width)'/>
  @ Height: <input type='text' size='8' name='height' value='%d(height)'/><br />
  @ <input type='submit' value='Draw The Pie Chart'/>
  @ </form>
  @ <p>Interesting test cases:
  @ <ul>
  @ <li> <a href='test-piechart?data=44,2,2,2,2,2,3,2,2,2,2,2,44'>Case 1</a>
  @ <li> <a href='test-piechart?data=2,2,2,2,2,44,44,2,2,2,2,2'>Case 2</a>
  @ <li> <a href='test-piechart?data=20,2,2,2,2,2,2,2,2,2,2,80'>Case 3</a>
  @ <li> <a href='test-piechart?data=80,2,2,2,2,2,2,2,2,2,2,20'>Case 4</a>
  @ <li> <a href='test-piechart?data=2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2'>Case 5</a>
  @ </ul>
  style_footer();
}
