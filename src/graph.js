/* This module contains javascript needed to render timeline graphs in Fossil.
**
** There can be multiple graphs on a single webpage, but this script is only
** loaded once.  
**
** Prior to sourcing this script, there should be a separate
** <script type='application/json' id='timeline-data-NN'> for each graph,
** each containing JSON like this:
**
**   { "iTableId": INTEGER,         // Table sequence number (NN)
**     "circleNodes": BOOLEAN,      // True for circle nodes. False for squares
**     "showArrowheads": BOOLEAN,   // True for arrowheads. False to omit
**     "iRailPitch": INTEGER,       // Spacing between vertical lines (px)
**     "nomo": BOOLEAN,             // True to join merge lines with rails
**     "iTopRow": INTEGER,          // Index of top-most row in the graph
**     "omitDescenders": BOOLEAN,   // Omit ancestor lines off bottom of screen
**     "fileDiff": BOOLEAN,         // True for file diff. False for check-in
**     "scrollToSelect": BOOLEAN,   // Scroll to selection on first render
**     "nrail": INTEGER,            // Number of vertical "rails"
**     "baseUrl": TEXT,             // Top-level URL
**     "dwellTimeout": INTEGER,     // Tooltip show delay in milliseconds
**     "closeTimeout": INTEGER,     // Tooltip close delay in milliseconds
**     "hashDigits": INTEGER,       // Limit of tooltip hashes ("hash-digits")
**     "rowinfo": ROWINFO-ARRAY }
**
** The rowinfo field is an array of structures, one per entry in the timeline,
** where each structure has the following fields:
**
**   id:  The id of the <div> element for the row. This is an integer.
**        to get an actual id, prepend "m" to the integer.  The top node
**        is iTopRow and numbers increase moving down the timeline.
**   bg:  The background color for this row
**    r:  The "rail" that the node for this row sits on.  The left-most
**        rail is 0 and the number increases to the right.
**    d:  If exists and true then there is a "descender" - an arrow
**        coming from the bottom of the page straight up to this node.
**   mo:  "merge-out".  If it exists, this is the rail position
**        for the upward portion of a merge arrow.  The merge arrow goes as
**        a solid normal merge line up to the row identified by "mu" and
**        then as a dashed cherrypick merge line up further to "cu".
**        If this value is omitted if there are no merge children.
**   mu:  The id of the row which is the top of the merge-out arrow.
**        Only exists if "mo" exists.
**   cu:  Extend the mu merge arrow up to this row as a cherrypick
**        merge line, if this value exists.
**    u:  Draw a thick child-line out of the top of this node and up to
**        the node with an id equal to this value.  0 if it is straight to
**        the top of the page, -1 if there is no thick-line riser.
**    f:  0x01: a leaf node.
**   au:  An array of integers that define thick-line risers for branches.
**        The integers are in pairs.  For each pair, the first integer is
**        is the rail on which the riser should run and the second integer
**        is the id of the node upto which the riser should run. If there
**        are no risers, this array does not exist.
**   mi:  "merge-in".  An array of integer rail positions from which
**        merge arrows should be drawn into this node.  If the value is
**        negative, then the rail position is -1-mi[] and a thin merge-arrow
**        descender is drawn to the bottom of the screen. This array is
**        omitted if there are no inbound merges.
**   ci:  "cherrypick-in". Like "mi" except for cherrypick merges.
**        omitted if there are no cherrypick merges.
**    h:  The artifact hash of the object being graphed
*/
/* The amendCss() function does a one-time change to the CSS to account
** for the "circleNodes" and "showArrowheads" settings.  Do this change
** only once, even if there are multiple graphs being rendered.
*/
var amendCssOnce = 1; // Only change the CSS one time
function amendCss(circleNodes,showArrowheads){
  if( !amendCssOnce ) return;
  var css = "";
  if( circleNodes ){
    css += ".tl-node, .tl-node:after { border-radius: 50%; }";
  }
  if( !showArrowheads ){
    css += ".tl-arrow.u { display: none; }";
  }
  if( css!=="" ){
    var style = document.createElement("style");
    style.textContent = css;
    document.querySelector("head").appendChild(style);
  }
  amendCssOnce = 0;
}

