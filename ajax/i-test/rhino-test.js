var TestApp = {
    serverUrl:
        'http://localhost:8080'
        //'http://fjson/cgi-bin/fossil-json.cgi'
        //'http://192.168.1.62:8080'
        //'http://fossil.wanderinghorse.net/repos/fossil-json-java/index.cgi'
        ,
    verbose:true,
    fossilBinary:'fossil',
    wiki:{}
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
        asynchronous:false, /* rhino-based impl doesn't support async or timeout. */
        timeout:0,
        url:TestApp.serverUrl,
        fossilBinary:TestApp.fossilBinary
    });
    var cb = TestApp.fossil.ajaj.callbacks;
    cb.beforeSend = function(req,opt){
        if(!TestApp.verbose) return;
        print("SENDING REQUEST: AJAJ options="+JSON.stringify(opt));
        if(req) print("Request envelope="+WhAjaj.stringify(req));
    };
    cb.afterSend = function(req,opt){
        //if(!TestApp.verbose) return;
        //print("REQUEST RETURNED: opt="+JSON.stringify(opt));
        //if(req) print("Request="+WhAjaj.stringify(req));
    };
    cb.onError = function(req,opt){
        if(!TestApp.verbose) return;
        print("ERROR: "+WhAjaj.stringify(opt));
    };
    cb.onResponse = function(resp,req){
        if(!TestApp.verbose) return;
        print("GOT RESPONSE: "+(('string'===typeof resp) ? resp : WhAjaj.stringify(resp)));
    };

})();

