/*
** Copyright (c) 2013 Stephan Beal
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
** This file contains code to implement the /reports web page.
**
*/
#include "config.h"
#include <string.h>
#include <time.h>
#include "statrep.h"


/*
** Used by stats_report_xxxxx() to remember which type of events
** to show. Populated by stats_report_init_view() and holds the
** return value of that function.
*/
static int statsReportType = 0;

/*
** Set by stats_report_init_view() to one of the y=XXXX values
** accepted by /timeline?y=XXXX.
*/
static const char *statsReportTimelineYFlag = NULL;


/*
** Creates a TEMP VIEW named v_reports which is a wrapper around the
** EVENT table filtered on event.type. It looks for the request
** parameter 'type' (reminder: we "should" use 'y' for consistency
** with /timeline, but /reports uses 'y' for the year) and expects it
** to contain one of the conventional values from event.type or the
** value "all", which is treated as equivalent to "*".  By default (if
** no 'y' is specified), "*" is assumed (that is also the default for
** invalid/unknown filter values). That 'y' filter is the one used for
** the event list. Note that a filter of "*" or "all" is equivalent to
** querying against the full event table. The view, however, adds an
** abstraction level to simplify the implementation code for the
** various /reports pages.
**
** Returns one of: 'c', 'f', 'w', 'g', 't', 'e', representing the type of
** filter it applies, or '*' if no filter is applied (i.e. if "all" is
** used).
*/
static int stats_report_init_view(){
  const char *zType = PD("type","*");  /* analog to /timeline?y=... */
  const char *zRealType = NULL;        /* normalized form of zType */
  int rc = 0;                          /* result code */
  char *zTimeSpan;                     /* Time span */
  assert( !statsReportType && "Must not be called more than once." );
  switch( (zType && *zType) ? *zType : 0 ){
    case 'c':
    case 'C':
      zRealType = "ci";
      rc = *zRealType;
      break;
    case 'e':
    case 'E':
      zRealType = "e";
      rc = *zRealType;
      break;
    case 'f':
    case 'F':
      zRealType = "f";
      rc = *zRealType;
      break;
    case 'g':
    case 'G':
      zRealType = "g";
      rc = *zRealType;
      break;
    case 'm':
    case 'M':
      zRealType = "m";
      rc = *zRealType;
      break;
    case 'n':
    case 'N':
      zRealType = "n";
      rc = *zRealType;
      break;
    case 't':
    case 'T':
      zRealType = "t";
      rc = *zRealType;
      break;
    case 'w':
    case 'W':
      zRealType = "w";
      rc = *zRealType;
      break;
    default:
      rc = '*';
      break;
  }
  assert(0 != rc);
  if( P("from")!=0 && P("to")!=0 ){
    zTimeSpan = mprintf(
          " (event.mtime BETWEEN julianday(%Q) AND julianday(%Q))",
          P("from"), P("to"));
  }else{
    zTimeSpan = " 1";
  }
  if( zRealType==0 ){
    statsReportTimelineYFlag = "a";
    db_multi_exec("CREATE TEMP VIEW v_reports AS "
                  "SELECT * FROM event WHERE %s", zTimeSpan/*safe-for-%s*/);
  }else if( rc!='n' && rc!='m' ){
    statsReportTimelineYFlag = zRealType;
    db_multi_exec("CREATE TEMP VIEW v_reports AS "
                  "SELECT * FROM event WHERE (type GLOB %Q) AND %s",
                  zRealType, zTimeSpan/*safe-for-%s*/);
  }else{
    const char *zNot = rc=='n' ? "NOT" : "";
    statsReportTimelineYFlag = "ci";
    db_multi_exec(
      "CREATE TEMP VIEW v_reports AS "
      "SELECT * FROM event WHERE type='ci' AND %s"
      " AND objid %s IN (SELECT cid FROM plink WHERE NOT isprim)",
      zTimeSpan/*safe-for-%s*/, zNot/*safe-for-%s*/
    );
  }
  return statsReportType = rc;
}

/*
** Returns a string suitable (for a given value of suitable) for
** use in a label with the header of the /reports pages, dependent
** on the 'type' flag. See stats_report_init_view().
** The returned bytes are static.
*/
static const char *stats_report_label_for_type(){
  assert( statsReportType && "Must call stats_report_init_view() first." );
  switch( statsReportType ){
    case 'c':
      return "check-ins";
    case 'm':
      return "merge check-ins";
    case 'n':
      return "non-merge check-ins";
    case 'e':
      return "technotes";
    case 'f':
      return "forum posts";
    case 'w':
      return "wiki changes";
    case 't':
      return "ticket changes";
    case 'g':
      return "tag changes";
    default:
      return "all types";
  }
}


