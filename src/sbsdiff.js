/* The javascript in this file was added by Joel Bruick on 2013-07-06,
** originally as in-line javascript.  It does some kind of setup for
** side-by-side diff display, but I'm not really sure what.
*/
(function(){
  var SCROLL_LEN = 25;
  function initSbsDiff(diff){
    var txtCols = diff.querySelectorAll('.difftxtcol');
    var txtPres = diff.querySelectorAll('.difftxtcol pre');
    var width = Math.max(txtPres[0].scrollWidth, txtPres[1].scrollWidth);
    for(var i=0; i<2; i++){
      txtPres[i].style.width = width + 'px';
      txtCols[i].onscroll = function(e){
        txtCols[0].scrollLeft = txtCols[1].scrollLeft = this.scrollLeft;
      };
    }
    diff.tabIndex = 0;
    diff.onkeydown = function(e){
      e = e || event;
      var len = {37: -SCROLL_LEN, 39: SCROLL_LEN}[e.keyCode];
      if( !len ) return;
      txtCols[0].scrollLeft += len;
      return false;
    };
  }
  var diffs = document.querySelectorAll('.sbsdiffcols');
  for(var i=0; i<diffs.length; i++){
    initSbsDiff(diffs[i]);
  }
}())
