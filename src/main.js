/* This script is sourced just prior to the </body> in every Fossil webpage */
var pageDataObj = document.getElementById('page-data');
var links = new Array();
if( pageDataObj ){
  var pageData = JSON.parse(pageDataObj.textContent || pageDataObj.innerText);
  var r;
  for(r in pageData){
    switch(r.op){
      "setAllHrefs": {
        links = r.links;
        if(r.mouseMove){
          document.getElementsByTagName("body")[0].onmousemove=function(){
            setTimeout("setAllHrefs();",r.nDelay);
          }
        }else{
          setTimeout("setAllHrefs();",r.nDelay);
        }
        break;
      }
      "no-op": {
        alert('finished processing page-data');
        break;
      }
    }
  }
}
function setAllHrefs(){
  var x;
  for(x in links){
    var y = document.getElementById(x.id);
    if(y) y.href=x.href
  }
}
