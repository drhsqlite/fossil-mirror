/**
    This file provides a JS interface into the core functionality of
    JSON-centric back-ends. It sends GET or JSON POST requests to
    a back-end and expects JSON responses. The exact semantics of
    the underlying back-end and overlying front-end are not its concern,
    and it leaves the interpretation of the data up to the client/server
    insofar as possible.

    All functionality is part of a class named WhAjaj, and that class
    acts as namespace for this framework.

    Author: Stephan Beal (http://wanderinghorse.net/home/stephan/)

    License: Public Domain

    This framework is directly derived from code originally found in
    http://code.google.com/p/jsonmessage, and later in
    http://whiki.wanderinghorse.net, where it contained quite a bit
    of application-specific logic. It was eventually (the 3rd time i
    needed it) split off into its own library to simplify inclusion
    into my many mini-projects.
*/


/**
    The WhAjaj function is primarily a namespace, and not intended
    to called or instantiated via the 'new' operator.
*/
function WhAjaj()
{
}

/** Returns a millisecond Unix Epoch timestamp. */
WhAjaj.msTimestamp = function()
{
    return (new Date()).getTime();
};

/** Returns a Unix Epoch timestamp (in seconds) in integer format.

    Reminder to self: (1.1 %1.2) evaluates to a floating-point value
    in JS, and thus this implementation is less than optimal.
*/
WhAjaj.unixTimestamp = function()
{
    var ts = (new Date()).getTime();
    return parseInt( ""+((ts / 1000) % ts) );
};

/**
    Returns true if v is-a Array instance.
*/
WhAjaj.isArray = function( v )
{
    return (v &&
            (v instanceof Array) ||
            (Object.prototype.toString.call(v) === "[object Array]")
            );
    /* Reminders to self:
        typeof [] == "object"
        toString.call([]) == "[object Array]"
        ([]).toString() == empty
    */
};

/**
    Returns true if v is-a Object instance.
*/
WhAjaj.isObject = function( v )
{
    return v &&
        (v instanceof Object) &&
        ('[object Object]' === Object.prototype.toString.apply(v) );
};

/**
    Returns true if v is-a Function instance.
*/
WhAjaj.isFunction = function(obj)
{
    return obj
    && (
    (obj instanceof Function)
    || ('function' === typeof obj)
    || ("[object Function]" === Object.prototype.toString.call(obj))
    )
    ;
};

