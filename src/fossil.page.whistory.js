/* This script adds interactivity for wiki-history webpages.
 *
 * The main code is within the 'on-click' handler of the "diff" links.
 * Instead of standard redirection it fills-in two hidden inputs with
 * the appropriate values and submits the corresponding form.
 * A special care should be taken if some intermediate edits are hidden.
 *
 * For the sake of compatibility with ascetic browsers the code tries
 * to avoid modern API and ECMAScript constructs. This makes it less
 * readable and may be reconsidered in the future.
*/
window.addEventListener( 'load', function() {

var form = document.getElementById("wh-form");
form.method = "GET";
var csrf = form.querySelector("input[name='csrf']");
if( csrf ) form.removeChild( csrf );

var wh_id  = document.getElementById("wh-id" );
var wh_pid = document.getElementById("wh-pid");
var wh_cleaner = document.getElementById("wh-cleaner");
var wh_collapser = document.getElementById("wh-collapser");

var wh_radios   = [];  // user-visible controls for baseline selection
var wh_hidden   =  0;  // current number of hidden (collapsed) rows
var wh_selected = -1;  // index of the currently selected radio-button

var wh_onRadio = function( event ){

  var indx = event.target.indx;
  if( wh_selected == indx ){

    wh_selected  = -1;
    event.target.checked = false;
  }
  else wh_selected = indx;
}
var wh_onDifflink = function( event ){

  event.preventDefault();
  var indx = event.target.indx;
  wh_id.value = wh_radios[indx].value;

  if( wh_hidden > 0 ){

    var p = indx + 1;
    if( wh_selected >= 0 ){

       var tr = wh_radios[wh_selected].parentElement.parentElement;
       if( ! tr.hidden )
          p = wh_selected;
    }
    while( p < wh_radios.length ){

      if( ! wh_radios[p].parentElement.parentElement.hidden )
	     break;
      p++;
    }
    if( p < wh_radios.length ){

       wh_pid.value = wh_radios[p].value;
       wh_pid.checked = true;
    }
    else {  // just render the wiki for the case of the first major edit

      var path = document.location.pathname.split("/");
      path.pop();
      var newpath = path.join("/") + "/info/" + wh_radios[indx].value;
      document.location = document.location.origin + newpath;
      return;
    }
  }
  else if( wh_selected >= 0 ) {

     wh_pid.value = wh_radios[wh_selected].value;
     wh_pid.checked = true;
  }
  else wh_pid.checked = false;

  document.getElementById("wh-form").submit();
}
var wh_onCleaner = function() {

   if( wh_selected >= 0 ) {

      wh_radios[wh_selected].checked = false;
      wh_selected = -1;
   }
}
var wh_onCollapser = function( event ){

  var collapsing = ( wh_hidden == 0 );
  for( var k = 0; k < wh_radios.length; k++ ){

    var radio = wh_radios[k];
    var tr = radio.parentElement.parentElement;
    if( tr.className == "wh-intermediate" ){

	  if( tr.hidden = ! tr.hidden )
	    wh_hidden++;
	  else
	    wh_hidden--;

    } else if( radio.iterspan )
               radio.iterspan.hidden = ! collapsing;
  }
  if( wh_hidden > 0 ) {

    wh_collapser.title="Show intermediate edits";
    wh_collapser.innerHTML = "&emsp;&#9851; " + wh_hidden;
  }
  else {

    wh_collapser.title="Hide intermediate edits";
    wh_collapser.innerHTML = "&emsp;&#9842;"
  }
}

var inputs = document.getElementsByTagName("input");
for( var k = 0, indx = 0; k < inputs.length; k++ ) {

   var r = inputs[k];
   if( r.type == "radio" && r.name == "baseline" ) {

      wh_radios.push( r );
	  r.indx = indx++;
      r.addEventListener( "click", wh_onRadio );
      r.disabled = false;
      var td = r.parentElement.nextElementSibling;
      r.iterspan = td.getElementsByTagName("span")[0];
   }
}
for( var edits = 0, k = wh_radios.length - 1; k >= 0; k-- ) {

   var td = wh_radios[k].parentElement.nextElementSibling;
   if( td.parentElement.className == "wh-intermediate" )

      edits++;

   else if( edits > 0 ){

      var span = td.getElementsByTagName("span")[0];
      span.innerHTML = "&ensp;&#9842;" + edits;
      wh_radios[k].iterspan = span;
      edits = 0;
      //   also:  &#8746; (union)   &#931; (sigma)   &#215; (times)
   }
}
var links = document.getElementsByTagName("a");
for( var i = 0, indx = 0; i < links.length; i++ ) {

   var l = links[i];
   if( l.className == "wh-difflink" ){

      l.indx = indx++;
      l.addEventListener( "click", wh_onDifflink );
   }
}
wh_cleaner.addEventListener( "click", wh_onCleaner );
wh_collapser.addEventListener( "click", wh_onCollapser );
wh_collapser.title="Hide intermediate edits";
wh_collapser.hidden = false;

}); // window.addEventListener( 'load' ...