/* The <span> object that holds the tooltip */
var tooltipObj = document.createElement("span");
tooltipObj.className = "tl-tooltip";
tooltipObj.style.display = "none";
document.getElementsByClassName("content")[0].appendChild(tooltipObj);
tooltipObj.onmouseenter = function(){
  /* Hold the tooltip constant as long as the mouse is over the tooltip.
  ** In other words, do not let any of the timers changes the tooltip while
  ** the mouse is directly over the tooltip.  This makes it easier for the
  ** user to move over top of the "copy-button" or the hyperlink to the
  ** /info page. */
  stopCloseTimer();
  stopDwellTimer();
  tooltipInfo.ixHover = tooltipInfo.ixActive;
}
tooltipObj.onmouseleave = function(){
  if (tooltipInfo.ixActive != -1) resumeCloseTimer();
};

/* State information for the tooltip popup and its timers */
window.tooltipInfo = {
  dwellTimeout: 250,  /* The tooltip dwell timeout. */
  closeTimeout: 3000, /* The tooltip close timeout. */
  hashDigits: 16,     /* Limit of tooltip hashes ("hash-digits"). */
  idTimer: 0,         /* The tooltip dwell timer id */
  idTimerClose: 0,    /* The tooltip close timer id */
  ixHover: -1,        /* The mouse is over a thick riser arrow for
                      ** tx.rowinfo[ixHover].  Or -2 when the mouse is
                      ** over a graph node.  Or -1 when the mouse is not
                      ** over anything. */
  ixActive: -1,       /* The item shown in the tooltip is tx.rowinfo[ixActive].
                      ** ixActive is -1 if the tooltip is not visible */
  nodeHover: null,    /* Graph node under mouse when ixHover==-2 */
  idNodeActive: 0,    /* Element ID of the graph node with the tooltip. */
  posX: 0, posY: 0    /* The last mouse position. */
};

/* Functions used to control the tooltip popup and its timer */
function onKeyDown(event){  /* Hide the tooltip when ESC key pressed */
  var key = event.which || event.keyCode;
  if( key==27 ){
    event.stopPropagation();
    hideGraphTooltip();
  }
}
function hideGraphTooltip(){ /* Hide the tooltip */
  document.removeEventListener('keydown',onKeyDown,/* useCapture == */true);
  stopCloseTimer();
  tooltipObj.style.display = "none";
  tooltipInfo.ixActive = -1;
  tooltipInfo.idNodeActive = 0;
}
window.onpagehide = hideGraphTooltip;
function stopDwellTimer(){
  if(tooltipInfo.idTimer!=0){
    clearTimeout(tooltipInfo.idTimer);
    tooltipInfo.idTimer = 0;
  }
}
function resumeCloseTimer(){
  /* This timer must be stopped explicitly to reset the elapsed timeout. */
  if(tooltipInfo.idTimerClose==0 && tooltipInfo.closeTimeout>0) {
    tooltipInfo.idTimerClose = setTimeout(function(){
      tooltipInfo.idTimerClose = 0;
      hideGraphTooltip();
    },tooltipInfo.closeTimeout);
  }
}
function stopCloseTimer(){
  if(tooltipInfo.idTimerClose!=0){
    clearTimeout(tooltipInfo.idTimerClose);
    tooltipInfo.idTimerClose = 0;
  }
}

