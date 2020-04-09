/* This javascript runs on web-pages that need to periodically send
** a keep-alive HTTP request back to the server.  This is typically
** used with the "fossil ui" command with a --idle-timeout set.  The
** HTTP server will stop if it does not receive a new HTTP request
** within some time interval (60 seconds).  This script keeps sending
** new HTTP requests every 20 seconds or so to keep the server running
** while the page is still viewable.
*/
setInterval(function(){fetch('/noop');},15000+10000*Math.random());
