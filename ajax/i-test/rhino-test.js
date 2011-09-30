var TestApp = {
    serverUrl:'http://localhost:8080'
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
            print("SENDING REQUEST: "+WhAjaj.stringify(opt));
        },
        afterSend:function(req,opt){
            print("SENT REQUEST: "+WhAjaj.stringify(opt));
        },
        onError:function(req,opt){
            print("ERROR: "+WhAjaj.stringify(opt));
        },
        onResponse:function(resp,req){
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
        print("Assertion failed: "+descr);
        throw new Error(descr);
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
    assert( !resp.resultCode, 'Response contains no error state.');
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

function testWhoAmI_1(){
    TestApp.fossil.whoami('/json/whoami');
    assert('nobody' === TestApp.fossil.userName, 'User == nobody.' );
    assert('anonymous' !== TestApp.fossil.userName, 'User != anonymous.' );
    
}
testWhoAmI_1.description = 'First ever fossil-over-rhino test.';



(function runAllTests(){
    var testList = [
        testHAI,
        testWhoAmI_1
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
