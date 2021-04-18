/*
 * This script adds multiselect facility for the list of branches.
*/
window.addEventListener( 'load', function() {

var anchor = document.querySelector("div.submenu > a.label" );
if( !anchor || anchor.innerText != "Timeline" ) return;
var prefix = anchor.href.toString() + "?ms=regexp&rel&t=";

var onChange = function( event ){
  var cbx = event.target;
  var tag = cbx.parentElement.children[0].innerText;
  var re = anchor.href.substr(prefix.length);
  if( cbx.checked ){
    if( re != "" ){
      re += "|";
    }
    re += tag;
  }else if( re == tag ){
    re = ""
  }else {
      var a = re.split("|");
      var i = a.length;
      while( --i >= 0 ){
        if( a[i] == tag )
          a.splice(i,1);
      }
      re = a.join("|");
  }
  anchor.href = prefix + re;
}

var selected = [];
document.querySelectorAll("div.brlist > table td:first-child > input")
  .forEach( function( cbx ){
    cbx.onchange = onChange;
    cbx.disabled = false;
    if( cbx.checked )
      selected.push(cbx.parentElement.children[0].innerText);
  });
anchor.href = selected.length != 0 ? prefix + selected.join("|") : "#";
}); // window.addEventListener( 'load' ...
