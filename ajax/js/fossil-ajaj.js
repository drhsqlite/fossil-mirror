/**
    This file contains a WhAjaj extension for use with Fossil/JSON.

    Author: Stephan Beal (sgbeal@googlemail.com)

    License: Public Domain
*/

/**
    Constructor for a new Fossil AJAJ client. ajajOpt may be an optional
    object suitable for passing to the WhAjaj.Connector() constructor.

    On returning, this.ajaj is-a WhAjaj.Connector instance which can
    be used to send requests to the back-end (though the convenience
    functions of this class are the preferred way to do it). Clients
    are encouraged to use FossilAjaj.sendCommand() (and friends) instead
    of the underlying WhAjaj.Connector API, since this class' API
    contains Fossil-specific request-calling handling (e.g. of authentication
    info) whereas WhAjaj is more generic.
*/
function FossilAjaj(ajajOpt)
{
    this.ajaj = new WhAjaj.Connector(ajajOpt);
    return this;
}

FossilAjaj.prototype.generateRequestId = function() {
    return this.ajaj.generateRequestId();
};

/**
   Proxy for this.ajaj.sendRequest().
*/
FossilAjaj.prototype.sendRequest = function(req,opt) {
    return this.ajaj.sendRequest(req,opt);
};

/**
    Sends a command to the fossil back-end. Command should be the
    path part of the URL, e.g. /json/stat, payload is a request-specific
    value type (may often be null/undefined). ajajOpt is an optional object
    holding WhAjaj.sendRequest()-compatible options.

    This function constructs a Fossil/JSON request envelope based
    on the given arguments and adds this.auth.authToken and a requestId
    to it.
*/
FossilAjaj.prototype.sendCommand = function(command, payload, ajajOpt) {
    var req;
    ajajOpt = ajajOpt || {};
    if(payload || (this.auth && this.auth.authToken) || ajajOpt.jsonp) {
        req = {
            payload:payload,
            requestId:('function' === typeof this.generateRequestId) ? this.generateRequestId() : undefined,
            authToken:(this.auth ? this.auth.authToken : undefined),
            jsonp:('string' === typeof ajajOpt.jsonp) ? ajajOpt.jsonp : undefined
        };
    }
    ajajOpt.method = req ? 'POST' : 'GET';
    // just for debuggering: ajajOpt.method = 'POST'; if(!req) req={};
    if(command) ajajOpt.url = this.ajaj.derivedOption('url',ajajOpt) + command;
    this.ajaj.sendRequest(req,ajajOpt);
};

/**
    Sends a login request to the back-end.

    ajajOpt is an optional configuration object suitable for passing
    to sendCommand().

    After the response returns, this.auth will be
    set to the response payload.

    If name === 'anonymous' (the default if none is passed in) then this
    function ignores the pw argument and must make two requests - the first
    one gets the captcha code and the second one submits it.
    ajajOpt.onResponse() (if set) is only called for the actual login
    response (the 2nd one), as opposed to being called for both requests.
    However, this.ajaj.callbacks.onResponse() _is_ called for both (because
    it happens at a lower level).

    If this object has an onLogin() function it is called (with
    no arguments) before the onResponse() handler of the login is called
    (that is the 2nd request for anonymous logins) and any exceptions
    it throws are ignored.

*/
FossilAjaj.prototype.login = function(name,pw,ajajOpt) {
    name = name || 'anonymous';
    var self = this;
    var loginReq = {
        name:name,
        password:pw
    };
    ajajOpt = this.ajaj.normalizeAjaxParameters( ajajOpt || {} );
    var oldOnResponse = ajajOpt.onResponse;
    ajajOpt.onResponse = function(resp,req) {
        var thisOpt = this;
        //alert('login response:\n'+WhAjaj.stringify(resp));
        if( resp && resp.payload ) {
            //self.userName = resp.payload.name;
            //self.capabilities = resp.payload.capabilities;
            self.auth = resp.payload;
        }
        if( WhAjaj.isFunction( self.onLogin ) ){
            try{ self.onLogin(); }
            catch(e){}
        }
        if( WhAjaj.isFunction(oldOnResponse) ) {
            oldOnResponse.apply(thisOpt,[resp,req]);
        }
    };
    function doLogin(){
        //alert("Sending login request..."+WhAjaj.stringify(loginReq));
        self.sendCommand('/json/login', loginReq, ajajOpt);
    }
    if( 'anonymous' === name ){
      this.sendCommand('/json/anonymousPassword',undefined,{
          onResponse:function(resp,req){
/*
            if( WhAjaj.isFunction(oldOnResponse) ){
                oldOnResponse.apply(this, [resp,req]);
            };
*/
            if(resp && !resp.resultCode){
                //alert("Got PW. Trying to log in..."+WhAjaj.stringify(resp));
                loginReq.anonymousSeed = resp.payload.seed;
                loginReq.password = resp.payload.password;
                doLogin();
            }
          }
      });
    }
    else doLogin();
};

