var FShell = {
    serverUrl:
        'http://localhost:8080'
        //'http://fjson/cgi-bin/fossil-json.cgi'
        //'http://192.168.1.62:8080'
        //'http://fossil.wanderinghorse.net/repos/fossil-json-java/index.cgi'
        ,
    verbose:false,
    prompt:"fossil shell > ",
    wiki:{},
    consol:java.lang.System.console(),
    v:function(msg){
        if(this.verbose){
            print("VERBOSE: "+msg);
        }
    }
};
(function bootstrap() {
    var srcdir = '../js/';
    var includes = [srcdir+'json2.js',
                    srcdir+'whajaj.js',
                    srcdir+'fossil-ajaj.js'
                    ];
    for( var i in includes ) {
        load(includes[i]);
    }
    WhAjaj.Connector.prototype.sendImpl = WhAjaj.Connector.sendImpls.rhino;
    FShell.fossil = new FossilAjaj({
        asynchronous:false, /* rhino-based impl doesn't support async. */
        timeout:10000,
        url:FShell.serverUrl
    });
    print("Server: "+FShell.serverUrl);
    var cb = FShell.fossil.ajaj.callbacks;
    cb.beforeSend = function(req,opt){
        if(!FShell.verbose) return;
        print("SENDING REQUEST: AJAJ options="+JSON.stringify(opt));
        if(req) print("Request envelope="+WhAjaj.stringify(req));
    };
    cb.afterSend = function(req,opt){
        //if(!FShell.verbose) return;
        //print("REQUEST RETURNED: opt="+JSON.stringify(opt));
        //if(req) print("Request="+WhAjaj.stringify(req));
    };
    cb.onError = function(req,opt){
        //if(!FShell.verbose) return;
        print("ERROR: "+WhAjaj.stringify(opt));
    };
    cb.onResponse = function(resp,req){
        if(!FShell.verbose) return;
        if(resp && resp.resultCode){
            print("Response contains error info: "+resp.resultCode+": "+resp.resultText);
        }
        print("GOT RESPONSE: "+(('string'===typeof resp) ? resp : WhAjaj.stringify(resp)));
    };
    FShell.fossil.HAI({
        onResponse:function(resp,opt){
            assertResponseOK(resp);
        }
    });
})();

/**
    Throws an exception of cond is a falsy value.
*/
function assert(cond, descr){
    descr = descr || "Undescribed condition.";
    if(!cond){
        throw new Error("Assertion failed: "+descr);
    }else{
        //print("Assertion OK: "+descr);
    }
}

/**
    Convenience form of FShell.fossil.sendCommand(command,payload,ajajOpt).
*/
function send(command,payload, ajajOpt){
    FShell.fossil.sendCommand(command,payload,ajajOpt);
}

/**
    Asserts that resp is-a Object, resp.fossil is-a string, and
    !resp.resultCode.
*/
function assertResponseOK(resp){
    assert('object' === typeof resp,'Response is-a object.');
    assert( 'string' === typeof resp.fossil, 'Response contains fossil property.');
    assert( !resp.resultCode, 'resp.resultCode='+resp.resultCode);
}
/**
    Asserts that resp is-a Object, resp.fossil is-a string, and
    resp.resultCode is a truthy value. If expectCode is set then
    it also asserts that (resp.resultCode=='FOSSIL-'+expectCode).
*/
function assertResponseError(resp,expectCode){
    assert('object' === typeof resp,'Response is-a object.');
    assert( 'string' === typeof resp.fossil, 'Response contains fossil property.');
    assert( resp.resultCode, 'resp.resultCode='+resp.resultCode);
    if(expectCode){
        assert( 'FOSSIL-'+expectCode == resp.resultCode, 'Expecting result code '+expectCode );
    }
}

FShell.readline = (typeof readline === 'function') ? (readline) : (function() {
     importPackage(java.io);
     importPackage(java.lang);
     var stdin = new BufferedReader(new InputStreamReader(System['in']));
     var self = this;
     return function(prompt) {
        if(prompt) print(prompt);
        var x = stdin.readLine();
        return null===x ? x : String(x) /*convert to JS string!*/;
     };
}());

FShell.dispatchLine = function(line){
    var av = line.split(' '); // FIXME: to shell-like tokenization. Too tired!
    var cmd = av[0];
    var key, h;
    if('/' == cmd[0]) key = '/';
    else key = this.commandAliases[cmd];
    if(!key) key = cmd;
    h = this.commandHandlers[key];
    if(!h){
        print("Command not known: "+cmd +" ("+key+")");
    }else if(!WhAjaj.isFunction(h)){
        print("Not a function: "+key);
    }
    else{
        print("Sending ["+key+"] command... ");
        try{h(av);}
        catch(e){ print("EXCEPTION: "+e); }
    }
};

FShell.onResponseDefault = function(callback){
    return function(resp,req){
        assertResponseOK(resp);
        print("Payload: "+(resp.payload ? WhAjaj.stringify(resp.payload) : "none"));
        if(WhAjaj.isFunction(callback)){
            callback(resp,req);
        }
    };
};
FShell.commandHandlers = {
    "?":function(args){
        var k;
        print("Available commands...\n");
        var o = FShell.commandHandlers;
        for(k in o){
            if(! o.hasOwnProperty(k)) continue;
            print("\t"+k);
        }
    },
    "/":function(args){
        FShell.fossil.sendCommand('/json'+args[0],undefined,{
            beforeSend:function(req,opt){
                print("Sending to: "+opt.url);
            },
            onResponse:FShell.onResponseDefault()
        });
    },
    "eval":function(args){
        eval(args.join(' '));
    },
    "login":function(args){
        FShell.fossil.login(args[1], args[2], {
            onResponse:FShell.onResponseDefault()
        });
    },
    "whoami":function(args){
        FShell.fossil.whoami({
            onResponse:FShell.onResponseDefault()
        });
    },
    "HAI":function(args){
        FShell.fossil.HAI({
            onResponse:FShell.onResponseDefault()
        });
    }

};
FShell.commandAliases = {
    "li":"login",
    "lo":"logout",
    "who":"whoami",
    "hi":"HAI",
    "tci":"/timeline/ci?limit=3"
};
FShell.mainLoop = function(){
    var line;
    var check = /\S/;
    //var isJavaNull = /java\.lang\.null/;
    //print(typeof java.lang['null']);
    while( null != (line=this.readline(this.prompt)) ){
        if(null===line) break /*EOF*/;
        else if( "" === line ) continue;
        //print("Got line: "+line);
        else if(!check.test(line)) continue;
        print('typeof line = '+typeof line);
        this.dispatchLine(line);
        print("");
    }
    print("Bye!");
};

FShell.mainLoop();
