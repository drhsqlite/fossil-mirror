/* As an anti-robot defense, <a> elements are initially coded with the
** href= set to the honeypot, and <form> elements are initialized with
** action= set to the login page.  The real values for href= and action=
** are held in data-href= and data-action=.  The following code moves
** data-href= into href= and data-action= into action= for all
** <a> and <form> elements, after delay and maybe also after mouse
** movement is seen.
**
** Before sourcing this script, create a separate <script> element
** (with type='application/json' to avoid Content Security Policy issues)
** containing:
**
**     {"delay":MILLISECONDS, "mouseover":BOOLEAN}
**
** The <script> must have an id='href-data'.  DELAY is the number 
** milliseconds delay prior to populating href= and action=.  If the
** mouseover boolean is true, then the href= rewrite is further delayed
** until the first mousedown event that occurs after the timer expires.
*/
var antiRobotOnce = 0;
function antiRobotSetAllHrefs(){
  if( antiRobotOnce ) return;
  antiRobotOnce = 1;
  var anchors = document.getElementsByTagName("a");
  for(var i=0; i<anchors.length; i++){
    var j = anchors[i];
    if(j.hasAttribute("data-href")) j.href=j.getAttribute("data-href");
  }
  var forms = document.getElementsByTagName("form");
  for(var i=0; i<forms.length; i++){
    var j = forms[i];
    if(j.hasAttribute("data-action")) j.action=j.getAttribute("data-action");
  }
}
function antiRobotSetMouseEventHandler(){
  document.getElementsByTagName("body")[0].onmousedown=function(){
    antiRobotSetAllHrefs();
    document.getElementsByTagName("body")[0].onmousedown=null;
  }
}
function antiRobotDefense(){
  var x = document.getElementById("href-data");
  var jx = x.textContent || x.innerText;
  var g = JSON.parse(jx);
  if( g.mouseover ){
    setTimeout(antiRobotSetMouseEventHandler, g.delay);
  }else{
    setTimeout(antiRobotSetAllHrefs, g.delay);
  }
}
antiRobotDefense();
