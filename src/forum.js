(function(){
  "use strict";
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

  // Set up click handlers for "in reply to" links...
  var respondeeSelectClass = 'forumSelReplyTo'
  /* CSS class to apply to selected "in reply to" post. */;
  var responseLinkClick = function(){
    /** This <A> tag has an href in the form /something#post-{UID},
        and post-{UID} is the ID of a forum post DIV on this
        page. Here we fish that ID out of this anchor,
        unmark any currently-selected DIV, and mark this anchor's
        corresponding DIV.
    */
    var m = /#post-\w+/.exec(this.href);
    if(!m || !m.length) return /*unexpected*/;
    // Remove respondeeSelectClass from all entries...
    document.querySelectorAll('.forumPost.'+respondeeSelectClass)
      .forEach(function(e){
        e.classList.remove(respondeeSelectClass);
      });
    // Add respondeeSelectClass to the matching entry...
    document.querySelectorAll(m[0])
      .forEach(function(e){
        e.classList.add(respondeeSelectClass);
      });
  };
  document.querySelectorAll('a[href*="#post-"]')
    .forEach(function(e){
      e.addEventListener( "click", responseLinkClick, false );
    });
})();