/**
    Logs out of fossil, invaliding this login token.

    ajajOpt is an optional configuration object suitable for passing
    to sendCommand().

    If this object has an onLogout() function it is called (with
    no arguments) before the onResponse() handler is called.
    IFF the response succeeds then this.auth is unset.
*/
FossilAjaj.prototype.logout = function(ajajOpt) {
    var self = this;
    ajajOpt = this.ajaj.normalizeAjaxParameters( ajajOpt || {} );
    var oldOnResponse = ajajOpt.onResponse;
    ajajOpt.onResponse = function(resp,req) {
        var thisOpt = this;
        self.auth = undefined;
        if( WhAjaj.isFunction( self.onLogout ) ){
            try{ self.onLogout(); }
            catch(e){}
        }
        if( WhAjaj.isFunction(oldOnResponse) ) {
            oldOnResponse.apply(thisOpt,[resp,req]);
        }
    };
    this.sendCommand('/json/logout', undefined, ajajOpt );
};

/**
    Sends a HAI request to the server. /json/HAI is an alias /json/version.

    ajajOpt is an optional configuration object suitable for passing
    to sendCommand().
*/
FossilAjaj.prototype.HAI = function(ajajOpt) {
    this.sendCommand('/json/HAI', undefined, ajajOpt);
};


/**
    Sends a /json/whoami request. Updates this.auth to contain
    the login info, removing them if the response does not contain
    that data.
*/
FossilAjaj.prototype.whoami = function(ajajOpt) {
    var self = this;
    ajajOpt = this.ajaj.normalizeAjaxParameters( ajajOpt || {} );
    var oldOnResponse = ajajOpt.onResponse;
    ajajOpt.onResponse = function(resp,req) {
        var thisOpt = this;
        if( resp && resp.payload ){
            if(!self.auth || (self.auth.authToken!==resp.payload.authToken)){
                self.auth = resp.payload;
                if( WhAjaj.isFunction(self.onLogin) ){
                    self.onLogin();
                }
            }
        }
        else { delete self.auth; }
        if( WhAjaj.isFunction(oldOnResponse) ) {
            oldOnResponse.apply(thisOpt,[resp,req]);
        }
    };
    self.sendCommand('/json/whoami', undefined, ajajOpt);
};

/**
    EXPERIMENTAL concrete WhAjaj.Connector.sendImpl() implementation which
    uses Rhino to connect to a local fossil binary for input and output. Its
    signature and semantics are as described for
    WhAjaj.Connector.prototype.sendImpl(), with a few exceptions and
    additions:

    - It does not support timeouts or asynchronous mode.

    - The args.fossilBinary property must point to the local fossil binary
    (it need not be a complete path if fossil is in the $PATH). This
    function throws (without calling any request callbacks) if
    args.fossilBinary is not set. fossilBinary may be set on
    WhAjaj.Connector.options.ajax, in the FossilAjaj constructor call, as
    the ajax options parameter to any of the FossilAjaj.sendCommand() family
    of functions, or by setting
    aFossilAjajInstance.ajaj.options.fossilBinary on a specific
    FossilAjaj instance.

    - It uses the args.url field to create the "command" property of the
    request, constructs a request envelope, spawns a fossil process in JSON
    mode, feeds it the request envelope, and returns the response envelope
    via the same mechanisms defined for the HTTP-based implementations.

    The interface is otherwise compatible with the "normal"
    FossilAjaj.sendCommand() front-end (it is, however, fossil-specific, and
    not back-end agnostic like the WhAjaj.sendImpl() interface intends).


*/
FossilAjaj.rhinoLocalBinarySendImpl = function(request,args){
    var self = this;
    request = request || {};
    if(!args.fossilBinary){
        throw new Error("fossilBinary is not set on AJAX options!");
    }
    var url = args.url.split('?')[0].split(/\/+/);
    if(url.length>1){
        // 3x shift(): protocol, host, 'json' part of path
        request.command = (url.shift(),url.shift(),url.shift(), url.join('/'));
    }
    delete args.url;
    //print("rhinoLocalBinarySendImpl SENDING: "+WhAjaj.stringify(request));
    var json;
    try{
        var pargs = [args.fossilBinary, 'json', '--json-input', '-'];
        var p = java.lang.Runtime.getRuntime().exec(pargs);
        var outs = p.getOutputStream();
        var osr = new java.io.OutputStreamWriter(outs);
        var osb = new java.io.BufferedWriter(osr);

        json = JSON.stringify(request);
        osb.write(json,0, json.length);
        osb.close();
        var ins = p.getInputStream();
        var isr = new java.io.InputStreamReader(ins);
        var br = new java.io.BufferedReader(isr);
        var line;
        json = [];
        while( null !== (line=br.readLine())){
            json.push(line);
        }
        ins.close();
    }catch(e){
        args.errorMessage = e.toString();
        WhAjaj.Connector.sendHelper.onSendError.apply( self, [request, args] );
        return undefined;
    }
    json = json.join('');
    //print("READ IN JSON: "+json);
    WhAjaj.Connector.sendHelper.onSendSuccess.apply( self, [request, json, args] );
}/*rhinoLocalBinary*/