/*
** Implements the "byyear" and "bymonth" reports for /reports.
** If includeMonth is true then it generates the "bymonth" report,
** else the "byyear" report. If zUserName is not NULL then the report is
** restricted to events created by the named user account.
*/
static void stats_report_by_month_year(
  char includeMonth,        /* 0 for stats-by-year.  1 for stats-by-month */
  const char *zUserName     /* Only report events by this user */
){
  Stmt query = empty_Stmt;
  int nRowNumber = 0;                /* current TR number */
  int nEventTotal = 0;               /* Total event count */
  int rowClass = 0;                  /* counter for alternating
                                        row colors */
  const char *zTimeLabel = includeMonth ? "Year/Month" : "Year";
  char zPrevYear[5] = {0};           /* For keeping track of when
                                        we change years while looping */
  int nEventsPerYear = 0;            /* Total event count for the
                                        current year */
  char showYearTotal = 0;            /* Flag telling us when to show
                                        the per-year event totals */
  int nMaxEvents  = 1;               /* for calculating length of graph
                                        bars. */
  int iterations = 0;                /* number of weeks/months we iterate
                                        over */

  char *zCurrentTF;                  /* The timeframe in which 'now' lives */
  double rNowFraction;               /* Fraction of 'now' timeframe that has
                                        passed */
  int nTFChar;                       /* Prefix of date() for timeframe */

  nTFChar = includeMonth ? 7 : 4;
  stats_report_init_view();
  db_prepare(&query,
             "SELECT substr(date(mtime),1,%d) AS timeframe,"
             "       count(*) AS eventCount"
             "  FROM v_reports"
             " WHERE ifnull(coalesce(euser,user,'')=%Q,1)"
             " GROUP BY timeframe"
             " ORDER BY timeframe DESC",
             nTFChar, zUserName);
  @ <h1>Timeline Events (%s(stats_report_label_for_type()))
  @ by year%s(includeMonth ? "/month" : "")
  if( zUserName ){
    @ for user %h(zUserName)
  }
  @ </h1>
  @ <table border='0' cellpadding='2' cellspacing='0' \
  zCurrentTF = db_text(0, "SELECT substr(date(),1,%d)", nTFChar);
  if( !includeMonth ){
    @ class='statistics-report-table-events sortable' \
    @ data-column-types='tnx' data-init-sort='0'>
    style_table_sorter();
    rNowFraction = db_double(0.5,
       "SELECT (unixepoch() - unixepoch('now','start of year'))*1.0/"
       "        (unixepoch('now','start of year','+1 year') - "
       "         unixepoch('now','start of year'));");
  }else{
    @ class='statistics-report-table-events'>
    rNowFraction = db_double(0.5,
       "SELECT (unixepoch() - unixepoch('now','start of month'))*1.0/"
       "       (unixepoch('now','start of month','+1 month') - "
       "        unixepoch('now','start of month'));");
  }
  @ <thead>
  @ <th>%s(zTimeLabel)</th>
  @ <th>Events</th>
  @ <th width='90%%'><!-- relative commits graph --></th>
  @ </thead><tbody>
  /*
     Run the query twice. The first time we calculate the maximum
     number of events for a given row. Maybe someone with better SQL
     Fu can re-implement this with a single query.
  */
  while( SQLITE_ROW == db_step(&query) ){
    int nCount = db_column_int(&query, 1);
    if( strcmp(db_column_text(&query,0),zCurrentTF)==0
     && rNowFraction>0.05
    ){
      nCount = (int)(((double)nCount)/rNowFraction);
    }
    if(nCount>nMaxEvents){
      nMaxEvents = nCount;
    }
    ++iterations;
  }
  db_reset(&query);
  while( SQLITE_ROW == db_step(&query) ){
    const char *zTimeframe = db_column_text(&query, 0);
    const int nCount = db_column_int(&query, 1);
    int nSize = (nCount>0 && nMaxEvents>0)
      ? (int)(100 * nCount / nMaxEvents)
      : 1;
    showYearTotal = 0;
    if(!nSize) nSize = 1;
    if(includeMonth){
      /* For Month/year view, add a separator for each distinct year. */
      if(!*zPrevYear ||
         (0!=fossil_strncmp(zPrevYear,zTimeframe,4))){
        showYearTotal = *zPrevYear;
        if(showYearTotal){
          rowClass = ++nRowNumber % 2;
          @ <tr class='row%d(rowClass)'>
          @ <td></td>
          @ <td colspan='2'>Yearly total: %d(nEventsPerYear)</td>
          @</tr>
          showYearTotal = 0;
        }
        nEventsPerYear = 0;
        memcpy(zPrevYear,zTimeframe,4);
        rowClass = ++nRowNumber % 2;
        @ <tr class='row%d(rowClass)'>
        @ <th colspan='3' class='statistics-report-row-year'>%s(zPrevYear)</th>
        @ </tr>
     }
   }
   rowClass = ++nRowNumber % 2;
   nEventTotal += nCount;
   nEventsPerYear += nCount;
   @<tr class='row%d(rowClass)'>
   @ <td>
    if(includeMonth){
      cgi_printf("<a href='%R/timeline?"
                 "ym=%t&y=%s",
                 zTimeframe,
                 statsReportTimelineYFlag );
      /* Reminder: n=nCount is not actually correct for bymonth unless
         that was the only user who caused events.
      */
      if( zUserName ){
        cgi_printf("&u=%t", zUserName);
      }
      cgi_printf("' target='_new'>%s</a>",zTimeframe);
    }else {
      cgi_printf("<a href='?view=byweek&y=%s&type=%c",
                 zTimeframe, (char)statsReportType);
      if( zUserName ){
        cgi_printf("&u=%t", zUserName);
      }
      cgi_printf("'>%s</a>", zTimeframe);
    }
    @ </td><td>%d(nCount)</td>
    @ <td style='white-space: nowrap;'>
    if( strcmp(zTimeframe, zCurrentTF)==0
     && rNowFraction>0.05
     && nCount>0
     && nMaxEvents>0
    ){
      /* If the timespan covered by this row contains "now", then project
      ** the number of changes until the completion of the timespan and
      ** show a dashed box of that projection. */
      int nProj = (int)(((double)nCount)/rNowFraction);
      int nExtra = (int)(((double)nCount)/rNowFraction) - nCount;
      int nXSize = (100 * nExtra)/nMaxEvents;
      @ <span class='statistics-report-graph-line' \
      @  style='display:inline-block;min-width:%d(nSize)%%;'>&nbsp;</span>\
      @ <span class='statistics-report-graph-extra' title='%d(nProj)' \
      @  style='display:inline-block;min-width:%d(nXSize)%%;'>&nbsp;</span>\
    }else{
      @ <div class='statistics-report-graph-line' \
      @  style='width:%d(nSize)%%;'>&nbsp;</div> \
    }
    @ </td>
    @ </tr>
    /*
      Potential improvement: calculate the min/max event counts and
      use percent-based graph bars.
    */
  }
  db_finalize(&query);
  if(includeMonth && !showYearTotal && *zPrevYear){
    /* Add final year total separator. */
    rowClass = ++nRowNumber % 2;
    @ <tr class='row%d(rowClass)'>
    @ <td></td>
    @ <td colspan='2'>Yearly total: %d(nEventsPerYear)</td>
    @</tr>
  }
  @ </tbody></table>
  if(nEventTotal){
    const char *zAvgLabel = includeMonth ? "month" : "year";
    int nAvg = iterations ? (nEventTotal/iterations) : 0;
    @ <br><div>Total events: %d(nEventTotal)
    @ <br>Average per active %s(zAvgLabel): %d(nAvg)
    @ </div>
  }
}

