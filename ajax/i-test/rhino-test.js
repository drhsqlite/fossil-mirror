var TestApp = {
    serverUrl:
        'http://localhost:8080'
        //'http://fjson/cgi-bin/fossil-json.cgi'
        //'http://192.168.1.62:8080'
        ,
    verbose:true
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
    TestApp.fossil = new FossilAjaj({
        asynchronous:false, /* rhino-based impl doesn't support asynch. */
        url:TestApp.serverUrl,
        beforeSend:function(req,opt){
            if(!TestApp.verbose) return;
            print("SENDING REQUEST: opt="+JSON.stringify(opt));
            if(req) print("Request="+WhAjaj.stringify(req));
        },
        afterSend:function(req,opt){
            if(!TestApp.verbose) return;
            print("SENT REQUEST: opt="+JSON.stringify(opt));
            if(req) print("Request="+WhAjaj.stringify(req));
        },
        onError:function(req,opt){
            if(!TestApp.verbose) return;
            print("ERROR: "+WhAjaj.stringify(opt));
        },
        onResponse:function(resp,req){
            if(!TestApp.verbose) return;
            print("GOT RESPONSE: "+(('string'===typeof resp) ? resp : WhAjaj.stringify(resp)));
        }
    });
})();

/**
    Throws an exception of cond is a falsy value.
*/
function assert(cond, descr){
    descr = descr || "Undescribed condition failed.";
    if(!cond){
        throw new Error("Assertion failed: "+descr);
    }else{
        print("Assertion OK: "+descr);
    }
}

/**
    Calls func() in a try/catch block and throws an exception if
    func() does NOT throw.
*/
function assertThrows(func, descr){
    descr = descr || "Undescribed condition failed.";
    var ex;
    try{
        func();
    }catch(e){
        ex = e;
    }
    if(!ex){
        throw new Error("Function did not throw (as expected): "+descr);
    }else{
        print("Function threw (as expected): "+descr+": "+ex);
    }
}

/**
    Convenience form of TestApp.fossil.sendCommand(command,payload,ajajOpt).
*/
function send(command,payload, ajajOpt){
    TestApp.fossil.sendCommand(command,payload,ajajOpt);
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
function assertResponseError(resp,expectCode){
    assert('object' === typeof resp,'Response is-a object.');
    assert( 'string' === typeof resp.fossil, 'Response contains fossil property.');
    assert( resp.resultCode, 'resp.resultCode='+resp.resultCode);
    if(expectCode){
        assert( 'FOSSIL-'+expectCode == resp.resultCode, 'Expecting result code '+expectCode );
    }
}
function testHAI(){
    TestApp.fossil.HAI({
        onResponse:function(resp,req){
            assertResponseOK(resp);
            TestApp.serverVersion = resp.fossil;
        }
    });
    assert( 'string' === typeof TestApp.serverVersion, 'server version = '+TestApp.serverVersion);
}
testHAI.description = 'Get server version info.';

function testIAmNobody(){
    TestApp.fossil.whoami('/json/whoami');
    assert('nobody' === TestApp.fossil.userName, 'User == nobody.' );
    assert(!TestApp.fossil.authToken, 'authToken is not set.' );
   
}
testIAmNobody.description = 'Ensure that current user is "nobody".';


function testAnonymousLogin(){
    TestApp.fossil.login();
    assert('string' === typeof TestApp.fossil.authToken, 'authToken = '+TestApp.fossil.authToken);
    assert( 'string' === typeof TestApp.fossil.userName, 'User name = '+TestApp.fossil.userName);
}
testAnonymousLogin.description = 'Perform anonymous login.';



(function runAllTests(){
    var testList = [
        testHAI,
        testIAmNobody,
        testAnonymousLogin
    ];
    var i, f;
    for( i = 0; i < testList.length; ++i ){
        f = testList[i];
        try{
            print("Running #"+(i+1)+": "+(f.description || "no description."));
            f();
        }catch(e){
            print("Test failed: "+e);
            throw e;
        }
    }

})();

print("Done!");
