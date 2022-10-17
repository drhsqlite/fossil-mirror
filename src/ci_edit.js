/* Javascript used to make the check-in edit screen more interactive.
*/
function chgcbn(){
  var newbr = document.getElementById('newbr');
  var brname = document.getElementById('brname');
  var checked = newbr.checked;
  var x = brname.value.trim();
  if( !x || !newbr.checked ) x = newbr.getAttribute('data-branch');
  if( newbr.checked ) brname.select();
  document.getElementById('hbranch').textContent = x;
  cidbrid = document.getElementById('cbranch');
  if( cidbrid ) cidbrid.textContent = x;
}
function chgbn(){
  var newbr = document.getElementById('newbr');
  var brname = document.getElementById('brname');
  var x = brname.value.trim();
  var br = newbr.getAttribute('data-branch');
  if( !x ) x = br;
  newbr.checked = (x!=br);
  document.getElementById('hbranch').textContent = x;
  cidbrid = document.getElementById('cbranch');
  if( cidbrid ) cidbrid.textContent = x;
}
function chgtn(){
  var newtag = document.getElementById('newtag');
  var tagname = document.getElementById('tagname');
  newtag.checked=!!tagname.value;
}
(function(){
  document.getElementById('newbr').onchange = chgcbn;
  document.getElementById('brname').onkeyup = chgbn;
  document.getElementById('tagname').onkeyup = chgtn;
}());