/*
** Implements the "byuser" view for /reports.
*/
static void stats_report_by_user(){
  Stmt query = empty_Stmt;
  int nRowNumber = 0;                /* current TR number */
  int rowClass = 0;                  /* counter for alternating
                                        row colors */
  int nMaxEvents = 1;                /* max number of events for
                                        all rows. */
  stats_report_init_view();
  @ <h1>Timeline Events
  @ (%s(stats_report_label_for_type())) by User</h1>
  db_multi_exec(
    "CREATE TEMP VIEW piechart(amt,label) AS"
    " SELECT count(*), ifnull(euser,user) FROM v_reports"
                         " GROUP BY ifnull(euser,user) ORDER BY count(*) DESC;"
  );
  if( db_int(0, "SELECT count(*) FROM piechart")>=2 ){
    @ <center><svg width=700 height=400>
    piechart_render(700, 400, PIE_OTHER|PIE_PERCENT);
    @ </svg></centre><hr>
  }
  style_table_sorter();
  @ <table class='statistics-report-table-events sortable' border='0' \
  @ cellpadding='2' cellspacing='0' data-column-types='tkx' data-init-sort='2'>
  @ <thead><tr>
  @ <th>User</th>
  @ <th>Events</th>
  @ <th width='90%%'><!-- relative commits graph --></th>
  @ </tr></thead><tbody>
  db_prepare(&query,
               "SELECT ifnull(euser,user), "
               "COUNT(*) AS eventCount "
               "FROM v_reports "
               "GROUP BY ifnull(euser,user) ORDER BY eventCount DESC");
  while( SQLITE_ROW == db_step(&query) ){
    const int nCount = db_column_int(&query, 1);
    if(nCount>nMaxEvents){
      nMaxEvents = nCount;
    }
  }
  db_reset(&query);
  while( SQLITE_ROW == db_step(&query) ){
    const char *zUser = db_column_text(&query, 0);
    const int nCount = db_column_int(&query, 1);
    char y = (char)statsReportType;
    int nSize = nCount
      ? (int)(100 * nCount / nMaxEvents)
      : 0;
    if(!nCount) continue /* arguable! Possible? */;
    else if(!nSize) nSize = 1;
    rowClass = ++nRowNumber % 2;
    @ <tr class='row%d(rowClass)'>
    @ <td>
    @ <a href="?view=bymonth&user=%h(zUser)&type=%c(y)">%h(zUser)</a>
    @ </td><td data-sortkey='%08x(-nCount)'>%d(nCount)</td>
    @ <td>
    @ <div class='statistics-report-graph-line'
    @  style='width:%d(nSize)%%;'>&nbsp;</div>
    @ </td>
    @</tr>
    /*
      Potential improvement: calculate the min/max event counts and
      use percent-based graph bars.
    */
  }
  @ </tbody></table>
  db_finalize(&query);
}

