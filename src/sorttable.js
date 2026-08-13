/* Javascript code that will enables sorting of the table.  This code is
** derived from
**
**     http://www.webtoolkit.info/sortable-html-table.html
**
** but with extensive modifications.
**
** All tables with class "sortable" are registered with the SortableTable()
** function.  Example:
**
**   <table class='sortable' data-column-types='tnkx' data-init-sort='2'>
**
** Column data types are determined by the data-column-types attribute of
** the table.  The value of data-column-types is a string where each
** character of the string represents a datatype for one column in the
** table.
**
**       t      Sort by text
**       n      Sort numerically
**       k      Sort by the data-sortkey property
**       x      This column is not sortable
**
** Capital letters mean sort in reverse order.
** If there are fewer characters in zColumnTypes[] than their are columns,
** then all extra columns assume type "t" (text).
**
** If a column of the table is initially sorted, then the data-init-sort
** attribute should be set to the 1-based index of that column.  (The
** left-most column is 1, the next column to the right is 2, and so forth.)
** A value of 0 in the data-init-sort attribute indicates that no columns
** where initially sorted.
**
** Clicking on the same column header twice in a row inverts the sort.
*/
function SortableTable(tableEl){
  var columnTypes = tableEl.getAttribute("data-column-types");
  var initSort = tableEl.getAttribute("data-init-sort");
  this.tbody = tableEl.getElementsByTagName('tbody');
  this.columnTypes = columnTypes;
  if(tableEl.rows.length==0) return;
  var ncols = tableEl.rows[0].cells.length;
  for(var i = columnTypes.length; i<=ncols; i++){this.columnTypes += 't';}
  this.sort = function (cell) {
    var column = cell.cellIndex;
    var sortFn;
    switch( cell.sortType ){
      case "n": sortFn = this.sortNumeric;  break;
      case "N": sortFn = this.sortReverseNumeric;  break;
      case "t": sortFn = this.sortText;  break;
      case "T": sortFn = this.sortReverseText;  break;
      case "k": sortFn = this.sortKey;  break;
      case "K": sortFn = this.sortReverseKey;  break;
      default:  return;
    }
    this.sortIndex = column;
    var newRows = new Array();
    for (j = 0; j < this.tbody[0].rows.length; j++) {
       newRows[j] = this.tbody[0].rows[j];
    }
    if( this.sortIndex==Math.abs(this.prevColumn)-1 ){
      newRows.reverse();
      this.prevColumn = -this.prevColumn;
    }else{
      newRows.sort(sortFn);
      this.prevColumn = this.sortIndex+1;
    }
    for (i=0;i<newRows.length;i++) {
      this.tbody[0].appendChild(newRows[i]);
    }
    this.setHdrIcons();
  }
  this.setHdrIcons = function() {
    for (var i=0; i<this.hdrRow.cells.length; i++) {
      if( this.columnTypes[i]=='x' ) continue;
      var sortType;
      if( this.prevColumn==i+1 ){
        sortType = 'asc';
      }else if( this.prevColumn==(-1-i) ){
        sortType = 'desc'
      }else{
        sortType = 'none';
      }
      var hdrCell = this.hdrRow.cells[i];
      var clsName = hdrCell.className.replace(/\s*\bsort\s*\w+/, '');
      clsName += ' sort ' + sortType;
      hdrCell.className = clsName;
    }
  }
  this.sortText = function(a,b) {
    var i = thisObject.sortIndex;
    if (a.cells.length<=i) return -1; /* see ticket 59d699710b1ab5d4 */
    if (b.cells.length<=i) return 1;
    aa = a.cells[i].textContent.replace(/^\W+/,'').toLowerCase();
    bb = b.cells[i].textContent.replace(/^\W+/,'').toLowerCase();
    if(aa<bb) return -1;
    if(aa==bb) return a.rowIndex-b.rowIndex;
    return 1;
  }
  this.sortReverseText = function(a,b) {
    var i = thisObject.sortIndex;
    if (a.cells.length<=i) return 1; /* see ticket 59d699710b1ab5d4 */
    if (b.cells.length<=i) return -1;
    aa = a.cells[i].textContent.replace(/^\W+/,'').toLowerCase();
    bb = b.cells[i].textContent.replace(/^\W+/,'').toLowerCase();
    if(aa<bb) return +1;
    if(aa==bb) return a.rowIndex-b.rowIndex;
    return -1;
  }
  this.sortNumeric = function(a,b) {
    var i = thisObject.sortIndex;
    aa = parseFloat(a.cells[i].textContent);
    if (isNaN(aa)) aa = 0;
    bb = parseFloat(b.cells[i].textContent);
    if (isNaN(bb)) bb = 0;
    if(aa==bb) return a.rowIndex-b.rowIndex;
    return aa-bb;
  }
  this.sortReverseNumeric = function(a,b) {
    var i = thisObject.sortIndex;
    aa = parseFloat(a.cells[i].textContent);
    if (isNaN(aa)) aa = 0;
    bb = parseFloat(b.cells[i].textContent);
    if (isNaN(bb)) bb = 0;
    if(aa==bb) return a.rowIndex-b.rowIndex;
    return bb-aa;
  }
  this.sortKey = function(a,b) {
    var i = thisObject.sortIndex;
    aa = a.cells[i].getAttribute("data-sortkey");
    bb = b.cells[i].getAttribute("data-sortkey");
    if(aa<bb) return -1;
    if(aa==bb) return a.rowIndex-b.rowIndex;
    return 1;
  }
  this.sortReverseKey = function(a,b) {
    var i = thisObject.sortIndex;
    aa = a.cells[i].getAttribute("data-sortkey");
    bb = b.cells[i].getAttribute("data-sortkey");
    if(aa<bb) return +1;
    if(aa==bb) return a.rowIndex-b.rowIndex;
    return -1;
  }
  var x = tableEl.getElementsByTagName('thead');
  if(!(this.tbody && this.tbody[0].rows && this.tbody[0].rows.length>0)){
    return;
  }
  if(x && x[0].rows && x[0].rows.length > 0) {
    this.hdrRow = x[0].rows[0];
  } else {
    return;
  }
  var thisObject = this;
  this.prevColumn = initSort;
  for (var i=0; i<this.hdrRow.cells.length; i++) {
    if( columnTypes[i]=='x' ) continue;
    var hdrcell = this.hdrRow.cells[i];
    hdrcell.sTable = this;
    hdrcell.style.cursor = "pointer";
    hdrcell.sortType = columnTypes[i] || 't';
    hdrcell.onclick = function () {
      this.sTable.sort(this);
      return false;
    }
  }
  this.setHdrIcons()
}
(function(){
  var x = document.getElementsByClassName("sortable");
  for(var i=0; i<x.length; i++){
    new SortableTable(x[i]);
  }
}())
