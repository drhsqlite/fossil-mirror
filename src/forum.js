(function(){
  function absoluteY(obj){
    var top = 0;
    if( obj.offsetParent ){
      do{
        top += obj.offsetTop;
      }while( obj = obj.offsetParent );
    }
    return top;
  }
  var x = document.getElementsByClassName('forumSel');
  if(x[0]){
    var w = window.innerHeight;
    var h = x[0].scrollHeight;
    var y = absoluteY(x[0]);
    if( w>h ) y = y + (h-w)/2;
    if( y>0 ) window.scrollTo(0, y);
  }
})();
