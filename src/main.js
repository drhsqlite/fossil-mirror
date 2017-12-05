/* This script is sourced just prior to the </body> in every Fossil webpage */
var x = document.getElementById("page-data");
var jx = x.textContent || x.innerText;
var g = JSON.parse(jx);

/* As an anti-robot defense, <a> elements are initially coded with the
** href= set to the honeypot, and <form> elements are initialized with
** action= set to the login page.  The real values for href= and action=
** are held in data-href= and data-action=.  The following code moves
** data-href= into href= and data-action= into action= for all
** <a> and <form> elements, after delay and maybe also after mouse
** movement is seen.
*/
function setAllHrefs(){
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
if(g.antibot.enable){
  var isOperaMini =
       Object.prototype.toString.call(window.operamini)==="[object OperaMini]";
  if(g.antibot.mouseover && !isOperaMini){
    document.getElementByTagName("body")[0].onmousemove=function(){
      setTimeout("setAllHrefs();",g.antibot.delay);
    }
  }else{
    setTimeout("setAllHrefs();",g.antibot.delay);
  }
}
