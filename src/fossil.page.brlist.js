/*
 * This script adds multiselect facility for the list of branches.
 *
 * Some info on 'const':
 *   https://caniuse.com/const
 *   https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/const#browser_compatibility
 *
 * According to MDN 'const' requires Android's WebView 37,
 * which may not be available.
 * For the time being, continueing without 'const' and 'indexOf'
 * (but that may be reconsidered later).
*/
window.addEventListener( 'load', function() {

var submenu = document.querySelector("div.submenu");
var anchor = document.createElement("A");
var brlistDataObj = document.getElementById("brlist-data");
var brlistDataTxt = brlistDataObj.textContent || brlistDataObj.innerText;
var brlistData = JSON.parse(brlistDataTxt);
anchor.classList.add("label");
anchor.classList.add("timeline-link");
anchor.href = brlistData.timelineUrl;
var prefix   = anchor.href.toString() + "?ms=brlist&t=";
submenu.insertBefore(anchor,submenu.childNodes[0]);

var amendAnchor = function( selected ){
  if( selected.length == 0 ){
    anchor.classList.remove('selected');
    anchor.href = prefix;
    return;
  }
  re = selected.join(",");
  try{re = encodeURIComponent(re);}
  catch{console.log("encodeURIComponent() failed for ",re);}
  anchor.href = prefix + re;
  anchor.innerHTML = "View " + selected.length +
                     ( selected.length > 1 ? " branches" : " branch" );
  anchor.classList.add('selected');
}

var onChange = function( event ){
  var cbx = event.target;
  var tr  = cbx.parentElement.parentElement;
  var tag = cbx.parentElement.children[0].innerText;
  var re  = anchor.href.substr(prefix.length);
  try{re  = decodeURIComponent(re);}
  catch{console.log("decodeURIComponent() failed for ",re);}
  var selected = ( re != "" ? re.split(",") : [] );
  if( cbx.checked ){
    selected.push(tag);
    tr.classList.add('selected');
  }
  else {
    tr.classList.remove('selected');
    for( var i = selected.length; --i >= 0 ;)
      if( selected[i] == tag )
        selected.splice(i,1);
  }
  amendAnchor( selected );
}

var stags = []; /* initially selected tags, not used above */
document.querySelectorAll("div.brlist > table td:first-child > input")
  .forEach( function( cbx ){
    cbx.onchange = onChange;
    cbx.disabled = false;
    if( cbx.checked ){
      stags.push(cbx.parentElement.children[0].innerText);
      cbx.parentElement.parentElement.classList.add('selected');
    }
  });
amendAnchor( stags );

}); // window.addEventListener( 'load' ...