/*
** Implements the "byfile" view for /reports. If zUserName is not NULL then the
** report is restricted to events created by the named user account.
*/
static void stats_report_by_file(const char *zUserName){
  Stmt query;
  int mxEvent = 1;       /* max number of events across all rows */
  int nRowNumber = 0;

  db_multi_exec(
    "CREATE TEMP TABLE statrep(filename, cnt);"
    "INSERT INTO statrep(filename, cnt)"
    "  SELECT filename.name, count(distinct mlink.mid)"
    "    FROM filename, mlink, event"
    "   WHERE filename.fnid=mlink.fnid"
    "     AND mlink.mid=event.objid"
    "     AND ifnull(coalesce(euser,user,'')=%Q,1)"
    "   GROUP BY 1", zUserName
  );
  db_prepare(&query,
    "SELECT filename, cnt FROM statrep ORDER BY cnt DESC, filename /*sort*/"
  );
  mxEvent = db_int(1, "SELECT max(cnt) FROM statrep");
  @ <h1>Check-ins Per File
  if( zUserName ){
    @ for user %h(zUserName)
  }
  @ </h1>
  style_table_sorter();
  @ <table class='statistics-report-table-events sortable' border='0' \
  @ cellpadding='2' cellspacing='0' data-column-types='tNx' data-init-sort='2'>
  @ <thead><tr>
  @ <th>File</th>
  @ <th>Check-ins</th>
  @ <th width='90%%'><!-- relative commits graph --></th>
  @ </tr></thead><tbody>
  while( SQLITE_ROW == db_step(&query) ){
    const char *zFile = db_column_text(&query, 0);
    const int n = db_column_int(&query, 1);
    int sz;
    if( n<=0 ) continue;
    sz = (int)(100*n/mxEvent);
    if( sz==0 ) sz = 1;
    @<tr class='row%d(++nRowNumber%2)'>
    @ <td>%z(href("%R/finfo?name=%T",zFile))%h(zFile)</a></td>
    @ <td>%d(n)</td>
    @ <td>
    @ <div class='statistics-report-graph-line'
    @  style='width:%d(sz)%%;'>&nbsp;</div>
    @ </td>
    @</tr>
  }
  @ </tbody></table>
  db_finalize(&query);

}

