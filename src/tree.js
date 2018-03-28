/* Javascript to implement the file hierarchy tree.
*/
(function(){
function isExpanded(ul){
  return ul.className=='';
}

function toggleDir(ul, useInitValue){
  if( !useInitValue ){
    expandMap[ul.id] = !isExpanded(ul);
    history.replaceState(expandMap, '');
  }
  ul.className = expandMap[ul.id] ? '' : 'collapsed';
}

function toggleAll(tree, useInitValue){
  var lists = tree.querySelectorAll('.subdir > ul > li ul');
  if( !useInitValue ){
    var expand = true;  /* Default action: make all sublists visible */
    for( var i=0; lists[i]; i++ ){
      if( isExpanded(lists[i]) ){
        expand = false; /* Any already visible - make them all hidden */
        break;
      }
    }
    expandMap = {'*': expand};
    history.replaceState(expandMap, '');
  }
  var className = expandMap['*'] ? '' : 'collapsed';
  for( var i=0; lists[i]; i++ ){
    lists[i].className = className;
  }
}

function checkState(){
  expandMap = history.state || {};
  if( '*' in expandMap ) toggleAll(outer_ul, true);
  for( var id in expandMap ){
    if( id!=='*' ) toggleDir(document.getElementById(id), true);
  }
}

function belowSubdir(node){
  do{
    node = node.parentNode;
    if( node==subdir ) return true;
  } while( node && node!=outer_ul );
  return false;
}

var history = window.history || {};
if( !history.replaceState ) history.replaceState = function(){};
var outer_ul = document.querySelector('.filetree > ul');
var subdir = outer_ul.querySelector('.subdir');
var expandMap = {};
checkState();
outer_ul.onclick = function(e){
  e = e || window.event;
  var a = e.target || e.srcElement;
  if( a.nodeName!='A' ) return true;
  if( a.parentNode.parentNode==subdir ){
    toggleAll(outer_ul);
    return false;
  }
  if( !belowSubdir(a) ) return true;
  var ul = a.parentNode.nextSibling;
  while( ul && ul.nodeName!='UL' ) ul = ul.nextSibling;
  if( !ul ) return true; /* This is a file link, not a directory */
  toggleDir(ul);
  return false;
}
}())
