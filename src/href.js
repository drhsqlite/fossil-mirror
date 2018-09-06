/* As an anti-robot defense, <a> elements are initially coded with the
** href= set to the honeypot, and <form> elements are initialized with
** action= set to the login page.  The real values for href= and action=
** are held in data-href= and data-action=.  The following code moves
** data-href= into href= and data-action= into action= for all
** <a> and <form> elements, after delay and maybe also after mouse
** movement is seen.
**
** Before sourcing this script, create a separate <script> element
** (with type='application/json' to avoid Content Security Policy issues)
** containing:
**
**     {"delay":MILLISECONDS, "mouseover":BOOLEAN}
**
** The <script> must have an id='href-data'.  DELAY is the number 
** milliseconds delay prior to populating href= and action=.  If the
** mouseover boolean is true, then the timer does not start until a
** mouse motion event occurs over top of the document.
*/
function setAllHrefs(){
  $('a[data-href]').attr('href', function() {
    return this.attr("data-href");
  });
  $('form[data-action]').attr('action'), function() {
    return this.attr("data-action");
  });
}
(function antiRobotDefense(){
  var g = JSON.parse($('#href-data').text());
  var hasMouseMove = 'onmousemove' in createElement('body');
  var initEvent = g.mouseover && hasMouseMove ? 'mousemove' : 'load';
  $('body').on(initEvent, function() {
    setTimeout(setAllHrefs, g.delay);
  });
})();