/* Construct that graph corresponding to the timeline-data-N object that
** is passed in by the tx parameter */
function TimelineGraph(tx){
  var topObj = document.getElementById("timelineTable"+tx.iTableId);
  amendCss(tx.circleNodes, tx.showArrowheads);
  tooltipInfo.dwellTimeout = tx.dwellTimeout
  tooltipInfo.closeTimeout = tx.closeTimeout
  tooltipInfo.hashDigits = tx.hashDigits
  topObj.onclick = clickOnGraph
  topObj.ondblclick = dblclickOnGraph
  topObj.onmousemove = function(e) {
    var ix = findTxIndex(e);
    topObj.style.cursor = (ix<0) ? "" : "pointer"
    mouseOverGraph(e,ix,null);
  };
  topObj.onmouseleave = function(e) {
    /* Hide the tooltip if the mouse is outside the "timelineTableN" element,
    ** and outside the tooltip. */
    if(e.relatedTarget && e.relatedTarget != tooltipObj){
      tooltipInfo.ixHover = -1;
      hideGraphTooltip();
      stopDwellTimer();
      stopCloseTimer();
    }
  };
  function mouseOverNode(e){ /* Invoked by mousemove events over a graph node */
    e.stopPropagation()
    mouseOverGraph(e,-2,this)
  }
  /* Combined mousemove handler for graph nodes and rails. */
  function mouseOverGraph(e,ix,node){
    stopDwellTimer();                 // Mouse movement: reset the dwell timer.
    var ownTooltip =   // Check if the hovered element already has the tooltip.
      (ix>=0 && ix==tooltipInfo.ixActive) ||
      (ix==-2 && tooltipInfo.idNodeActive==node.id);
    if(ownTooltip) stopCloseTimer();  // ownTooltip: clear the close timer.
    else resumeCloseTimer();          // !ownTooltip: resume the close timer.
    tooltipInfo.ixHover = ix;
    tooltipInfo.nodeHover = node;
    tooltipInfo.posX = e.clientX;
    tooltipInfo.posY = e.clientY;
    if(ix!=-1 && !ownTooltip && tooltipInfo.dwellTimeout>0){  // Go dwell timer.
      tooltipInfo.idTimer = setTimeout(function(){
        tooltipInfo.idTimer = 0;
        stopCloseTimer();
        showGraphTooltip();
      },tooltipInfo.dwellTimeout);
    }
  }
  var canvasDiv;
  var railPitch;
  var mergeOffset;
  var node, arrow, arrowSmall, line, mArrow, mLine, wArrow, wLine;

  function initGraph(){
    var parent = topObj.rows[0].cells[1];
    parent.style.verticalAlign = "top";
    canvasDiv = document.createElement("div");
    canvasDiv.className = "tl-canvas";
    canvasDiv.style.position = "absolute";
    parent.appendChild(canvasDiv);
  
    var elems = {};
    var elemClasses = [
      "rail", "mergeoffset", "node", "arrow u", "arrow u sm", "line",
      "arrow merge r", "line merge", "arrow warp", "line warp",
      "line cherrypick", "line dotted"
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
    cpLine = elems.line_cherrypick;
    wArrow = elems.arrow_warp;
    wLine = elems.line_warp;
    dotLine = elems.line_dotted;
  
    var minRailPitch = Math.ceil((node.w+line.w)/2 + mArrow.w + 1);
    if( window.innerWidth<400 ){
      railPitch = minRailPitch;
    }else{
      if( tx.iRailPitch>0 ){
        railPitch = tx.iRailPitch;
      }else{
        railPitch = elems.rail.w;
        railPitch -= Math.floor((tx.nrail-1)*(railPitch-minRailPitch)/21);
      }
      railPitch = Math.max(railPitch, minRailPitch);
    }
  
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
    var y = 0;
    do{
      y += obj.offsetTop;
    }while( obj = obj.offsetParent );
    return y;
  }
  function absoluteX(obj){
    var x = 0;
    do{
      x += obj.offsetLeft;
    }while( obj = obj.offsetParent );
    return x;
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
    return drawBox(cls,color,x0,y0,x1,y1);
  }
  function drawUpArrow(from,to,color,id){
    var y = to.y + node.h;
    var arrowSpace = from.y - y + (!from.id || from.r!=to.r ? node.h/2 : 0);
    var arw = arrowSpace < arrow.h*1.5 ? arrowSmall : arrow;
    var x = to.x + (node.w-line.w)/2;
    var y0 = from.y + node.h/2;
    var y1 = Math.ceil(to.y + node.h + arw.h/2);
    var n = drawLine(line,color,x,y0,null,y1);
    addToolTip(n,id)
    x = to.x + (node.w-arw.w)/2;
    n = drawBox(arw.cls,null,x,y);
    if(color) n.style.borderBottomColor = color;
    addToolTip(n,id)
  }
  function drawDotted(from,to,color,id){
    var x = to.x + (node.w-line.w)/2;
    var y0 = from.y + node.h/2;
    var y1 = Math.ceil(to.y + node.h);
    var n = drawLine(dotLine,null,x,y0,null,y1)
    if( color ) n.style.borderColor = color
    addToolTip(n,id)
  }
  function addToolTip(n,id){
    if( id ) n.setAttribute("data-ix",id-tx.iTopRow)
  }
  /* Draw thin horizontal or vertical lines representing merges */
  function drawMergeLine(x0,y0,x1,y1){
    drawLine(mLine,null,x0,y0,x1,y1);
  }
  function drawCherrypickLine(x0,y0,x1,y1){
    drawLine(cpLine,null,x0,y0,x1,y1);
  }
  /* Draw an arrow representing an in-bound merge from the "rail"-th rail
  ** over to the node of "p".  Make it a checkpoint merge is "isCP" is true */
  function drawMergeArrow(p,rail,isCP){
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
    var x = p.x + (p.r<rail ? node.w : -mArrow.w);
    var cls;
    if( isCP ){
      drawCherrypickLine(x0,y,x1,null);
      cls = "arrow cherrypick " + (p.r<rail ? "l" : "r");
    }else{
      drawMergeLine(x0,y,x1,null);
      cls = "arrow merge " + (p.r<rail ? "l" : "r");
    }
    drawBox(cls,null,x,y+(mLine.w-mArrow.h)/2);
  }
  function drawNode(p, btm){
    if( p.bg ){
      var e = document.getElementById("mc"+p.id);
      if(e) e.style.backgroundColor = p.bg;
      e = document.getElementById("md"+p.id);
      if(e) e.style.backgroundColor = p.bg;
    }
    if( p.r<0 ) return;
    if( p.u>0 ) drawUpArrow(p,tx.rowinfo[p.u-tx.iTopRow],p.fg,p.id);
    if( p.sb>0 ) drawDotted(p,tx.rowinfo[p.sb-tx.iTopRow],p.fg,p.id);
    var cls = node.cls;
    if( p.hasOwnProperty('mi') && p.mi.length ) cls += " merge";
    if( p.f&2 ) cls += " closed-leaf";
    else if( p.f&1 ) cls += " leaf";
    var n = drawBox(cls,p.bg,p.x,p.y);
    n.id = "tln"+p.id;
    n.onclick = clickOnNode;
    n.ondblclick = dblclickOnNode;
    n.onmousemove = mouseOverNode;
    n.style.zIndex = 10;
    if( p.f&2 ){
      var pt1 = 0;
      var pt2 = 100;
      if( tx.circleNodes ){
        pt1 = 14;
        pt2 = 86;
      }
      n.innerHTML = "<svg width='100%' height='100%'viewbox='0 0 100 100'>"
          + `<path d='M ${pt1},${pt1} L ${pt2},${pt2} M ${pt1},${pt2} L ${pt2},${pt1}'`
          + " stroke='currentcolor' stroke-width='13'/>"
          + "</svg>";
    }
    if( !tx.omitDescenders ){
      if( p.u==0 ){
        if( p.hasOwnProperty('mo') && p.r==p.mo ){
          var ix = p.hasOwnProperty('cu') ? p.cu : p.mu;
          var top = tx.rowinfo[ix-tx.iTopRow]
          drawUpArrow(p,{x: p.x, y: top.y-node.h}, p.fg, p.id);
        }else if( p.y>100 ){
          drawUpArrow(p,{x: p.x, y: p.y-50}, p.fg, p.id);
        }else{
          drawUpArrow(p,{x: p.x, y: 0},p.fg, p.id);
        }
      }
      if( p.hasOwnProperty('d') ){
        if( p.y + 150 >= btm ){
          drawUpArrow({x: p.x, y: btm - node.h/2},p,p.fg,p.id);
        }else{
          drawUpArrow({x: p.x, y: p.y+50},p,p.fg,p.id);
          drawDotted({x: p.x, y: p.y+63},{x: p.x, y: p.y+50-node.h/2},p.fg,p.id);
        }
      }
    }
    if( p.hasOwnProperty('mo') ){
      var x0 = p.x + node.w/2;
      var x1 = p.mo*railPitch + node.w/2;
      var u = tx.rowinfo[p.mu-tx.iTopRow];
      var mtop = u;
      if( p.hasOwnProperty('cu') ){
        mtop = tx.rowinfo[p.cu-tx.iTopRow];
      }
      var y1 = miLineY(u);
      if( p.u<=0 || p.mo!=p.r ){
        if( p.u==0 && p.mo==p.r ){
          mergeLines[p.mo] = mtop.r<p.r ? -mergeOffset-mLine.w : mergeOffset;
        }else{
          mergeLines[p.mo] = -mLine.w/2;
        }
        x1 += mergeLines[p.mo]
        var y0 = p.y+2;
        var isCP = p.hasOwnProperty('cu');
        if( p.mu==p.id ){
          /* Special case:  The merge riser already exists.  Only draw the
          /* horizontal line or arrow going from the node out to the riser. */
          var dx = x1<x0 ? mArrow.w : -mArrow.w;
          if( isCP ){
            drawCherrypickLine(x0,y0,x1+dx,null);
            cls = "arrow cherrypick " + (x1<x0 ? "l" : "r");
          }else{
            drawMergeLine(x0,y0,x1+dx,null);
            cls = "arrow merge " + (x1<x0 ? "l" : "r");
          }
          if( !isCP || p.mu==p.cu ){
            dx = x1<x0 ? mLine.w : -(mArrow.w + mLine.w/2);
            drawBox(cls,null,x1+dx,y0+(mLine.w-mArrow.h)/2);
          }
          y1 = y0;
        }else{
          drawMergeLine(x0,y0,x1+(x0<x1 ? mLine.w : 0),null);
          drawMergeLine(x1,y0+mLine.w,null,y1);
        }
        if( isCP && p.cu!=p.id ){
          var u2 = tx.rowinfo[p.cu-tx.iTopRow];
          var y2 = miLineY(u2);
          drawCherrypickLine(x1,y1,null,y2);
        }
      }else if( mergeOffset ){
        mergeLines[p.mo] = mtop.r<p.r ? -mergeOffset-mLine.w : mergeOffset;
        x1 += mergeLines[p.mo];
        if( p.mu<p.id ){
          drawMergeLine(x1,p.y+node.h/2,null,y1);
        }
        if( p.hasOwnProperty('cu') ){
          var u2 = tx.rowinfo[p.cu-tx.iTopRow];
          var y2 = miLineY(u2);
          drawCherrypickLine(x1,y1,null,y2);
        }
      }else{
        delete mergeLines[p.mo];
      }
    }
    if( p.hasOwnProperty('au') ){
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
          // normal thick up-arrow
          drawLine(line,u.fg,x0,y0,x1,null);
          drawUpArrow(p,u,u.fg,u.id);
        }else{
          // timewarp:  The child node occurs before the parent
          var y1 = u.y + (node.h-line.w)/2;
          var n = drawLine(wLine,u.fg,x0,y0,x1,null);
          addToolTip(n,u.id)
          n = drawLine(wLine,u.fg,x1-line.w,y0,null,y1+line.w);
          addToolTip(n,u.id)
          n = drawLine(wLine,u.fg,x1,y1,u.x-wArrow.w/2,null);
          addToolTip(n,u.id)
          var x = u.x-wArrow.w;
          var y = u.y+(node.h-wArrow.h)/2;
          n = drawBox(wArrow.cls,null,x,y);
          addToolTip(n,u.id)
          if( u.fg ) n.style.borderLeftColor = u.fg;
        }
      }
    }
    if( p.hasOwnProperty('mi') ){
      for( var i=0; i<p.mi.length; i++ ){
        var rail = p.mi[i];
        if( rail<0 ){
          rail = -1-rail;
          mergeLines[rail] = -mLine.w/2;
          var x = rail*railPitch + (node.w-mLine.w)/2;
          var y = miLineY(p);
          drawMergeLine(x,y,null,mergeBtm[rail]);
          mergeBtm[rail] = y;
        }
        drawMergeArrow(p,rail,0);
      }
    }
    if( p.hasOwnProperty('ci') ){
      for( var i=0; i<p.ci.length; i++ ){
        var rail = p.ci[i];
        if( rail<0 ){
          rail = -rail;
          mergeLines[rail] = -mLine.w/2;
          var x = rail*railPitch + (node.w-mLine.w)/2;
          var y = miLineY(p);
          drawCherrypickLine(x,y,null,mergeBtm[rail]);
          mergeBtm[rail] = y;
        }
        drawMergeArrow(p,rail,1);
      }
    }
  }
  var mergeLines;
  var mergeBtm = new Array;
  function renderGraph(){
    mergeLines = {};
    canvasDiv.innerHTML = "";
    var canvasY = absoluteY(canvasDiv);
    for(var i=0; i<tx.rowinfo.length; i++ ){
      var e = document.getElementById("m"+tx.rowinfo[i].id);
      tx.rowinfo[i].y = absoluteY(e) - canvasY;
      tx.rowinfo[i].x = tx.rowinfo[i].r*railPitch;
    }
    var tlBtm = document.getElementById(tx.bottomRowId);
    if( tlBtm.offsetHeight<node.h ){
      tlBtm.style.height = node.h + "px";
    }
    var btm = absoluteY(tlBtm) - canvasY + tlBtm.offsetHeight;
    for( var i=0; i<tx.nrail; i++) mergeBtm[i] = btm;
    for( var i=tx.rowinfo.length-1; i>=0; i-- ){
      drawNode(tx.rowinfo[i], btm);
    }
  }
  var selRow;
  function clickOnNode(e){
    hideGraphTooltip()
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
        location.href=tx.baseUrl + "/fdiff?v1="+selRow.h+"&v2="+p.h;
      }else{
        var href = tx.baseUrl + "/vdiff?from="+selRow.h+"&to="+p.h;
        let params = (new URL(document.location)).searchParams;
        if(params && typeof params === "object"){
          /* When called from /timeline page, If chng=str was specified in the
          ** QueryString, specify glob=str on the /vdiff page */
          let glob = params.get("chng");
          if( !glob ){
            /* When called from /vdiff page, keep the glob= QueryString if
            ** present. */
            glob = params.get("glob");
          }
          if( glob ){
            href += "&glob=" + glob;
          }
        }
        location.href = href;
      }
    }
    e.stopPropagation()
  }
  function dblclickOnNode(e){
    var p = tx.rowinfo[parseInt(this.id.match(/\d+$/)[0], 10)-tx.iTopRow];
    window.location.href = tx.baseUrl+"/info/"+p.h
    e.stopPropagation()
  }
  function findTxIndex(e){
    if( !tx.rowinfo ) return -1;
    /* Look at all the graph elements.  If any graph elements that is near
    ** the click-point "e" and has a "data-ix" attribute, then return
    ** the value of that attribute.  Otherwise return -1 */
    var x = e.clientX + window.pageXOffset - absoluteX(canvasDiv);
    var y = e.clientY + window.pageYOffset - absoluteY(canvasDiv);
    var aNode = canvasDiv.childNodes
    var nNode = aNode.length;
    var i;
    for(i=0;i<nNode;i++){
      var n = aNode[i]
      if( !n.hasAttribute("data-ix") ) continue;
      if( x<n.offsetLeft-5 ) continue;
      if( x>n.offsetLeft+n.offsetWidth+5 ) continue;
      if( y<n.offsetTop-5 ) continue;
      if( y>n.offsetTop+n.offsetHeight ) continue;
      return n.getAttribute("data-ix")
    }
    return -1
  }
  /* Compute the hyperlink for the branch graph for tx.rowinfo[ix] */
  function branchHyperlink(ix){
    var br = tx.rowinfo[ix].br
    var dest = tx.baseUrl + "/timeline?r=" + encodeURIComponent(br)
    dest += tx.fileDiff ? "&m&cf=" : "&m&c="
    dest += encodeURIComponent(tx.rowinfo[ix].h)
    return dest
  }
  function clickOnGraph(e){
    stopCloseTimer();
    stopDwellTimer();
    tooltipInfo.ixHover = findTxIndex(e);
    tooltipInfo.posX = e.clientX;
    tooltipInfo.posY = e.clientY;
    showGraphTooltip();
  }
  function showGraphTooltip(){
    var html = null
    var ix = -1
    if( tooltipInfo.ixHover==-2 ){
      ix = parseInt(tooltipInfo.nodeHover.id.match(/\d+$/)[0],10)-tx.iTopRow
      var h = tx.rowinfo[ix].h
      var dest = tx.baseUrl + "/info/" + h
      h = h.slice(0,tooltipInfo.hashDigits); // Assume single-byte characters.
      if( tx.fileDiff ){
        html = "artifact <a id=\"tooltip-link\" href=\""+dest+"\">"+h+"</a>"
      }else{
        html = "check-in <a id=\"tooltip-link\" href=\""+dest+"\">"+h+"</a>"
      }
      tooltipInfo.ixActive = -2;
      tooltipInfo.idNodeActive = tooltipInfo.nodeHover.id;
    }else if( tooltipInfo.ixHover>=0 ){
      ix = tooltipInfo.ixHover
      var br = tx.rowinfo[ix].br
      var dest = branchHyperlink(ix)
      var hbr = br.replace(/&/g, "&amp;")
         .replace(/</g, "&lt;")
         .replace(/>/g, "&gt;")
         .replace(/"/g, "&quot;")
         .replace(/'/g, "&#039;");
      html = "branch <a id=\"tooltip-link\" href=\""+dest+"\">"+hbr+"</a>"
      tooltipInfo.ixActive = ix;
      tooltipInfo.idNodeActive = 0;
    }
    if( html ){
      /* Setup while hidden, to ensure proper dimensions. */
      var s = getComputedStyle(document.body)
      if( tx.rowinfo[ix].bg.length ){
        tooltipObj.style.backgroundColor = tx.rowinfo[ix].bg
      }else{
        tooltipObj.style.backgroundColor = s.getPropertyValue('background-color')
      }
      tooltipObj.style.borderColor =
         tooltipObj.style.color = s.getPropertyValue('color')
      tooltipObj.style.visibility = "hidden"
      tooltipObj.innerHTML = html
      tooltipObj.insertBefore(makeCopyButton("tooltip-link",0,0),
                              tooltipObj.childNodes[1]);
      tooltipObj.style.display = "inline"
      tooltipObj.style.position = "absolute"
      var x = tooltipInfo.posX + 4 + window.pageXOffset
                   - absoluteX(tooltipObj.offsetParent)
      tooltipObj.style.left = x+"px"
      var y = tooltipInfo.posY + window.pageYOffset
                   - tooltipObj.clientHeight - 4
                   - absoluteY(tooltipObj.offsetParent)
      tooltipObj.style.top = y+"px"
      tooltipObj.style.visibility = "visible"
      document.addEventListener('keydown',onKeyDown,/* useCapture == */true);
    }else{
      hideGraphTooltip()
    }
  }
  function dblclickOnGraph(e){
    var ix = findTxIndex(e);
    hideGraphTooltip()
    if( ix>=0 ){
      var dest = branchHyperlink(ix)
      window.location.href = dest
    }
  }
  function changeDisplay(selector,value){
    var x = document.getElementsByClassName(selector);
    var n = x.length;
    for(var i=0; i<n; i++) {x[i].style.display = value;}
  }
  function changeDisplayById(id,value){
    var x = document.getElementById(id);
    if(x) x.style.display=value;
  }
  function toggleDetail(){
    var id = parseInt(this.getAttribute('data-id'))
    var x = document.getElementById("detail-"+id);
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
  if( tx.rowinfo ){
    var lastRow = 
       document.getElementById("m"+tx.rowinfo[tx.rowinfo.length-1].id);
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
  }else{
    function checkHeight(){}
  }
  if( tx.scrollToSelect ){
    scrollToSelected();
  }

  /* Set the onclick= attributes for elements of the "Compact" display
  ** mode so that clicking turns the details on and off.
  */
  var lx = topObj.getElementsByClassName('timelineEllipsis');
  var i;
  for(i=0; i<lx.length; i++){
    if( lx[i].hasAttribute('data-id') ) lx[i].onclick = toggleDetail;
  }
  lx = topObj.getElementsByClassName('timelineCompactComment');
  for(i=0; i<lx.length; i++){
    if( lx[i].hasAttribute('data-id') ) lx[i].onclick = toggleDetail;
  }
  if( window.innerWidth<400 ){
    /* On narrow displays, shift the date from the first column to the
    ** third column, to make the first column narrower */
    lx = topObj.getElementsByClassName('timelineDateRow');
    for(i=0; i<lx.length; i++){
      var rx = lx[i];
      if( rx.getAttribute('data-reordered') ) break;
      rx.setAttribute('data-reordered',1);
      rx.appendChild(rx.firstChild);
      rx.insertBefore(rx.childNodes[1],rx.firstChild);
    }
    /* Do not show the HH:MM timestamps on very narrow displays
    ** as they take up too much horizontal space. */
    lx = topObj.getElementsByClassName('timelineHistLink');
    for(i=0; i<lx.length; i++){
      var rx = lx[i];
      rx.style.display="none";
    }
  }
}
  
/* Look for all timeline-data-NN objects.  Load each one and draw
** a graph for each one.
*/
(function(){
  var i;
  for(i=0; 1; i++){
    var dataObj = document.getElementById("timeline-data-"+i);
    if(!dataObj) break;
    var txJson = dataObj.textContent || dataObj.innerText;
    var tx = JSON.parse(txJson);
    TimelineGraph(tx);
  }
}());