/**
    Parses window.location.search-style string into an object
    containing key/value pairs of URL arguments (already urldecoded).

    If the str argument is not passed (arguments.length==0) then
    window.location.search.substring(1) is used by default. If
    neither str is passed in nor window exists then false is returned.

    On success it returns an Object containing the key/value pairs
    parsed from the string. Keys which have no value are treated
    has having the boolean true value.

    FIXME: for keys in the form "name[]", build an array of results,
    like PHP does.

*/
WhAjaj.processUrlArgs = function(str) {
    if( 0 === arguments.length ) {
        if( ('undefined' === typeof window) ||
            !window.location ||
            !window.location.search )  return false;
        else str = (''+window.location.search).substring(1);
    }
    if( ! str ) return false;
    str = (''+str).split(/#/,2)[0]; // remove #... to avoid it being added as part of the last value.
    var args = {};
    var sp = str.split(/&+/);
    var rx = /^([^=]+)(=(.+))?/;
    var i, m;
    for( i in sp ) {
        m = rx.exec( sp[i] );
        if( ! m ) continue;
        args[decodeURIComponent(m[1])] = (m[3] ? decodeURIComponent(m[3]) : true);
    }
    return args;
};

/**
    A simple wrapper around JSON.stringify(), using my own personal
    preferred values for the 2nd and 3rd parameters. To globally
    set its indentation level, assign WhAjaj.stringify.indent to
    an integer value (0 for no intendation).

    This function is intended only for human-readable output, not
    generic over-the-wire JSON output (where JSON.stringify(val) will
    produce smaller results).
*/
WhAjaj.stringify = function(val) {
    if( ! arguments.callee.indent ) arguments.callee.indent = 4;
    return JSON.stringify(val,0,arguments.callee.indent);
};

/**
    Each instance of this class holds state information for making
    AJAJ requests to a back-end system. While clients may use one
    "requester" object per connection attempt, for connections to the
    same back-end, using an instance configured for that back-end
    can simplify usage. This class is designed so that the actual
    connection-related details (i.e. _how_ it connects to the
    back-end) may be re-implemented to use a client's preferred
    connection mechanism (e.g. jQuery).

    The optional opt paramater may be an object with any (or all) of
    the properties documented for WhAjaj.Connector.options.ajax.
    Properties set here (or later via modification of the "options"
    property of this object) will be used in calls to
    WhAjaj.Connector.sendRequest(), and these override (normally) any
    options set in WhAjaj.Connector.options.ajax. Note that
    WhAjaj.Connector.sendRequest() _also_ takes an options object,
    and ones passed there will override, for purposes of that one
    request, any options passed in here or defined in
    WhAjaj.Connector.options.ajax. See WhAjaj.Connector.options.ajax
    and WhAjaj.Connector.prototype.sendRequest() for more details
    about the precedence of options.

    Sample usage:

    @code
    // Set up common connection-level options:
    var cgi = new WhAjaj.Connector({
        url: '/cgi-bin/my.cgi',
        timeout:10000,
        onResponse(resp,req) { alert(JSON.stringify(resp,0.4)); },
        onError(req,opt) {
            alert(opt.errorMessage);
        }
    });
    // Any of those options may optionally be set globally in
    // WhAjaj.Connector.options.ajax (onError(), beforeSend(), and afterSend()
    // are often easiest/most useful to set globally).

    // Get list of pages...
    cgi.sendRequest( null, {
        onResponse(resp,req){ alert(WhAjaj.stringify(resp)); }
    });
    @endcode

    For common request types, clients can add functions to this
    object which act as wrappers for backend-specific functionality. As
    a simple example:

    @code
    cgi.login = function(name,pw,ajajOpt) {
        this.sendRequest(
            {command:"json/login",
              name:name,
              password:pw
            }, ajajOpt );
    };
    @endcode

    TODOs:

    - Caching of page-load requests, with a configurable lifetime.

    - Use-cases like the above login() function are a tiny bit
    problematic to implement when each request has a different URL
    path (i know this from the whiki and fossil implementations).
    This is partly a side-effect of design descisions made back in
    the very first days of this code's life. i need to go through
    and see where i can bend those conventions a bit (where it won't
    break my other apps unduly).
*/
WhAjaj.Connector = function(opt)
{
    if(WhAjaj.isObject(opt)) this.options = opt;
    //TODO?: this.$cache = {};
};

/**
    The core options used by WhAjaj.Connector instances for performing
    network operations. These options can (and some _should_)
    be changed by a client application. They can also be changed
    on specific instances of WhAjaj.Connector, but for most applications
    it is simpler to set them here and not have to bother with configuring
    each WhAjaj.Connector instance. Apps which use multiple back-ends at one time,
    however, will need to customize each instance for a given back-end.
*/
WhAjaj.Connector.options = {
    /**
        A (meaningless) prefix to apply to WhAjaj.Connector-generated
        request IDs.
    */
    requestIdPrefix:'WhAjaj.Connector-',
    /**
        Default options for WhAjaj.Connector.sendRequest() connection
        parameters. This object holds only connection-related
        options and callbacks (all optional), and not options
        related to the required JSON structure of any given request.
        i.e. the page name used in a get-page request are not set
        here but are specified as part of the request object.

        These connection options are a "normalized form" of options
        often found in various AJAX libraries like jQuery,
        Prototype, dojo, etc. This approach allows us to swap out
        the real connection-related parts by writing a simple proxy
        which transforms our "normalized" form to the
        backend-specific form. For examples, see the various
        implementations stored in WhAjaj.Connector.sendImpls.

        The following callback options are, in practice, almost
        always set globally to some app-wide defaults:

        - onError() to report errors using a common mechanism.
        - beforeSend() to start a visual activity notification
        - afterSend() to disable the visual activity notification

        However, be aware that if any given WhAjaj.Connector instance is
        given its own before/afterSend callback then those will
        override these. Mixing shared/global and per-instance
        callbacks can potentially lead to confusing results if, e.g.,
        the beforeSend() and afterSend() functions have side-effects
        but are not used with their proper before/after partner.

        TODO: rename this to 'ajaj' (the name is historical). The
        problem with renaming it is is that the word 'ajax' is
        pretty prevelant in the source tree, so i can't globally
        swap it out.
    */
    ajax: {
        /**
            URL of the back-end server/CGI.
        */
        url: '/some/path',

        /**
            Connection method. Some connection-related functions might
            override any client-defined setting.

            Must be one of 'GET' or 'POST'. For custom connection
            implementation, it may optionally be some
            implementation-specified value.

            Normally the API can derive this value automatically - if the
            request uses JSON data it is POSTed, else it is GETted.
        */
        method:'GET',

        /**
            A hint whether to run the operation asynchronously or
            not. Not all concrete WhAjaj.Connector.sendImpl()
            implementations can support this. Interestingly, at
            least one popular AJAX toolkit does not document
            supporting _synchronous_ AJAX operations. All common
            browser-side implementations support async operation, but
            non-browser implementations might not.
        */
        asynchronous:true,

        /**
            A HTTP authentication login name for the AJAX
            connection. Not all concrete WhAjaj.Connector.sendImpl()
            implementations can support this.
        */
        loginName:undefined,

        /**
            An HTTP authentication login password for the AJAJ
            connection. Not all concrete WhAjaj.Connector.sendImpl()
            implementations can support this.
        */
        loginPassword:undefined,

        /**
            A connection timeout, in milliseconds, for establishing
            an AJAJ connection. Not all concrete
            WhAjaj.Connector.sendImpl() implementations can support this.
        */
        timeout:10000,

        /**
            If an AJAJ request receives JSON data from the back-end,
            that data is passed as a plain Object as the response
            parameter (exception: in jsonp mode it is passed a
            string (why???)). The initiating request object is
            passed as the second parameter, but clients can normally
            ignore it (only those which need a way to map specific
            requests to responses will need it). The 3rd parameter
            is the same as the 'this' object for the context of the
            callback, but is provided because the instance-level
            callbacks (set in (WhAjaj.Connector instance).callbacks,
            require it in some cases (because their 'this' is
            different!).

            Note that the response might contain error information
            which comes from the back-end. The difference between
            this error info and the info passed to the onError()
            callback is that this data indicates an
            application-level error, whereas onError() is used to
            report connection-level problems or when the backend
            produces non-JSON data (which, when not in jsonp mode,
            is unexpected and is as fatal to the request as a
            connection error).
        */
        onResponse: function(response, request, opt){},

        /**
            If an AJAX request fails to establish a connection or it
            receives non-JSON data from the back-end, this function
            is called (e.g. timeout error or host name not
            resolvable). It is passed the originating request and the
            "normalized" connection parameters used for that
            request. The connectOpt object "should" (or "might")
            have an "errorMessage" property which describes the
            nature of the problem.

            Clients will almost always want to replace the default
            implementation with something which integrates into
            their application.
        */
        onError: function(request, connectOpt)
        {
            alert('AJAJ request failed:\n'
                +'Connection information:\n'
                +JSON.stringify(connectOpt,0,4)
            );
        },

        /**
            Called before each connection attempt is made. Clients
            can use this to, e.g.,  enable a visual "network activity
            notification" for the user. It is passed the original
            request object and the normalized connection parameters
            for the request. If this function changes opt, those
            changes _are_ applied to the subsequent request. If this
            function throws, neither the onError() nor afterSend()
            callbacks are triggered and WhAjaj.Connector.sendImpl()
            propagates the exception back to the caller.
        */
        beforeSend: function(request,opt){},

        /**
            Called after an AJAJ connection attempt completes,
            regardless of success or failure. Passed the same
            parameters as beforeSend() (see that function for
            details).

            Here's an example of setting up a visual notification on
            ajax operations using jQuery (but it's also easy to do
            without jQuery as well):

            @code
            function startAjaxNotif(req,opt) {
                var me = arguments.callee;
                var c = ++me.ajaxCount;
                me.element.text( c + " pending AJAX operation(s)..." );
                if( 1 == c ) me.element.stop().fadeIn();
            }
            startAjaxNotif.ajaxCount = 0.
            startAjaxNotif.element = jQuery('#whikiAjaxNotification');

            function endAjaxNotif() {
                var c = --startAjaxNotif.ajaxCount;
                startAjaxNotif.element.text( c+" pending AJAX operation(s)..." );
                if( 0 == c ) startAjaxNotif.element.stop().fadeOut();
            }
            @endcode

            Set the beforeSend/afterSend properties to those
            functions to enable the notifications by default.
        */
        afterSend: function(request,opt){},

        /**
            If jsonp is a string then the WhAjaj-internal response
            handling code ASSUMES that the response contains a JSONP-style
            construct and eval()s it after afterSend() but before onResponse().
            In this case, onResponse() will get a string value for the response
            instead of a response object parsed from JSON.
        */
        jsonp:undefined,
        /**
            Don't use yet. Planned future option.
        */
        propagateExceptions:false
    }
};


/**
    WhAjaj.Connector.prototype.callbacks defines callbacks analog
    to the onXXX callbacks defined in WhAjaj.Connector.options.ajax,
    with two notable differences:

    1) these callbacks, if set, are called in addition to any
    request-specific callback. The intention is to allow a framework to set
    "framework-level" callbacks which should be called independently of the
    request-specific callbacks (without interfering with them, e.g.
    requiring special re-forwarding features).

    2) The 'this' object in these callbacks is the Connector instance
    associated with the callback, whereas the "other" onXXX form has its
    "ajax options" object as its this.

    When this API says that an onXXX callback will be called for a request,
    both the request's onXXX (if set) and this one (if set) will be called.
*/
WhAjaj.Connector.prototype.callbacks = {};
/**
    Instance-specific values for AJAJ-level properties (as opposed to
    application-level request properties). Options set here "override" those
    specified in WhAjaj.Connector.options.ajax and are "overridden" by
    options passed to sendRequest().
*/
WhAjaj.Connector.prototype.options = {};


/**
    Tries to find the given key in any of the following, returning
    the first match found: opt, this.options, WhAjaj.Connector.options.ajax.

    Returns undefined if key is not found.
*/
WhAjaj.Connector.prototype.derivedOption = function(key,opt) {
    var v = opt ? opt[key] : undefined;
    if( undefined !== v ) return v;
    else v = this.options[key];
    if( undefined !== v ) return v;
    else v = WhAjaj.Connector.options.ajax[key];
    return v;
};

/**
    Returns a unique string on each call containing a generic
    reandom request identifier string. This is not used by the core
    API but can be used by client code to generate unique IDs for
    each request (if needed).

    The exact format is unspecified and may change in the future.

    Request IDs can be used by clients to "match up" responses to
    specific requests if needed. In practice, however, they are
    seldom, if ever, needed. When passing several concurrent
    requests through the same response callback, it might be useful
    for some clients to be able to distinguish, possibly re-routing
    them through other handlers based on the originating request type.

    If this.options.requestIdPrefix or
    WhAjaj.Connector.options.requestIdPrefix is set then that text
    is prefixed to the returned string.
*/
WhAjaj.Connector.prototype.generateRequestId = function()
{
    if( undefined === arguments.callee.sequence )
    {
        arguments.callee.sequence = 0;
    }
    var pref = this.options.requestIdPrefix || WhAjaj.Connector.options.requestIdPrefix || '';
    return pref +
        WhAjaj.msTimestamp() +
        '/'+(Math.round( Math.random() * 100000000) )+
        ':'+(++arguments.callee.sequence);
};

/**
    Copies (SHALLOWLY) all properties in opt to this.options.
*/
WhAjaj.Connector.prototype.addOptions = function(opt) {
    var k, v;
    for( k in opt ) {
        if( ! opt.hasOwnProperty(k) ) continue /* proactive Prototype kludge! */;
        this.options[k] = opt[k];
    }
    return this.options;
};

/**
    An internal helper object which holds several functions intended
    to simplify the creation of concrete communication channel
    implementations for WhAjaj.Connector.sendImpl(). These operations
    take care of some of the more error-prone parts of ensuring that
    onResponse(), onError(), etc. callbacks are called consistently
    using the same rules.
*/
WhAjaj.Connector.sendHelper = {
    /**
        opt is assumed to be a normalized set of
        WhAjaj.Connector.sendRequest() options. This function
        creates a url by concatenating opt.url and some form of
        opt.urlParam.

        If opt.urlParam is an object or string then it is appended
        to the url. An object is assumed to be a one-dimensional set
        of simple (urlencodable) key/value pairs, and not larger
        data structures. A string value is assumed to be a
        well-formed, urlencoded set of key/value pairs separated by
        '&' characters.

        The new/normalized URL is returned (opt is not modified). If
        opt.urlParam is not set then opt.url is returned (or an
        empty string if opt.url is itself a false value).

        TODO: if opt is-a Object and any key points to an array,
        build up a list of keys in the form "keyname[]". We could
        arguably encode sub-objects like "keyname[subkey]=...", but
        i don't know if that's conventions-compatible with other
        frameworks.
    */
    normalizeURL: function(opt) {
        var u = opt.url || '';
        if( opt.urlParam ) {
            var addQ = (u.indexOf('?') >= 0) ? false : true;
            var addA = addQ ? false : ((u.indexOf('&')>=0) ? true : false);
            var tail = '';
            if( WhAjaj.isObject(opt.urlParam) ) {
                var li = [], k;
                for( k in opt.urlParam) {
                    li.push( k+'='+encodeURIComponent( opt.urlParam[k] ) );
                }
                tail = li.join('&');
            }
            else if( 'string' === typeof opt.urlParam ) {
                tail = opt.urlParam;
            }
            u = u + (addQ ? '?' : '') + (addA ? '&' : '') + tail;
        }
        return u;
    },
    /**
        Should be called by WhAjaj.Connector.sendImpl()
        implementations after a response has come back. This
        function takes care of most of ensuring that framework-level
        conventions involving WhAjaj.Connector.options.ajax
        properties are followed.

        The request argument must be the original request passed to
        the sendImpl() function. It may legally be null for GET requests.

        The opt object should be the normalized AJAX options used
        for the connection.

        The resp argument may be either a plain Object or a string
        (in which case it is assumed to be JSON).

        The 'this' object for this call MUST be a WhAjaj.Connector
        instance in order for callback processing to work properly.

        This function takes care of the following:

        - Calling opt.afterSend()

        - If resp is a string, de-JSON-izing it to an object.

        - Calling opt.onResponse()

        - Calling opt.onError() in several common (potential) error
        cases.

        - If resp is-a String and opt.jsonp then resp is assumed to be
        a JSONP-form construct and is eval()d BEFORE opt.onResponse()
        is called. It is arguable to eval() it first, but the logic
        integrates better with the non-jsonp handler.

        The sendImpl() should return immediately after calling this.

        The sendImpl() must call only one of onSendSuccess() or
        onSendError(). It must call one of them or it must implement
        its own response/error handling, which is not recommended
        because getting the documented semantics of the
        onError/onResponse/afterSend handling correct can be tedious.
    */
    onSendSuccess:function(request,resp,opt) {
        var cb = this.callbacks || {};
        if( WhAjaj.isFunction(cb.afterSend) ) {
            try {cb.afterSend( request, opt );}
            catch(e){}
        }
        if( WhAjaj.isFunction(opt.afterSend) ) {
            try {opt.afterSend( request, opt );}
            catch(e){}
        }
        function doErr(){
            if( WhAjaj.isFunction(cb.onError) ) {
                try {cb.onError( request, opt );}
                catch(e){}
            }
            if( WhAjaj.isFunction(opt.onError) ) {
                try {opt.onError( request, opt );}
                catch(e){}
            }
        }
        if( ! resp ) {
            opt.errorMessage = "Sending of request succeeded but returned no data!";
            doErr();
            return false;
        }

        if( 'string' === typeof resp ) {
            try {
                resp = opt.jsonp ? eval(resp) : JSON.parse(resp);
            } catch(e) {
                opt.errorMessage = e.toString();
                doErr();
                return;
            }
        }
        try {
            if( WhAjaj.isFunction( cb.onResponse  ) ) {
                cb.onResponse( resp, request, opt );
            }
            if( WhAjaj.isFunction( opt.onResponse  ) ) {
                opt.onResponse( resp, request, opt );
            }
            return true;
        }
        catch(e) {
            opt.errorMessage = "Exception while handling inbound JSON response:\n"
                + e
                +"\nOriginal response data:\n"+JSON.stringify(resp,0,2)
                ;
            ;
            doErr();
            return false;
        }
    },
   /**
        Should be called by sendImpl() implementations after a response
        has failed to connect (e.g. could not resolve host or timeout
        reached). This function takes care of most of ensuring that
        framework-level conventions involving WhAjaj.Connector.options.ajax
        properties are followed.

        The request argument must be the original request passed to
        the sendImpl() function. It may legally be null for GET
        requests.

        The 'this' object for this call MUST be a WhAjaj.Connector
        instance in order for callback processing to work properly.

        The opt object should be the normalized AJAX options used
        for the connection. By convention, the caller of this
        function "should" set opt.errorMessage to contain a
        human-readable description of the error.

        The sendImpl() should return immediately after calling this. The
        return value from this function is unspecified.
    */
    onSendError: function(request,opt) {
        var cb = this.callbacks || {};
        if( WhAjaj.isFunction(cb.afterSend) ) {
            try {cb.afterSend( request, opt );}
            catch(e){}
        }
        if( WhAjaj.isFunction(opt.afterSend) ) {
            try {opt.afterSend( request, opt );}
            catch(e){}
        }
        if( WhAjaj.isFunction( cb.onError ) ) {
            try {cb.onError( request, opt );}
            catch(e) {/*ignore*/}
        }
        if( WhAjaj.isFunction( opt.onError ) ) {
            try {opt.onError( request, opt );}
            catch(e) {/*ignore*/}
        }
    }
};

/**
    WhAjaj.Connector.sendImpls holds several concrete
    implementations of WhAjaj.Connector.prototype.sendImpl(). To use
    a specific implementation by default assign
    WhAjaj.Connector.prototype.sendImpl to one of these functions.

    The functions defined here require that the 'this' object be-a
    WhAjaj.Connector instance.

    Historical notes:

    a) We once had an implementation based on Prototype, but that
    library just pisses me off (they change base-most types'
    prototypes, introducing side-effects in client code which
    doesn't even use Prototype). The Prototype version at the time
    had a serious toJSON() bug which caused empty arrays to
    serialize as the string "[]", which broke a bunch of my code.
    (That has been fixed in the mean time, but i don't use
    Prototype.)

    b) We once had an implementation for the dojo library,

    If/when the time comes to add Prototype/dojo support, we simply
    need to port:

    http://code.google.com/p/jsonmessage/source/browse/trunk/lib/JSONMessage/JSONMessage.inc.js

    (search that file for "dojo" and "Prototype") to this tree. That
    code is this code's generic grandfather and they are still very
    similar, so a port is trivial.

*/
WhAjaj.Connector.sendImpls = {
    /**
        This is a concrete implementation of
        WhAjaj.Connector.prototype.sendImpl() which uses the
        environment's native XMLHttpRequest class to send whiki
        requests and fetch the responses.

        The only argument must be a connection properties object, as
        constructed by WhAjaj.Connector.normalizeAjaxParameters().

        If window.firebug is set then window.firebug.watchXHR() is
        called to enable monitoring of the XMLHttpRequest object.

        This implementation honors the loginName and loginPassword
        connection parameters.

        Returns the XMLHttpRequest object.

        This implementation requires that the 'this' object be-a
        WhAjaj.Connector.

        This implementation uses setTimeout() to implement the
        timeout support, and thus the JS engine must provide that
        functionality.
    */
    XMLHttpRequest: function(request, args)
    {
        var json = WhAjaj.isObject(request) ? JSON.stringify(request) : request;
        var xhr = new XMLHttpRequest();
        var startTime = (new Date()).getTime();
        var timeout = args.timeout || 10000/*arbitrary!*/;
        var hitTimeout = false;
        var done = false;
        var tmid /* setTimeout() ID */;
        var whself = this;
        function handleTimeout()
        {
            hitTimeout = true;
            if( ! done )
            {
                var now = (new Date()).getTime();
                try { xhr.abort(); } catch(e) {/*ignore*/}
                // see: http://www.w3.org/TR/XMLHttpRequest/#the-abort-method
                args.errorMessage = "Timeout of "+timeout+"ms reached after "+(now-startTime)+"ms during AJAX request.";
                WhAjaj.Connector.sendHelper.onSendError.apply( whself, [request, args] );
            }
            return;
        }
        function onStateChange()
        { // reminder to self: apparently 'this' is-not-a XHR :/
            if( hitTimeout )
            { /* we're too late - the error was already triggered. */
                return;
            }

            if( 4 == xhr.readyState )
            {
                done = true;
                if( tmid )
                {
                    clearTimeout( tmid );
                    tmid = null;
                }
                if( (xhr.status >= 200) && (xhr.status < 300) )
                {
                    WhAjaj.Connector.sendHelper.onSendSuccess.apply( whself, [request, xhr.responseText, args] );
                    return;
                }
                else
                {
                    if( undefined === args.errorMessage )
                    {
                        args.errorMessage = "Error sending a '"+args.method+"' AJAX request to "
                                +"["+args.url+"]: "
                                +"Status text=["+xhr.statusText+"]"
                            ;
                        WhAjaj.Connector.sendHelper.onSendError.apply( whself, [request, args] );
                    }
                    else { /*maybe it was was set by the timeout handler. */ }
                    return;
                }
            }
        };

        xhr.onreadystatechange = onStateChange;
        if( ('undefined'!==(typeof window)) && ('firebug' in window) && ('watchXHR' in window.firebug) )
        { /* plug in to firebug lite's XHR monitor... */
            window.firebug.watchXHR( xhr );
        }
        try
        {
            //alert( JSON.stringify( args  ));
            function xhrOpen()
            {
                if( ('loginName' in args) && args.loginName )
                {
                    xhr.open( args.method, args.url, args.asynchronous, args.loginName, args.loginPassword );
                }
                else
                {
                    xhr.open( args.method, args.url, args.asynchronous  );
                }
            }
            if( json && ('POST' ===  args.method.toUpperCase()) )
            {
                xhrOpen();
                xhr.setRequestHeader("Content-Type", "application/json; charset=utf-8");
                // Google Chrome warns that it refuses to set these
                // "unsafe" headers (his words, not mine):
                // xhr.setRequestHeader("Content-length", json.length);
                // xhr.setRequestHeader("Connection", "close");
                xhr.send( json );
            }
            else /* assume GET */
            {
                xhrOpen();
                xhr.send(null);
            }
            tmid = setTimeout( handleTimeout, timeout );
            return xhr;
        }
        catch(e)
        {
            args.errorMessage = e.toString();
            WhAjaj.Connector.sendHelper.onSendError.apply( whself, [request, args] );
            return undefined;
        }
    }/*XMLHttpRequest()*/,
    /**
        This is a concrete implementation of
        WhAjaj.Connector.prototype.sendImpl() which uses the jQuery
        AJAX API to send requests and fetch the responses.

        The first argument may be either null/false, an Object
        containing toJSON-able data to post to the back-end, or such an
        object in JSON string form.

        The second argument must be a connection properties object, as
        constructed by WhAjaj.Connector.normalizeAjaxParameters().

        If window.firebug is set then window.firebug.watchXHR() is
        called to enable monitoring of the XMLHttpRequest object.

        This implementation honors the loginName and loginPassword
        connection parameters.

        Returns the XMLHttpRequest object.

        This implementation requires that the 'this' object be-a
        WhAjaj.Connector.
    */
    jQuery:function(request,args)
    {
        var data = request || undefined;
        var whself = this;
        if( data ) {
            if('string'!==typeof data) {
                try {
                    data = JSON.stringify(data);
                }
                catch(e) {
                    WhAjaj.Connector.sendHelper.onSendError.apply( whself, [request, args] );
                    return;
                }
            }
        }
        var ajopt = {
            url: args.url,
            data: data,
            type: args.method,
            async: args.asynchronous,
            password: (undefined !== args.loginPassword) ? args.loginPassword : undefined,
            username: (undefined !== args.loginName) ? args.loginName : undefined,
            contentType: 'application/json; charset=utf-8',
            error: function(xhr, textStatus, errorThrown)
            {
                //this === the options for this ajax request
                args.errorMessage = "Error sending a '"+ajopt.type+"' request to ["+ajopt.url+"]: "
                        +"Status text=["+textStatus+"]"
                        +(errorThrown ? ("Error=["+errorThrown+"]") : "")
                    ;
                WhAjaj.Connector.sendHelper.onSendError.apply( whself, [request, args] );
            },
            success: function(data)
            {
                WhAjaj.Connector.sendHelper.onSendSuccess.apply( whself, [request, data, args] );
            },
            /* Set dataType=text instead of json to keep jQuery from doing our carefully
                written response handling for us.
            */
            dataType: 'text'
        };
        if( undefined !== args.timeout )
        {
            ajopt.timeout = args.timeout;
        }
        try
        {
            return jQuery.ajax(ajopt);
        }
        catch(e)
        {
            args.errorMessage = e.toString();
            WhAjaj.Connector.sendHelper.onSendError.apply( whself, [request, args] );
            return undefined;
        }
    }/*jQuery()*/,
    /**
        This is a concrete implementation of
        WhAjaj.Connector.prototype.sendImpl() which uses the rhino
        Java API to send requests and fetch the responses.

        Limitations vis-a-vis the interface:

        - timeouts are not supported.

        - asynchronous mode is not supported because implementing it
        requires the ability to kill a running thread (which is deprecated
        in the Java API).

        TODOs:

        - add socket timeouts.

        - support HTTP proxy.

        The Java APIs support this, it just hasn't been added here yet.
    */
    rhino:function(request,args)
    {
        var self = this;
        var data = request || undefined;
        if( data ) {
            if('string'!==typeof data) {
                try {
                    data = JSON.stringify(data);
                }
                catch(e) {
                    WhAjaj.Connector.sendHelper.onSendError.apply( self, [request, args] );
                    return;
                }
            }
        }
        var url;
        var con;
        var IO = new JavaImporter(java.io);
        var wr;
        var rd, ln, json = [];
        function setIncomingCookies(list){
            if(!list || !list.length) return;
            if( !self.cookies ) self.cookies = {};
            var k, v, i;
            for( i = 0; i < list.length; ++i ){
                v = list[i].split('=',2);
                k = decodeURIComponent(v[0])
                v = v[0] ? decodeURIComponent(v[0].split(';',2)[0]) : null;
                //print("RECEIVED COOKIE: "+k+"="+v);
                if(!v) {
                    delete self.cookies[k];
                    continue;
                }else{
                    self.cookies[k] = v;
                }
            }
        };
        function setOutboundCookies(conn){
            if(!self.cookies) return;
            var k, v;
            for( k in self.cookies ){
                if(!self.cookies.hasOwnProperty(k)) continue /*kludge for broken JS libs*/;
                v = self.cookies[k];
                conn.addRequestProperty("Cookie", encodeURIComponent(k)+'='+encodeURIComponent(v));
                //print("SENDING COOKIE: "+k+"="+v);
            }
        };
        try{
            url = new java.net.URL( args.url )
            con = url.openConnection(/*FIXME: add proxy support!*/);
            con.setRequestProperty("Accept-Charset","utf-8");
            setOutboundCookies(con);
            if(data){
                con.setRequestProperty("Content-Type","application/json; charset=utf-8");
                con.setDoOutput( true );
                wr = new IO.OutputStreamWriter(con.getOutputStream())
                wr.write(data);
                wr.flush();
                wr.close();
                wr = null;
                //print("POSTED: "+data);
            }
            rd = new IO.BufferedReader(new IO.InputStreamReader(con.getInputStream()));
            //var skippedHeaders = false;
            while ((line = rd.readLine()) !== null) {
                //print("LINE: "+line);
                //if(!line.length && !skippedHeaders){
                //    skippedHeaders = true;
                // json = [];
                //    continue;
                //}
                json.push(line);
            }
            setIncomingCookies(con.getHeaderFields().get("Set-Cookie"));
        }catch(e){
            args.errorMessage = e.toString();
            WhAjaj.Connector.sendHelper.onSendError.apply( self, [request, args] );
            return undefined;
        }
        try { if(wr) wr.close(); } catch(e) { /*ignore*/}
        try { if(rd) rd.close(); } catch(e) { /*ignore*/}
        json = json.join('');
        //print("READ IN JSON: "+json);
        WhAjaj.Connector.sendHelper.onSendSuccess.apply( self, [request, json, args] );
    }/*rhino*/
};

/**
    An internal function which takes an object containing properties
    for a WhAjaj.Connector network request. This function creates a new
    object containing a superset of the properties from:

    a) opt
    b) this.options
    c) WhAjaj.Connector.options.ajax

    in that order, using the first one it finds.

    All non-function properties are _deeply_ copied via JSON cloning
    in order to prevent accidental "cross-request pollenation" (been
    there, done that). Functions cannot be cloned and are simply
    copied by reference.

    This function throws if JSON-copying one of the options fails
    (e.g. due to cyclic data structures).

    Reminder to self: this function does not "normalize" opt.urlParam
    by encoding it into opt.url, mainly for historical reasons, but
    also because that behaviour was specifically undesirable in this
    code's genetic father.
*/
WhAjaj.Connector.prototype.normalizeAjaxParameters = function (opt)
{
    var rc = {};
    function merge(k,v)
    {
        if( rc.hasOwnProperty(k) ) return;
        else if( WhAjaj.isFunction(v) ) {}
        else if( WhAjaj.isObject(v) ) v = JSON.parse( JSON.stringify(v) );
        rc[k]=v;
    }
    function cp(obj) {
        if( ! WhAjaj.isObject(obj) ) return;
        var k;
        for( k in obj ) {
            if( ! obj.hasOwnProperty(k) ) continue /* i will always hate the Prototype designers for this. */;
            merge(k, obj[k]);
        }
    }
    cp( opt );
    cp( this.options );
    cp( WhAjaj.Connector.options.ajax );
    // no, not here: rc.url = WhAjaj.Connector.sendHelper.normalizeURL(rc);
    return rc;
};

/**
    This is the generic interface for making calls to a back-end
    JSON-producing request handler. It is a simple wrapper around
    WhAjaj.Connector.prototype.sendImpl(), which just normalizes the
    connection options for sendImpl() and makes sure that
    opt.beforeSend() is (possibly) called.

    The request parameter must either be false/null/empty or a
    fully-populated JSON-able request object (which will be sent as
    unencoded application/json text), depending on the type of
    request being made. It is never semantically legal (in this API)
    for request to be a string/number/true/array value. As a rule,
    only POST requests use the request data. GET requests should
    encode their data in opt.url or opt.urlParam (see below).

    opt must contain the network-related parameters for the request.
    Paramters _not_ set in opt are pulled from this.options or
    WhAjaj.Connector.options.ajax (in that order, using the first
    value it finds). Thus the set of connection-level options used
    for the request are a superset of those various sources.

    The "normalized" (or "superimposed") opt object's URL may be
    modified before the request is sent, as follows:

    if opt.urlParam is a string then it is assumed to be properly
    URL-encoded parameters and is appended to the opt.url. If it is
    an Object then it is assumed to be a one-dimensional set of
    key/value pairs with simple values (numbers, strings, booleans,
    null, and NOT objects/arrays). The keys/values are URL-encoded
    and appended to the URL.

    The beforeSend() callback (see below) can modify the options
    object before the request attempt is made.

    The callbacks in the normalized opt object will be triggered as
    follows (if they are set to Function values):

    - beforeSend(request,opt) will be called before any network
    processing starts. If beforeSend() throws then no other
    callbacks are triggered and this function propagates the
    exception. This function is passed normalized connection options
    as its second parameter, and changes this function makes to that
    object _will_ be used for the pending connection attempt.

    - onError(request,opt) will be called if a connection to the
    back-end cannot be established. It will be passed the original
    request object (which might be null, depending on the request
    type) and the normalized options object. In the error case, the
    opt object passed to onError() "should" have a property called
    "errorMessage" which contains a description of the problem.

    - onError(request,opt) will also be called if connection
    succeeds but the response is not JSON data.

    - onResponse(response,request) will be called if the response
    returns JSON data. That data might hold an error response code -
    clients need to check for that. It is passed the response object
    (a plain object) and the original request object.

    - afterSend(request,opt) will be called directly after the
    AJAX request is finished, before onError() or onResonse() are
    called. Possible TODO: we explicitly do NOT pass the response to
    this function in order to keep the line between the responsibilities
    of the various callback clear (otherwise this could be used the same
    as onResponse()). In practice it would sometimes be useful have the
    response passed to this function, mainly for logging/debugging
    purposes.

    The return value from this function is meaningless because
    AJAX operations tend to take place asynchronously.

*/
WhAjaj.Connector.prototype.sendRequest = function(request,opt)
{
    if( !WhAjaj.isFunction(this.sendImpl) )
    {
        throw new Error("This object has no sendImpl() member function! I don't know how to send the request!");
    }
    var ex = false;
    var av = Array.prototype.slice.apply( arguments, [0] );

    /**
        FIXME: how to handle the error, vis-a-vis- the callbacks, if
        normalizeAjaxParameters() throws? It can throw if
        (de)JSON-izing fails.
    */
    var norm = this.normalizeAjaxParameters( WhAjaj.isObject(opt) ? opt : {} );
    norm.url = WhAjaj.Connector.sendHelper.normalizeURL(norm);
    if( ! request ) norm.method = 'GET';
    var cb = this.callbacks || {};
    if( this.callbacks && WhAjaj.isFunction(this.callbacks.beforeSend) ) {
        this.callbacks.beforeSend( request, norm );
    }
    if( WhAjaj.isFunction(norm.beforeSend) ){
        norm.beforeSend( request, norm );
    }
    //alert( WhAjaj.stringify(request)+'\n'+WhAjaj.stringify(norm));
    try { this.sendImpl( request, norm ); }
    catch(e) { ex = e; }
    if(ex) throw ex;
};

/**
    sendImpl() holds a concrete back-end connection implementation. It
    can be replaced with a custom implementation if one follows the rules
    described throughout this API. See WhAjaj.Connector.sendImpls for
    the concrete implementations included with this API.
*/
//WhAjaj.Connector.prototype.sendImpl = WhAjaj.Connector.sendImpls.XMLHttpRequest;
//WhAjaj.Connector.prototype.sendImpl = WhAjaj.Connector.sendImpls.rhino;
//WhAjaj.Connector.prototype.sendImpl = WhAjaj.Connector.sendImpls.jQuery;

if( 'undefined' !== typeof jQuery ){
    WhAjaj.Connector.prototype.sendImpl = WhAjaj.Connector.sendImpls.jQuery;
}
else {
    WhAjaj.Connector.prototype.sendImpl = WhAjaj.Connector.sendImpls.XMLHttpRequest;
}
