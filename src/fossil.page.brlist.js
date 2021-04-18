/*
 * This script adds multiselect facility for the list of branches.
*/
window.addEventListener( 'load', function() {

var anchor = document.querySelector("div.submenu > a.label" );
if( !anchor || anchor.innerText != "Timeline" ) return;
var prefix = anchor.href.toString() + "?ms=regexp&rel&t=";
anchor.classList.add('timeline-link');
const selectedCheckboxes = []/*currently-selected checkboxes*/;
var onChange = function( event ){
  const cbx = event.target;
  const tag = cbx.parentElement.children[0].innerText;
  var re = anchor.href.substr(prefix.length);
  if( cbx.checked ){
    if( re != "" ){
      re += "|";
    }
    re += tag;
    selectedCheckboxes.push(cbx);
    anchor.classList.add('selected'); 
  }else{
    const ndx = selectedCheckboxes.indexOf(cbx);
    if(ndx>=0){
      selectedCheckboxes.splice(ndx,1);
      if(!selectedCheckboxes.length){
        anchor.classList.remove('selected');
      }
    }
    if( re == tag ){
      re = "";
      removeSelected(cbx);
    }else {
      var a = re.split("|");
      var i = a.length;
      while( --i >= 0 ){
        if( a[i] == tag )
          a.splice(i,1);
      }
      re = a.join("|");
    }
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