/**
    Throws an exception of cond is a falsy value.
*/
function assert(cond, descr){
    descr = descr || "Undescribed condition.";
    if(!cond){
        print("Assertion FAILED: "+descr);
        throw new Error("Assertion failed: "+descr);
        // aarrgghh. Exceptions are of course swallowed by
        // the AJAX layer, to keep from killing a browser's
        // script environment.
    }else{
        if(TestApp.verbose) print("Assertion OK: "+descr);
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
        if(TestApp.verbose) print("Function threw (as expected): "+descr+": "+ex);
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
    assert( undefined === resp.resultCode, 'resp.resultCode is not set');
}
/**
    Asserts that resp is-a Object, resp.fossil is-a string, and
    resp.resultCode is a truthy value. If expectCode is set then
    it also asserts that (resp.resultCode=='FOSSIL-'+expectCode).
*/
function assertResponseError(resp,expectCode){
    assert('object' === typeof resp,'Response is-a object.');
    assert( 'string' === typeof resp.fossil, 'Response contains fossil property.');
    assert( !!resp.resultCode, 'resp.resultCode='+resp.resultCode);
    if(expectCode){
        assert( 'FOSSIL-'+expectCode == resp.resultCode, 'Expecting result code '+expectCode );
    }
}

function testHAI(){
    var rs;
    TestApp.fossil.HAI({
        onResponse:function(resp,req){
            rs = resp;
        }
    });
    assertResponseOK(rs);
    TestApp.serverVersion = rs.fossil;
    assert( 'string' === typeof TestApp.serverVersion, 'server version = '+TestApp.serverVersion);
}
testHAI.description = 'Get server version info.';

function testIAmNobody(){
    TestApp.fossil.whoami('/json/whoami');
    assert('nobody' === TestApp.fossil.auth.name, 'User == nobody.' );
    assert(!TestApp.fossil.auth.authToken, 'authToken is not set.' );

}
testIAmNobody.description = 'Ensure that current user is "nobody".';


function testAnonymousLogin(){
    TestApp.fossil.login();
    assert('string' === typeof TestApp.fossil.auth.authToken, 'authToken = '+TestApp.fossil.auth.authToken);
    assert( 'string' === typeof TestApp.fossil.auth.name, 'User name = '+TestApp.fossil.auth.name);
    TestApp.fossil.userName = null;
    TestApp.fossil.whoami('/json/whoami');
    assert( 'string' === typeof TestApp.fossil.auth.name, 'User name = '+TestApp.fossil.auth.name);
}
testAnonymousLogin.description = 'Perform anonymous login.';

function testAnonWiki(){
    var rs;
    TestApp.fossil.sendCommand('/json/wiki/list',undefined,{
        beforeSend:function(req,opt){
            assert( req && (req.authToken==TestApp.fossil.auth.authToken), 'Request envelope contains expected authToken.'  );
        },
        onResponse:function(resp,req){
            rs = resp;
        }
    });
    assertResponseOK(rs);
    assert( (typeof [] === typeof rs.payload) && rs.payload.length,
        "Wiki list seems to be okay.");
    TestApp.wiki.list = rs.payload;

    TestApp.fossil.sendCommand('/json/wiki/get',{
        name:TestApp.wiki.list[0]
    },{
        onResponse:function(resp,req){
            rs = resp;
        }
    });
    assertResponseOK(rs);
    assert(rs.payload.name == TestApp.wiki.list[0], "Fetched page name matches expectations.");
    print("Got first wiki page: "+WhAjaj.stringify(rs.payload));

}
testAnonWiki.description = 'Fetch wiki list as anonymous user.';

function testFetchCheckinArtifact(){
    var art = '18dd383e5e7684ece';
    var rs;
    TestApp.fossil.sendCommand('/json/artifact',{
        'name': art
        },
        {
            onResponse:function(resp,req){
                rs = resp;
            }
        });
    assertResponseOK(rs);
    assert(3 == rs.payload.parents.length, 'Got 3 parent artifacts.');
}
testFetchCheckinArtifact.description = '/json/artifact/CHECKIN';

function testAnonLogout(){
    var rs;
    TestApp.fossil.logout({
        onResponse:function(resp,req){
            rs = resp;
        }
    });
    assertResponseOK(rs);
    print("Ensure that second logout attempt fails...");
    TestApp.fossil.logout({
        onResponse:function(resp,req){
            rs = resp;
        }
    });
    assertResponseError(rs);
}
testAnonLogout.description = 'Log out anonymous user.';

function testExternalProcess(){

    var req = { command:"HAI", requestId:'testExternalProcess()' };
    var args = [TestApp.fossilBinary, 'json', '--json-input', '-'];
    var p = java.lang.Runtime.getRuntime().exec(args);
    var outs = p.getOutputStream();
    var osr = new java.io.OutputStreamWriter(outs);
    var osb = new java.io.BufferedWriter(osr);
    var json = JSON.stringify(req);
    osb.write(json,0, json.length);
    osb.close();
    req = json = outs = osr = osb = undefined;
    var ins = p.getInputStream();
    var isr = new java.io.InputStreamReader(ins);
    var br = new java.io.BufferedReader(isr);
    var line;

    while( null !== (line=br.readLine())){
        print(line);
    }
    br.close();
    isr.close();
    ins.close();
    p.waitFor();
}
testExternalProcess.description = 'Run fossil as external process.';

function testExternalProcessHandler(){
    var aj = TestApp.fossil.ajaj;
    var oldImpl = aj.sendImpl;
    aj.sendImpl = FossilAjaj.rhinoLocalBinarySendImpl;
    var rs;
    TestApp.fossil.sendCommand('/json/HAI',undefined,{
        onResponse:function(resp,opt){
            rs = resp;
        }
    });
    aj.sendImpl = oldImpl;
    assertResponseOK(rs);
    print("Using local fossil binary via AJAX interface, we fetched: "+
        WhAjaj.stringify(rs));
}
testExternalProcessHandler.description = 'Try local fossil binary via AJAX interface.';

(function runAllTests(){
    var testList = [
        testHAI,
        testIAmNobody,
        testAnonymousLogin,
        testAnonWiki,
        testFetchCheckinArtifact,
        testAnonLogout,
        testExternalProcess,
        testExternalProcessHandler
    ];
    var i, f;
    for( i = 0; i < testList.length; ++i ){
        f = testList[i];
        try{
            print("Running test #"+(i+1)+": "+(f.description || "no description."));
            f();
        }catch(e){
            print("Test #"+(i+1)+" failed: "+e);
            throw e;
        }
    }

})();

print("Done! If you don't see an exception message in the last few lines, you win!");
