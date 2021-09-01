/* Keeps the horizontal scrollbars* in sync on side-by-side diffs.
*/
(function(){
  var SCROLL_LEN = 25;
  function initDiff(diff){
    var txtCols = diff.querySelectorAll('td.difftxt');
    var txtPres = diff.querySelectorAll('td.difftxt pre');
    var width = Math.max(txtPres[0].scrollWidth, txtPres[1].scrollWidth);
    var i;
    for(i=0; i<txtCols.length; i++){
      txtCols[i].style.width = width + 'px';
      txtPres[i].style.maxWidth = width + 'px';
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
  var i, diffs = document.querySelectorAll('table.splitdiff')
  for(i=0; i<diffs.length; i++){
    initDiff(diffs[i]);
  }
  if(window.fossil && fossil.page){
    fossil.page.tweakSbsDiffs = function(){
      document.querySelectorAll('table.splitdiff').forEach(initDiff);
    };
  }
  var lastWidth = 0;
  function checkWidth(){
    if( document.body.clientWidth!=lastWidth ){
      lastWidth = document.body.clientWidth;
      var w = lastWidth*0.5 - 100;
      var allCols = document.querySelectorAll('td.difftxt pre');
      for(let i=0; i<allCols.length; i++){
        allCols[i].style.width = w + "px";
        allCols[i].style.maxWidth = w + "px";
      }
      var allDiffs = document.querySelectorAll('table.diff');
      w = lastWidth;
      for(let i=0; i<allDiffs.length; i++){
        allDiffs[i].style.width = '98%'; // setting to w causes unsightly horiz. scrollbar
        allDiffs[i].style.maxWidth = w + "px";
      }
    }
    setTimeout(checkWidth, 100)
  }
  checkWidth();
})();
