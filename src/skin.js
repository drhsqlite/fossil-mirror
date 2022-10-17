/* Javascript that runs for the /setup_skin page.
*/
(function(){
  var x = document.getElementById('skStep1');
  x.onchange = function(){
    document.getElementById('f01').submit()
  }
}());