/*
** Implements the "byweekday" view for /reports. If zUserName is not NULL then
** the report is restricted to events created by the named user account.
*/
static void stats_report_day_of_week(const char *zUserName){
  Stmt query = empty_Stmt;
  int nRowNumber = 0;                /* current TR number */
  int rowClass = 0;                  /* counter for alternating
                                        row colors */
  int nMaxEvents = 1;                /* max number of events for
                                        all rows. */
  static const char *const daysOfWeek[] = {
  "Sunday", "Monday", "Tuesday", "Wednesday",
  "Thursday", "Friday", "Saturday"
  };

  stats_report_init_view();
  db_prepare(&query,
               "SELECT cast(strftime('%%w', mtime) AS INTEGER) dow,"
               "       COUNT(*) AS eventCount"
               "  FROM v_reports"
               " WHERE ifnull(coalesce(euser,user,'')=%Q,1)"
               " GROUP BY dow ORDER BY dow", zUserName);
  @ <h1>Timeline Events (%h(stats_report_label_for_type())) by Day of the Week
  if( zUserName ){
    @ for user %h(zUserName)
  }
  @ </h1>
  db_multi_exec(
    "CREATE TEMP VIEW piechart(amt,label) AS"
    " SELECT count(*),"
    "   CASE cast(strftime('%%w', mtime) AS INT)"
    "    WHEN 0 THEN 'Sunday'"
    "    WHEN 1 THEN 'Monday'"
    "    WHEN 2 THEN 'Tuesday'"
    "    WHEN 3 THEN 'Wednesday'"
    "    WHEN 4 THEN 'Thursday'"
    "    WHEN 5 THEN 'Friday'"
    "    WHEN 6 THEN 'Saturday'"
    "    ELSE 'ERROR'"
    "   END"
    "  FROM v_reports"
    "  WHERE ifnull(coalesce(euser,user,'')=%Q,1)"
    "  GROUP BY 2 ORDER BY cast(strftime('%%w', mtime) AS INT);"
    , zUserName
  );
  if( db_int(0, "SELECT count(*) FROM piechart")>=2 ){
    @ <center><svg width=700 height=400>
    piechart_render(700, 400, PIE_OTHER|PIE_PERCENT);
    @ </svg></centre><hr>
  }
  style_table_sorter();
  @ <table class='statistics-report-table-events sortable' border='0' \
  @ cellpadding='2' cellspacing='0' data-column-types='ntnx' data-init-sort='1'>
  @ <thead><tr>
  @ <th>DoW</th>
  @ <th>Day</th>
  @ <th>Events</th>
  @ <th width='90%%'><!-- relative commits graph --></th>
  @ </tr></thead><tbody>
  while( SQLITE_ROW == db_step(&query) ){
    const int nCount = db_column_int(&query, 1);
    if(nCount>nMaxEvents){
      nMaxEvents = nCount;
    }
  }
  db_reset(&query);
  while( SQLITE_ROW == db_step(&query) ){
    const int dayNum =db_column_int(&query, 0);
    const int nCount = db_column_int(&query, 1);
    int nSize = nCount
      ? (int)(100 * nCount / nMaxEvents)
      : 0;
    if(!nCount) continue /* arguable! Possible? */;
    else if(!nSize) nSize = 1;
    rowClass = ++nRowNumber % 2;
    @<tr class='row%d(rowClass)'>
    @ <td>%d(dayNum)</td>
    @ <td>%s(daysOfWeek[dayNum])</td>
    @ <td>%d(nCount)</td>
    @ <td>
    @ <div class='statistics-report-graph-line'
    @  style='width:%d(nSize)%%;'>&nbsp;</div>
    @ </td>
    @</tr>
  }
  @ </tbody></table>
  db_finalize(&query);
}

/*
** Implements the "byhour" view for /reports. If zUserName is not NULL
** then the report is restricted to events created by the named user
** account.
*/
static void stats_report_hour_of_day(const char *zUserName){
  Stmt query = empty_Stmt;
  int nRowNumber = 0;                /* current TR number */
  int rowClass = 0;                  /* counter for alternating
                                        row colors */
  int nMaxEvents = 1;                /* max number of events for
                                        all rows. */

  stats_report_init_view();
  db_prepare(&query,
               "SELECT cast(strftime('%%H', mtime) AS INTEGER) hod,"
               "       COUNT(*) AS eventCount"
               "  FROM v_reports"
               " WHERE ifnull(coalesce(euser,user,'')=%Q,1)"
               " GROUP BY hod ORDER BY hod", zUserName);
  @ <h1>Timeline Events (%h(stats_report_label_for_type())) by Hour of Day
  if( zUserName ){
    @ for user %h(zUserName)
  }
  @ </h1>
  db_multi_exec(
    "CREATE TEMP VIEW piechart(amt,label) AS"
    " SELECT count(*), strftime('%%H', mtime) hod"
    "  FROM v_reports"
    "  WHERE ifnull(coalesce(euser,user,'')=%Q,1)"
    "  GROUP BY 2 ORDER BY hod;",
    zUserName
  );
  if( db_int(0, "SELECT count(*) FROM piechart")>=2 ){
    @ <center><svg width=700 height=400>
    piechart_render(700, 400, PIE_OTHER|PIE_PERCENT);
    @ </svg></centre><hr>
  }
  style_table_sorter();
  @ <table class='statistics-report-table-events sortable' border='0' \
  @ cellpadding='2' cellspacing='0' data-column-types='nnx' data-init-sort='1'>
  @ <thead><tr>
  @ <th>Hour</th>
  @ <th>Events</th>
  @ <th width='90%%'><!-- relative commits graph --></th>
  @ </tr></thead><tbody>
  while( SQLITE_ROW == db_step(&query) ){
    const int nCount = db_column_int(&query, 1);
    if(nCount>nMaxEvents){
      nMaxEvents = nCount;
    }
  }
  db_reset(&query);
  while( SQLITE_ROW == db_step(&query) ){
    const int hourNum =db_column_int(&query, 0);
    const int nCount = db_column_int(&query, 1);
    int nSize = nCount
      ? (int)(100 * nCount / nMaxEvents)
      : 0;
    if(!nCount) continue /* arguable! Possible? */;
    else if(!nSize) nSize = 1;
    rowClass = ++nRowNumber % 2;
    @<tr class='row%d(rowClass)'>
    @ <td>%d(hourNum)</td>
    @ <td>%d(nCount)</td>
    @ <td>
    @ <div class='statistics-report-graph-line'
    @  style='width:%d(nSize)%%;'>&nbsp;</div>
    @ </td>
    @</tr>
  }
  @ </tbody></table>
  db_finalize(&query);
}


