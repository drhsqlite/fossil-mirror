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
#endif

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
  int rot;                /* Text rotation angle */
  double sina3, cosa3;    /* sin(a3) and cos(a3) */
  unsigned char h;        /* Hue */
  const char *zClr;       /* Color */
  int l;                  /* Large arc flag */
  int j;                  /* Wedge number */
  double rTotal;          /* Total piechart.amt */
  double rTooSmall;       /* Sum of pieChart.amt entries less than 1/60th */
  int nTotal;             /* Total number of entries in piechart */
  int nTooSmall;          /* Number of pieChart.amt entries less than 1/60th */

# define SATURATION 128
# define VALUE      192

  cx = 0.5*width;
  cy = 0.5*height;
  r2 = cx<cy ? cx : cy;
  r = r2 - 60.0;
  if( r<0.33333*r2 ) r = 0.33333*r2;
  h = 0;

  db_prepare(&q, "SELECT sum(amt), count(*) FROM piechart");
  if( db_step(&q)!=SQLITE_ROW ) return;
  rTotal = db_column_double(&q, 0);
  nTotal = db_column_int(&q, 1);
  db_finalize(&q);
  rTooSmall = 0.0;
  nTooSmall = 0;
  if( (pieFlags & PIE_OTHER)!=0 && nTotal>1 ){
    db_prepare(&q, "SELECT sum(amt), count(*) FROM piechart WHERE amt<:amt");
    db_bind_double(&q, ":amt", rTotal/60.0);
    if( db_step(&q)==SQLITE_ROW ){
      rTooSmall = db_column_double(&q, 0);
      nTooSmall = db_column_double(&q, 1);
    }
    db_finalize(&q);
  }
  if( nTooSmall>1 ){
    db_prepare(&q, "SELECT amt, label FROM piechart WHERE amt>=:limit"
                   " UNION ALL SELECT %.17g, '(%d others)';",
                    rTooSmall, nTooSmall);
    db_bind_double(&q, ":limit", rTotal/60.0);
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
    sina3 = sin(a3);
    cosa3 = cos(a3);
    x3 = cx + sina3*r;
    y3 = cy - cosa3*r;
    d1 = r*1.1;
    x4 = cx + sina3*d1;
    y4 = cy - cosa3*d1;
    y5 = y4 - 3.0 + 6.0*(1.0 -cosa3);
    rot = ((int)(a3*180/M_PI))%180;
    if( a2-a1 > 0.6 ){
      rot = 0;  /* Never rotate text on fat slices */
    }else if( rot<60 ){
      rot = (rot - 60)/2;
    }else if( rot>120 ){
      rot = (rot - 120)/2;
    }else{
      rot = 0;
    }
    if( x4<=cx ){
      x5 = x4 - 5.0;
      zAnc = "end";
    }else{
      x5 = x4 + 4.0;
      zAnc = "start";
    }
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
    @ <path stroke="black" stroke-width="1" fill="%s(zClr)"
    @  d='M%g(cx),%g(cy)L%g(x1),%g(y1)A%g(r),%g(r) 0 %d(l),1 %g(x2),%g(y2)z'/>
    @ <line stroke='black' stroke-width='1'
    @  x1='%g(x3)' y1='%g(y3)' x2='%g(x4)' y2='%g(y4)''/>
    if( rot!=0 ){
      @ <text text-anchor="%s(zAnc)" transform='rotate(%d(rot),%g(x5),%g(y5))'
    }else{
      @ <text text-anchor="%s(zAnc)" transform='rotate(%d(rot),%g(x5),%g(y5))'
    }
    @  x='%g(x5)' y='%g(y5)'>%h(zLbl)</text>
  }
  db_finalize(&q);

}

/*
** WEBPAGE: test-piechart
**
** Generate a pie-chart based on data input from a form.
*/
void piechart_test_page(void){
  const char *zData;
  Stmt ins, q;
  Blob all, line, token1, token2;
  login_check_credentials();
  int n = 0;
  int width;
  int height;

  style_header("Pie Chart Test");
  db_multi_exec("CREATE TEMP TABLE piechart(amt REAL, label TEXT);");
  db_prepare(&ins, "INSERT INTO piechart(amt,label) VALUES(:amt,:label)");
  zData = PD("data","");
  width = atoi(PD("width","800"));
  height = atoi(PD("height","400"));
  blob_init(&all, zData, -1);
  while( blob_line(&all, &line) ){
    double rAmt;
    if( blob_token(&line, &token1)==0 ) continue;
    rAmt = atof(blob_str(&token1));
    if( rAmt<=0.0 ) continue;
    blob_tail(&line, &token2);
    db_bind_double(&ins, ":amt", rAmt);
    db_bind_text(&ins, ":label", blob_str(&token2));
    db_step(&ins);
    db_reset(&ins);
    n++;
  }
  db_finalize(&ins);
  blob_reset(&all);
  if( n>0 ){
    @ <svg width=%d(width) height=%d(height) style="border:1px solid #d3d3d3;">
    piechart_render(width,height, PIE_OTHER);
    @ </svg>
    @ <hr>
  }
  @ <form method="post" action='%R/test-piechart'>
  @ <p>One slice per line.  Value and then Label.<p>
  @ <textarea name='data' rows='20' cols='80'>%h(zData)</textarea><br/>
  @ Width: <input type='text' size='8' name='width' value='%d(width)'/>
  @ Height: <input type='text' size='8' name='height' value='%d(height)'/><br/>
  @ <input type='hidden' name='width' value='%d(width)'/>
  @ <input type='hidden' name='height' value='%d(height)'/>
  @ <input type='submit' value='Draw The Pie Chart'/>
  @ </form>
  @ <hr><p>Previous Data:</p>
  @ <table border="1">
  db_prepare(&q, "SELECT rowid, amt, label FROM piechart");
  while( db_step(&q)==SQLITE_ROW ){
     @ <tr><td>%d(db_column_int(&q,0))</td>
     @ <td>%g(db_column_double(&q,1))</td>
     @ <td>%h(db_column_text(&q,2))</td></tr>
  }
  db_finalize(&q);
  @ </table>
  style_footer();
}
