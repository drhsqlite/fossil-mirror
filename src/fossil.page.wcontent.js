/* This script implements interactivity of checkboxes that
 * toggle visibilitiy of user-defined classes of wikipage.
 *
 * For the sake of compatibility with ascetic browsers the code tries
 * to avoid modern API and ECMAScript constructs. This makes it less
 * readable and may be reconsidered in the future.
*/
window.addEventListener( 'load', function() {

var tbody = document.querySelector(
            "body.wiki div.content table.sortable > tbody");
var prc = document.getElementById("page-reload-canary");
if( !tbody || !prc ) return;

var reloading = prc.checked;
// console.log("Reloading:",reloading);

var onChange = function(event){
  var display = event.target.checked ? "" : "none";
  var rows    = event.target.matchingRows;
  for(var i=0; i<rows.length; i++)
    rows[i].style.display = display;
}
var checkboxes = [];
document.querySelectorAll(
  "body.wiki .submenu > label.submenuckbox > input")
  .forEach(function(cbx){ checkboxes.push(cbx); });

for(var j=0; j<checkboxes.length; j++){
  var cbx = checkboxes[j];
  var ctrl = cbx.getAttribute("data-ctrl").toString();
  var cname = cbx.parentElement.innerText.toString();
  var hidden = ( ctrl == 'h' || ctrl == 'd' );
  if( reloading )
    hidden = !cbx.checked;
  else
    cbx.checked = !hidden;
  cbx.matchingRows = [];
  tbody.querySelectorAll("tr."+cname).forEach(function (tr){
    tr.style.display = ( hidden ? "none" : "" );
    cbx.matchingRows.push(tr);
  });
  cbx.addEventListener("change", onChange ); 
  // console.log( cbx.matchingRows.length, cname, ctrl );
}

prc.checked = true;
}); // window.addEventListener( 'load' ...