/*
** Helper for stats_report_by_month_year(), which generates a list of
** week numbers.  The "y" query parameter is the year in format YYYY.
** If zUserName is not NULL then the report is restricted to events
** created by the named user account.
*/
static void stats_report_year_weeks(const char *zUserName){
  const char *zYear = P("y");        /* Year for which report shown */
  Stmt q;
  int nMaxEvents = 1;                /* max number of events for
                                        all rows. */
  int iterations = 0;                /* # of active time periods. */
  int rowCount = 0;
  int total = 0;
  char *zCurrentWeek;                /* Current week number */
  double rNowFraction = 0.0;         /* Fraction of current week that has
                                     ** passed */

  stats_report_init_view();
  style_submenu_sql("y", "Year:",
     "WITH RECURSIVE a(b) AS ("
     "  SELECT substr(date('now'),1,4) UNION ALL"
     "  SELECT b-1 FROM a"
     "   WHERE b>0+(SELECT substr(date(min(mtime)),1,4) FROM event)"
     ") SELECT b, b FROM a ORDER BY b DESC");
  if( zYear==0 || strlen(zYear)!=4 ){
    zYear = db_text("1970","SELECT substr(date('now'),1,4);");
  }
  cgi_printf("<br>\n");
  db_prepare(&q,
             "SELECT DISTINCT strftime('%%W',mtime) AS wk, "
             "       count(*) AS n "
             "  FROM v_reports "
             " WHERE %Q=substr(date(mtime),1,4) "
             "   AND mtime < current_timestamp "
             "   AND ifnull(coalesce(euser,user,'')=%Q,1)"
             " GROUP BY wk ORDER BY wk DESC", zYear, zUserName);
  @ <h1>Timeline events (%h(stats_report_label_for_type()))
  @ for the calendar weeks of %h(zYear)
  if( zUserName ){
    @  for user %h(zUserName)
  }
  @ </h1>
  zCurrentWeek = db_text(0,
      "SELECT strftime('%%W','now') WHERE date() LIKE '%q%%'",
      zYear);
  if( zCurrentWeek ){
    rNowFraction = db_double(0.5,
      "SELECT (unixepoch()-unixepoch('now','weekday 0','-7 days'))/604800.0;");
  }
    style_table_sorter();
  cgi_printf("<table class='statistics-report-table-events sortable' "
              "border='0' cellpadding='2' width='100%%' "
             "cellspacing='0' data-column-types='tnx' data-init-sort='0'>\n");
  cgi_printf("<thead><tr>"
             "<th>Week</th>"
             "<th>Events</th>"
             "<th width='90%%'><!-- relative commits graph --></th>"
             "</tr></thead>\n"
             "<tbody>\n");
  while( SQLITE_ROW == db_step(&q) ){
    int nCount = db_column_int(&q, 1);
    if( zCurrentWeek!=0
     && strcmp(db_column_text(&q,0),zCurrentWeek)==0
     && rNowFraction>0.05
    ){
      nCount = (int)(((double)nCount)/rNowFraction);
    }
    if(nCount>nMaxEvents){
      nMaxEvents = nCount;
    }
    ++iterations;
  }
  db_reset(&q);
  while( SQLITE_ROW == db_step(&q) ){
    const char *zWeek = db_column_text(&q,0);
    const int nCount = db_column_int(&q,1);
    int nSize = (nCount>0 && nMaxEvents>0)
      ? (int)(100 * nCount / nMaxEvents)
      : 0;
    if(!nSize) nSize = 1;
    total += nCount;
    cgi_printf("<tr class='row%d'>", ++rowCount % 2 );
    cgi_printf("<td><a href='%R/timeline?yw=%t%s&y=%s",
               zYear, zWeek,
               statsReportTimelineYFlag);
    if( zUserName ){
      cgi_printf("&u=%t",zUserName);
    }
    cgi_printf("'>%s</a></td>",zWeek);

    cgi_printf("<td>%d</td>",nCount);
    cgi_printf("<td style='white-space: nowrap;'>");
    if( nCount ){
      if( zCurrentWeek!=0
      && strcmp(zWeek, zCurrentWeek)==0
      && rNowFraction>0.05
      && nMaxEvents>0
      ){
        /* If the timespan covered by this row contains "now", then project
        ** the number of changes until the completion of the week and
        ** show a dashed box of that projection. */
        int nProj = (int)(((double)nCount)/rNowFraction);
        int nExtra = (int)(((double)nCount)/rNowFraction) - nCount;
        int nXSize = (100 * nExtra)/nMaxEvents;
        @ <span class='statistics-report-graph-line' \
        @  style='display:inline-block;min-width:%d(nSize)%%;'>&nbsp;</span>\
        @ <span class='statistics-report-graph-extra' title='%d(nProj)' \
        @  style='display:inline-block;min-width:%d(nXSize)%%;'>&nbsp;</span>\
      }else{
        @ <div class='statistics-report-graph-line' \
        @  style='width:%d(nSize)%%;'>&nbsp;</div> \
      }
    }
    cgi_printf("</td></tr>\n");
  }
  db_finalize(&q);
  cgi_printf("</tbody></table>");
  if(total){
    int nAvg = iterations ? (total/iterations) : 0;
    cgi_printf("<br><div>Total events: %d<br>"
               "Average per active week: %d</div>",
               total, nAvg);
  }
}


