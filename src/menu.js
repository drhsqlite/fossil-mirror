/* This script runs when the submenu contains controls created by routines
** like style_submenu_checkbox() or style_submenu_multichoice() - controls
** that require javascript support.
*/

function toggle_annotation_log(){
  var w = document.getElementById("annotation_log");
  var x = document.forms["f01"].elements["log"].checked
  w.style.display = x ? "block" : "none";
}
function submenu_onchange_submit(){
  var w = document.getElementById("f01");
  w.submit();
}

(function (){
  for(var i=0; 1; i++){
    var x = document.getElementById("submenuctrl-"+i);
    if(!x) break;
    if( !x.hasAttribute('data-ctrl') ){
      x.onchange = submenu_onchange_submit;
    }else{
      var cx = x.getAttribute('data-ctrl');
      if( cx=="toggle_annotation_log" ){
        x.onchange = toggle_annotation_log;
      }
    }
  }
})();
