/* This module contains javascript needed to render timeline graphs in Fossil.
**
** Prior to sourcing this script, there should be a separate
** <script type='application/json' id='timeline-data'> containing JSON
** like this:
**
**   { "circleNodes": BOOLEAN,      // True for circle nodes. False for squares
**     "showArrowheads": BOOLEAN,   // True for arrowheads. False to omit
**     "iRailPitch": INTEGER,       // Spacing between vertical lines (px)
**     "colorGraph": BOOLEAN,       // True to put color on graph lines
**     "nomo": BOOLEAN,             // True to join merge lines with rails
**     "iTopRow": INTEGER,          // Index of top-most row in the graph
**     "omitDescenders": BOOLEAN,   // Omit ancestor lines off bottom of screen
**     "fileDiff": BOOLEAN,         // True for file diff. False for check-in
**     "nrail": INTEGER,            // Number of vertical "rails"
**     "baseUrl": TEXT,             // Top-level URL
**     "rowinfo": ROWINFO-ARRAY }
**
** The rowinfo field is an array of structures, one per entry in the timeline,
** where each structure has the following fields:
**
**   id:  The id of the <div> element for the row. This is an integer.
**        to get an actual id, prepend "m" to the integer.  The top node
**        is iTopRow and numbers increase moving down the tx.
**   bg:  The background color for this row
**    r:  The "rail" that the node for this row sits on.  The left-most
**        rail is 0 and the number increases to the right.
**    d:  True if there is a "descender" - an arrow coming from the bottom
**        of the page straight up to this node.
**   mo:  "merge-out".  If non-negative, this is the rail position
**        for the upward portion of a merge arrow.  The merge arrow goes up
**        to the row identified by mu:.  If this value is negative then
**        node has no merge children and no merge-out line is drawn.
**   mu:  The id of the row which is the top of the merge-out arrow.
**    u:  Draw a thick child-line out of the top of this node and up to
**        the node with an id equal to this value.  0 if it is straight to
**        the top of the page, -1 if there is no thick-line riser.
**    f:  0x01: a leaf node.
**   au:  An array of integers that define thick-line risers for branches.
**        The integers are in pairs.  For each pair, the first integer is
**        is the rail on which the riser should run and the second integer
**        is the id of the node upto which the riser should run.
**   mi:  "merge-in".  An array of integer rail positions from which
**        merge arrows should be drawn into this node.  If the value is
**        negative, then the rail position is the absolute value of mi[]
**        and a thin merge-arrow descender is drawn to the bottom of
**        the screen.
**    h:  The artifact hash of the object being graphed
*/
var timelineObj = document.getElementById("timeline-data");
var timelineJson = timelineObj.textContent || timelineObj.innerText;
var tx = JSON.parse(timelineJson);
var css = "";
if( tx.circleNodes ){
  css += ".tl-node, .tl-node:after { border-radius: 50%; }";
}
if( tx.showArrowheads ){
  css += ".tl-arrow.u { display: none; }";
}
if( css!=="" ){
  var style = document.createElement("style");
  style.textContent = css;
  document.querySelector("head").appendChild(style);
}
var canvasDiv;
var railPitch;
var mergeOffset;
var node, arrow, arrowSmall, line, mArrow, mLine, wArrow, wLine;
function initGraph(){
  var parent = document.getElementById("timelineTable").rows[0].cells[1];
  parent.style.verticalAlign = "top";
  canvasDiv = document.createElement("div");
  canvasDiv.className = "tl-canvas";
  canvasDiv.style.position = "absolute";
  parent.appendChild(canvasDiv);

  var elems = {};
  var elemClasses = [
    "rail", "mergeoffset", "node", "arrow u", "arrow u sm", "line",
    "arrow merge r", "line merge", "arrow warp", "line warp"
  ];
  for( var i=0; i<elemClasses.length; i++ ){
    var cls = elemClasses[i];
    var elem = document.createElement("div");
    elem.className = "tl-" + cls;
    if( cls.indexOf("line")==0 ) elem.className += " v";
    canvasDiv.appendChild(elem);
    var k = cls.replace(/\s/g, "_");
    var r = elem.getBoundingClientRect();
    var w = Math.round(r.right - r.left);
    var h = Math.round(r.bottom - r.top);
    elems[k] = {w: w, h: h, cls: cls};
  }
  node = elems.node;
  arrow = elems.arrow_u;
  arrowSmall = elems.arrow_u_sm;
  line = elems.line;
  mArrow = elems.arrow_merge_r;
  mLine = elems.line_merge;
  wArrow = elems.arrow_warp;
  wLine = elems.line_warp;

  var minRailPitch = Math.ceil((node.w+line.w)/2 + mArrow.w + 1);
  if( tx.iRailPitch>0 ){
    railPitch = tx.iRailPitch;
  }else{
    railPitch = elems.rail.w;
    railPitch -= Math.floor((tx.nrail-1)*(railPitch-minRailPitch)/21);
  }
  railPitch = Math.max(railPitch, minRailPitch);

  if( tx.nomo ){
    mergeOffset = 0;
  }else{
    mergeOffset = railPitch-minRailPitch-mLine.w;
    mergeOffset = Math.min(mergeOffset, elems.mergeoffset.w);
    mergeOffset = mergeOffset>0 ? mergeOffset + line.w/2 : 0;
  }

  var canvasWidth = (tx.nrail-1)*railPitch + node.w;
  canvasDiv.style.width = canvasWidth + "px";
  canvasDiv.style.position = "relative";
}
function drawBox(cls,color,x0,y0,x1,y1){
  var n = document.createElement("div");
  x0 = Math.floor(x0);
  y0 = Math.floor(y0);
  x1 = x1 || x1===0 ? Math.floor(x1) : x0;
  y1 = y1 || y1===0 ? Math.floor(y1) : y0;
  if( x0>x1 ){ var t=x0; x0=x1; x1=t; }
  if( y0>y1 ){ var t=y0; y0=y1; y1=t; }
  var w = x1-x0;
  var h = y1-y0;
  n.style.position = "absolute";
  n.style.left = x0+"px";
  n.style.top = y0+"px";
  if( w ) n.style.width = w+"px";
  if( h ) n.style.height = h+"px";
  if( color ) n.style.backgroundColor = color;
  n.className = "tl-"+cls;
  canvasDiv.appendChild(n);
  return n;
}
function absoluteY(obj){
  var top = 0;
  if( obj.offsetParent ){
    do{
      top += obj.offsetTop;
    }while( obj = obj.offsetParent );
  }
  return top;
}
function miLineY(p){
  return p.y + node.h - mLine.w - 1;
}
function drawLine(elem,color,x0,y0,x1,y1){
  var cls = elem.cls + " ";
  if( x1===null ){
    x1 = x0+elem.w;
    cls += "v";
  }else{
    y1 = y0+elem.w;
    cls += "h";
  }
  drawBox(cls,color,x0,y0,x1,y1);
}
function drawUpArrow(from,to,color){
  var y = to.y + node.h;
  var arrowSpace = from.y - y + (!from.id || from.r!=to.r ? node.h/2 : 0);
  var arw = arrowSpace < arrow.h*1.5 ? arrowSmall : arrow;
  var x = to.x + (node.w-line.w)/2;
  var y0 = from.y + node.h/2;
  var y1 = Math.ceil(to.y + node.h + arw.h/2);
  drawLine(line,color,x,y0,null,y1);
  x = to.x + (node.w-arw.w)/2;
  var n = drawBox(arw.cls,null,x,y);
  n.style.borderBottomColor = color;
}
function drawMergeLine(x0,y0,x1,y1){
  drawLine(mLine,null,x0,y0,x1,y1);
}
function drawMergeArrow(p,rail){
  var x0 = rail*railPitch + node.w/2;
  if( rail in mergeLines ){
    x0 += mergeLines[rail];
    if( p.r<rail ) x0 += mLine.w;
  }else{
    x0 += (p.r<rail ? -1 : 1)*line.w/2;
  }
  var x1 = mArrow.w ? mArrow.w/2 : -node.w/2;
  x1 = p.x + (p.r<rail ? node.w + Math.ceil(x1) : -x1);
  var y = miLineY(p);
  drawMergeLine(x0,y,x1,null);
  var x = p.x + (p.r<rail ? node.w : -mArrow.w);
  var cls = "arrow merge " + (p.r<rail ? "l" : "r");
  drawBox(cls,null,x,y+(mLine.w-mArrow.h)/2);
}
function drawNode(p, btm){
  if( p.u>0 ) drawUpArrow(p,tx.rowinfo[p.u-tx.iTopRow],p.fg);
  var cls = node.cls;
  if( p.mi.length ) cls += " merge";
  if( p.f&1 ) cls += " leaf";
  var n = drawBox(cls,p.bg,p.x,p.y);
  n.id = "tln"+p.id;
  n.onclick = clickOnNode;
  n.style.zIndex = 10;
  if( !tx.omitDescenders ){
    if( p.u==0 ) drawUpArrow(p,{x: p.x, y: -node.h},p.fg);
    if( p.d ) drawUpArrow({x: p.x, y: btm-node.h/2},p,p.fg);
  }
  if( p.mo>=0 ){
    var x0 = p.x + node.w/2;
    var x1 = p.mo*railPitch + node.w/2;
    var u = tx.rowinfo[p.mu-tx.iTopRow];
    var y1 = miLineY(u);
    if( p.u<0 || p.mo!=p.r ){
      x1 += mergeLines[p.mo] = -mLine.w/2;
      var y0 = p.y+2;
      if( p.r!=p.mo ) drawMergeLine(x0,y0,x1+(x0<x1 ? mLine.w : 0),null);
      drawMergeLine(x1,y0+mLine.w,null,y1);
    }else if( mergeOffset ){
      mergeLines[p.mo] = u.r<p.r ? -mergeOffset-mLine.w : mergeOffset;
      x1 += mergeLines[p.mo];
      drawMergeLine(x1,p.y+node.h/2,null,y1);
    }else{
      delete mergeLines[p.mo];
    }
  }
  for( var i=0; i<p.au.length; i+=2 ){
    var rail = p.au[i];
    var x0 = p.x + node.w/2;
    var x1 = rail*railPitch + (node.w-line.w)/2;
    if( x0<x1 ){
      x0 = Math.ceil(x0);
      x1 += line.w;
    }
    var y0 = p.y + (node.h-line.w)/2;
    var u = tx.rowinfo[p.au[i+1]-tx.iTopRow];
    if( u.id<p.id ){
      drawLine(line,u.fg,x0,y0,x1,null);
      drawUpArrow(p,u,u.fg);
    }else{
      var y1 = u.y + (node.h-line.w)/2;
      drawLine(wLine,u.fg,x0,y0,x1,null);
      drawLine(wLine,u.fg,x1-line.w,y0,null,y1+line.w);
      drawLine(wLine,u.fg,x1,y1,u.x-wArrow.w/2,null);
      var x = u.x-wArrow.w;
      var y = u.y+(node.h-wArrow.h)/2;
      var n = drawBox(wArrow.cls,null,x,y);
      if( u.fg ) n.style.borderLeftColor = u.fg;
    }
  }
  for( var i=0; i<p.mi.length; i++ ){
    var rail = p.mi[i];
    if( rail<0 ){
      rail = -rail;
      mergeLines[rail] = -mLine.w/2;
      var x = rail*railPitch + (node.w-mLine.w)/2;
      drawMergeLine(x,miLineY(p),null,btm);
    }
    drawMergeArrow(p,rail);
  }
}
var mergeLines;
function renderGraph(){
  mergeLines = {};
  canvasDiv.innerHTML = "";
  var canvasY = absoluteY(canvasDiv);
  for(var i=0; i<tx.rowinfo.length; i++ ){
    var e = document.getElementById("m"+tx.rowinfo[i].id);
    tx.rowinfo[i].y = absoluteY(e) - canvasY;
    tx.rowinfo[i].x = tx.rowinfo[i].r*railPitch;
  }
  var tlBtm = document.querySelector(".timelineBottom");
  if( tlBtm.offsetHeight<node.h ){
    tlBtm.style.height = node.h + "px";
  }
  var btm = absoluteY(tlBtm) - canvasY + tlBtm.offsetHeight;
  for( var i=tx.rowinfo.length-1; i>=0; i-- ){
    drawNode(tx.rowinfo[i], btm);
  }
}
var selRow;
function clickOnNode(){
  var p = tx.rowinfo[parseInt(this.id.match(/\d+$/)[0], 10)-tx.iTopRow];
  if( !selRow ){
    selRow = p;
    this.className += " sel";
    canvasDiv.className += " sel";
  }else if( selRow==p ){
    selRow = null;
    this.className = this.className.replace(" sel", "");
    canvasDiv.className = canvasDiv.className.replace(" sel", "");
  }else{
    if( tx.fileDiff ){
      location.href=tx.baseUrl + "/fdiff?v1="+selRow.h+"&v2="+p.h+"&sbs=1";
    }else{
/*
**      if( db_get_boolean("show-version-diffs", 0)==0 ){
**        @     location.href="%R/vdiff?from="+selRow.h+"&to="+p.h+"&sbs=0";
**      }else{
**        @     location.href="%R/vdiff?from="+selRow.h+"&to="+p.h+"&sbs=1";
**      }
*/
      location.href=tx.baseUrl+"/vdiff?from="+selRow.h+"&to="+p.h
    }
  }
}
function changeDisplay(selector,value){
  var x = document.getElementsByClassName(selector);
  var n = x.length;
  for(var i=0; i<n; i++) {x[i].style.display = value;}
}
function declutter(){
  changeDisplay('clutter','none');
  changeDisplay('anticlutter','inline');
  checkHeight();
}
function reclutter(){
  changeDisplay('clutter','inline');
  changeDisplay('anticlutter','none');
  checkHeight();
}
function changeDisplayById(id,value){
  var x = document.getElementById(id);
  if(x) x.style.display=value;
}
function toggleDetail(id){
  var x = gebi("detail-"+id);
  if( x.style.display=="inline" ){
    x.style.display="none";
    changeDisplayById("ellipsis-"+id,"inline");
    changeDisplayById("links-"+id,"none");
  }else{
    x.style.display="inline";
    changeDisplayById("ellipsis-"+id,"none");
    changeDisplayById("links-"+id,"inline");
  }
  checkHeight();
}
function scrollToSelected(){
  var x = document.getElementsByClassName('timelineSelected');
  if(x[0]){
    var h = window.innerHeight;
    var y = absoluteY(x[0]) - h/2;
    if( y>0 ) window.scrollTo(0, y);
  }
}
var lastRow = gebi("m"+tx.rowinfo[tx.rowinfo.length-1].id);
var lastY = 0;
function checkHeight(){
  var h = absoluteY(lastRow);
  if( h!=lastY ){
    renderGraph();
    lastY = h;
  }
  setTimeout(checkHeight, 1000);
}
initGraph();
checkHeight();
scrollToSelected();