/*
** Generate a report that shows the most recent change for each user.
*/
static void stats_report_last_change(void){
  Stmt s;
  double rNow;
  char *zBaseUrl;

  stats_report_init_view();
  style_table_sorter();
  @ <h1>Event Summary
  @ (%s(stats_report_label_for_type())) by User</h1>
  @ <table border=1  class='statistics-report-table-events sortable' \
  @ cellpadding=2 cellspacing=0 data-column-types='tNK' data-init-sort='3'>
  @ <thead><tr>
  @ <th>User<th>Total Changes<th>Last Change</tr></thead>
  @ <tbody>
  zBaseUrl = mprintf("%R/timeline?y=%t&u=", PD("type","ci"));
  db_prepare(&s,
    "SELECT coalesce(euser,user),"
    "       count(*),"
    "       max(mtime)"
    "  FROM v_reports"
    " GROUP BY 1"
    " ORDER BY 3 DESC"
  );
  rNow = db_double(0.0, "SELECT julianday('now');");
  while( db_step(&s)==SQLITE_ROW ){
    const char *zUser = db_column_text(&s, 0);
    int cnt = db_column_int(&s, 1);
    double rMTime = db_column_double(&s,2);
    char *zAge = human_readable_age(rNow - rMTime);
    @ <tr>
    @ <td><a href='%s(zBaseUrl)%t(zUser)'>%h(zUser)</a>
    @ <td>%d(cnt)
    @ <td data-sortkey='%f(rMTime)' style='white-space:nowrap'>%s(zAge?zAge:"")
    @ </tr>
    fossil_free(zAge);
  }
  @ </tbody></table>
  db_finalize(&s);
}


/* Report types
*/
#define RPT_BYFILE    1
#define RPT_BYMONTH   2
#define RPT_BYUSER    3
#define RPT_BYWEEK    4
#define RPT_BYWEEKDAY 5
#define RPT_BYYEAR    6
#define RPT_LASTCHNG  7  /* Last change made for each user */
#define RPT_BYHOUR    8  /* hour-of-day */
#define RPT_NONE      0  /* None of the above */

/*
** WEBPAGE: reports
**
** Shows activity reports for the repository.
**
** Query Parameters:
**
**   view=REPORT_NAME  Valid REPORT_NAME values:
**                        * byyear
**                        * bymonth
**                        * byweek
**                        * byweekday
**                        * byhour
**                        * byuser
**                        * byfile
**                        * lastchng
**   user=NAME         Restricts statistics to the given user
**   type=TYPE         Restricts the report to a specific event type:
**                        * all (everything),
**                        * ci  (check-in)
**                        * m   (merge check-in),
**                        * n   (non-merge check-in)
**                        * f   (forum post)
**                        * w   (wiki page change)
**                        * t   (ticket change)
**                        * g   (tag added or removed)
**                     Defaulting to all event types.
**   from=DATETIME     Consider only events after this timestamp (requires to)
**   to=DATETIME       Consider only events before this timestamp (requires from)
**
**
** The view-specific query parameters include:
**
** view=byweek:
**
**   y=YYYY            The year to report (default is the server's
**                     current year).
*/
void stats_report_page(){
  const char *zView = P("view");     /* Which view/report to show. */
  int eType = RPT_NONE;              /* Numeric code for view/report to show */
  int i;                             /* Loop counter */
  const char *zUserName;             /* Name of user */
  const char *azView[16];            /* Drop-down menu of view types */
  static const struct {
    const char *zName;  /* Name of view= screen type */
    const char *zVal;   /* Value of view= query parameter */
    int eType;          /* Corresponding RPT_* define */
  } aViewType[] = {
     {  "File Changes","byfile",    RPT_BYFILE    },
     {  "Last Change", "lastchng",  RPT_LASTCHNG  },
     {  "By Month",    "bymonth",   RPT_BYMONTH   },
     {  "By User",     "byuser",    RPT_BYUSER    },
     {  "By Week",     "byweek",    RPT_BYWEEK    },
     {  "By Weekday",  "byweekday", RPT_BYWEEKDAY },
     {  "By Year",     "byyear",    RPT_BYYEAR    },
     {  "By Hour",     "byhour",    RPT_BYHOUR    },
  };
  static const char *const azType[] = {
     "a",  "All Changes",
     "ci", "Check-ins",
     "f",  "Forum Posts",
     "m",  "Merge check-ins",
     "n",  "Non-merge check-ins",
     "g",  "Tags",
     "e",  "Tech Notes",
     "t",  "Tickets",
     "w",  "Wiki"
  };

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  zUserName = P("user");
  if( zUserName==0 ) zUserName = P("u");
  if( zUserName && zUserName[0]==0 ) zUserName = 0;
  if( zView==0 ){
    zView = "byuser";
    cgi_replace_query_parameter("view","byuser");
  }
  for(i=0; i<count(aViewType); i++){
    if( fossil_strcmp(zView, aViewType[i].zVal)==0 ){
      eType = aViewType[i].eType;
      break;
    }
  }
  cgi_check_for_malice();
  if( eType!=RPT_NONE ){
    int nView = 0;                     /* Slots used in azView[] */
    for(i=0; i<count(aViewType); i++){
      azView[nView++] = aViewType[i].zVal;
      azView[nView++] = aViewType[i].zName;
    }
    if( eType!=RPT_BYFILE ){
      style_submenu_multichoice("type", count(azType)/2, azType, 0);
    }
    style_submenu_multichoice("view", nView/2, azView, 0);
    if( eType!=RPT_BYUSER && eType!=RPT_LASTCHNG ){
      style_submenu_sql("user","User:",
         "SELECT '', 'All Users' UNION ALL "
         "SELECT x, x FROM ("
         "  SELECT DISTINCT trim(coalesce(euser,user)) AS x FROM event %s"
         "  ORDER BY 1 COLLATE nocase) WHERE x!=''",
         eType==RPT_BYFILE ? "WHERE type='ci'" : ""
      );
    }
  }
  style_submenu_element("Stats", "%R/stat");
  style_header("Activity Reports");
  switch( eType ){
    case RPT_BYYEAR:
      stats_report_by_month_year(0, zUserName);
      break;
    case RPT_BYMONTH:
      stats_report_by_month_year(1, zUserName);
      break;
    case RPT_BYWEEK:
      stats_report_year_weeks(zUserName);
      break;
    default:
    case RPT_BYUSER:
      stats_report_by_user();
      break;
    case RPT_BYWEEKDAY:
      stats_report_day_of_week(zUserName);
      break;
    case RPT_BYFILE:
      stats_report_by_file(zUserName);
      break;
    case RPT_BYHOUR:
      stats_report_hour_of_day(zUserName);
      break;
    case RPT_LASTCHNG:
      stats_report_last_change();
      break;
  }
  style_finish_page();
}
