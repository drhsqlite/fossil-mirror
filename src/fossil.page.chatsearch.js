/*
** This file contains the client-side implementation of fossil's
** /chat-search application.
*/
window.fossil.onPageLoad(function(){

  const F = window.fossil, D = F.dom;
  const E1 = function(selector){
    const e = document.querySelector(selector);
    if(!e) throw new Error("missing required DOM element: "+selector);
    return e;
  };

/************************************************************************/
/************************************************************************/
/************************************************************************/

  /**
     Custom widget type for rendering messages (one message per
     instance). These are modelled after FIELDSET elements but we
     don't use FIELDSET because of cross-browser inconsistencies in
     features of the FIELDSET/LEGEND combination, e.g. inability to
     align legends via CSS in Firefox and clicking-related
     deficiencies in Safari.
  */
  var MessageWidget = (function(){
    /**
       Constructor. If passed an argument, it is passed to
       this.setMessage() after initialization.
    */
    const cf = function(){
      this.e = {
        body: D.addClass(D.div(), 'message-widget'),
        tab: D.addClass(D.div(), 'message-widget-tab'),
        content: D.addClass(D.div(), 'message-widget-content')
      };
      D.append(this.e.body, this.e.tab, this.e.content);
      this.e.tab.setAttribute('role', 'button');
      if(arguments.length){
        this.setMessage(arguments[0]);
      }
    };

    /**
       Returns true if this page believes it can embed a view of the
       file wrapped by the given message object, else returns false.
    */
    const canEmbedFile = function f(msg){
      if(!f.$rx){
        f.$rx = /\.((html?)|(txt)|(md)|(wiki)|(pikchr))$/i;
        f.$specificTypes = [
          'text/plain',
          'text/html',
          'text/x-markdown',
          /* Firefox sends text/markdown when uploading .md files */
          'text/markdown',
          'text/x-pikchr',
          'text/x-fossil-wiki'
          // add more as we discover which ones Firefox won't
          // force the user to try to download.
        ];
      }
      if(msg.fmime){
        if(msg.fmime.startsWith("image/")
           || f.$specificTypes.indexOf(msg.fmime)>=0){
          return true;
        }
      }
      return (msg.fname && f.$rx.test(msg.fname));
    };

    /**
      Returns true if the given message object "should"
      be embedded in fossil-rendered form instead of
      raw content form. This is only intended to be passed
      message objects for which canEmbedFile() returns true.
    */
    const shouldWikiRenderEmbed = function f(msg){
      if(!f.$rx){
        f.$rx = /\.((md)|(wiki)|(pikchr))$/i;
        f.$specificTypes = [
          'text/x-markdown',
          'text/markdown' /* Firefox-uploaded md files */,
          'text/x-pikchr',
          'text/x-fossil-wiki'
          // add more as we discover which ones Firefox won't
          // force the user to try to download.
        ];
      }
      if(msg.fmime){
        if(f.$specificTypes.indexOf(msg.fmime)>=0) return true;
      }
      return msg.fname && f.$rx.test(msg.fname);
    };

    const adjustIFrameSize = function(msgObj){
      const iframe = msgObj.e.iframe;
      const body = iframe.contentWindow.document.querySelector('body');
      if(body && !body.style.fontSize){
        /** _Attempt_ to force the iframe to inherit the message's text size
            if the body has no explicit size set. On desktop systems
            the size is apparently being inherited in that case, but on mobile
            not. */
        body.style.fontSize = window.getComputedStyle(msgObj.e.content);
      }
      if('' === iframe.style.maxHeight){
        /* Resize iframe height to fit the content. Workaround: if we
           adjust the iframe height while it's hidden then its height
           is 0, so we must briefly unhide it. */
        const isHidden = iframe.classList.contains('hidden');
        if(isHidden) D.removeClass(iframe, 'hidden');
        iframe.style.maxHeight = iframe.style.height
          = iframe.contentWindow.document.documentElement.scrollHeight + 'px';
        if(isHidden) D.addClass(iframe, 'hidden');
      }
    };

    cf.prototype = {
      scrollIntoView: function(){
        this.e.content.scrollIntoView();
      },
      setMessage: function(m){
        const ds = this.e.body.dataset;
        ds.timestamp = m.mtime;
        ds.lmtime = m.lmtime;
        ds.msgid = m.msgid;
        ds.xfrom = m.xfrom || '';

        if(m.uclr){
          this.e.content.style.backgroundColor = m.uclr;
          this.e.tab.style.backgroundColor = m.uclr;
        }
        const d = new Date(m.mtime);
        D.clearElement(this.e.tab);
        var contentTarget = this.e.content;
        var eXFrom /* element holding xfrom name */;
        var eXFrom = D.append(D.addClass(D.span(), 'xfrom'), m.xfrom);
        const wrapper = D.append(
            D.span(), eXFrom,
            D.text(" #",(m.msgid||'???'),' @ ',d.toLocaleString()));
        D.append(this.e.tab, wrapper);

        if( m.xfrom && m.fsize>0 ){
          if( m.fmime
              && m.fmime.startsWith("image/")
              /* && Chat.settings.getBool('images-inline',true) */
            ){
            const extension = m.fname.split('.').pop();
            contentTarget.appendChild(D.img("chat-download/" + m.msgid +(
              extension ? ('.'+extension) : ''/*So that IMG tag mimetype guessing works*/
            )));
            ds.hasImage = 1;
          }else{
            // Add a download link.
            const downloadUri = window.fossil.rootPath+
                  'chat-download/' + m.msgid+'/'+encodeURIComponent(m.fname);
            const w = D.addClass(D.div(), 'attachment-link');
            const a = D.a(downloadUri,
              // ^^^ add m.fname to URL to cause downloaded file to have that name.
              "(" + m.fname + " " + m.fsize + " bytes)"
            )
            D.attr(a,'target','_blank');
            D.append(w, a);
            if(canEmbedFile(m)){
              /* Add an option to embed HTML attachments in an iframe. The primary
                 use case is attached diffs. */
              const shouldWikiRender = shouldWikiRenderEmbed(m);
              const downloadArgs = shouldWikiRender ? '?render' : '';
              D.addClass(contentTarget, 'wide');
              const embedTarget = this.e.content;
              const self = this;
              const btnEmbed = D.attr(D.checkbox("1", false), 'id',
                                      'embed-'+ds.msgid);
              const btnLabel = D.label(btnEmbed, shouldWikiRender
                                       ? "Embed (fossil-rendered)" : "Embed");
              /* Maintenance reminder: do not disable the toggle
                 button while the content is loading because that will
                 cause it to get stuck in disabled mode if the browser
                 decides that loading the content should prompt the
                 user to download it, rather than embed it in the
                 iframe. */
              btnEmbed.addEventListener('change',function(){
                if(self.e.iframe){
                  if(btnEmbed.checked){
                    D.removeClass(self.e.iframe, 'hidden');
                    if(self.e.$iframeLoaded) adjustIFrameSize(self);
                  }
                  else D.addClass(self.e.iframe, 'hidden');
                  return;
                }
                const iframe = self.e.iframe = document.createElement('iframe');
                D.append(embedTarget, iframe);
                iframe.addEventListener('load', function(){
                  self.e.$iframeLoaded = true;
                  adjustIFrameSize(self);
                });
                iframe.setAttribute('src', downloadUri + downloadArgs);
              });
              D.append(w, btnEmbed, btnLabel);
            }
            contentTarget.appendChild(w);
          }
        }
        if(m.xmsg){
          if(m.fsize>0){
            /* We have file/image content, so need another element for
               the message text. */
            contentTarget = D.div();
            D.append(this.e.content, contentTarget);
          }
          D.addClass(contentTarget, 'content-target'
                     /*target element for the 'toggle text mode' feature*/);
          // The m.xmsg text comes from the same server as this script and
          // is guaranteed by that server to be "safe" HTML - safe in the
          // sense that it is not possible for a malefactor to inject HTML
          // or javascript or CSS.  The m.xmsg content might contain
          // hyperlinks, but otherwise it will be markup-free.  See the
          // chat_format_to_html() routine in the server for details.
          //
          // Hence, even though innerHTML is normally frowned upon, it is
          // perfectly safe to use in this context.
          if(m.xmsg && 'string' !== typeof m.xmsg){
            // Used by Chat.reportErrorAsMessage()
            D.append(contentTarget, m.xmsg);
          }else{
            contentTarget.innerHTML = m.xmsg;
            // contentTarget.querySelectorAll('a').forEach(addAnchorTargetBlank);
            if(F.pikchr){
              F.pikchr.addSrcView(contentTarget.querySelectorAll('svg.pikchr'));
            }
          }
        }
        //console.debug("tab",this.e.tab);
        //console.debug("this.e.tab.firstElementChild",this.e.tab.firstElementChild);
        // this.e.tab.firstElementChild.addEventListener('click', this._handleLegendClicked, false);
        /*if(eXFrom){
          eXFrom.addEventListener('click', ()=>this.e.tab.click(), false);
        }*/
        return this;
      }
    };
    return cf;
  })()/*MessageWidget*/;

/************************************************************************/
/************************************************************************/
/************************************************************************/

  var MessageSpacer = (function(){
    const nMsgContext = 5;
    const zUpArrow = '\u25B2';
    const zDownArrow = '\u25BC';

    const cf = function(o){

      /* iFirstInTable: 
      **   msgid of first row in chatfts table.
      **
      ** iLastInTable: 
      **   msgid of last row in chatfts table.
      **
      ** iPrevId:
      **   msgid of message immediately above this spacer. Or 0 if this
      **   spacer is above all results.
      **
      ** iNextId:
      **   msgid of message immediately below this spacer. Or 0 if this
      **   spacer is below all results.
      **
      ** bIgnoreClick:
      **   ignore any clicks if this is true. This is used to ensure there
      **   is only ever one request belonging to this widget outstanding
      **   at any time.
      */ 
      this.o = {
        iFirstInTable: o.first,
        iLastInTable: o.last,
        iPrevId: o.previd,
        iNextId: o.nextid,
        bIgnoreClick: false,
      };

      this.e = {
        body:    D.addClass(D.div(), 'spacer-widget'),

        above:   D.addClass(D.div(), 'spacer-widget-above'),
        buttons: D.addClass(D.div(), 'spacer-widget-buttons'),
        below:   D.addClass(D.div(), 'spacer-widget-below'),

        up:      D.button(zDownArrow+' Load '+nMsgContext+' more '+zDownArrow),
        down:    D.button(zUpArrow+' Load '+nMsgContext+' more '+zUpArrow),
        all:     D.button('Load More'),
      };

      D.addClass(this.e.up, 'up');
      D.addClass(this.e.down, 'down');
      D.addClass(this.e.all, 'all');

      D.append(this.e.buttons, this.e.up, this.e.down, this.e.all);
      D.append(this.e.body, this.e.above, this.e.buttons, this.e.below);

      this.e.up.style.float = 'left';
      this.e.down.style.float = 'left';
      this.e.all.style.float = 'left';
      this.e.below.style.clear = 'both';

      const ms = this;
      this.e.up.addEventListener('click', function(){
        ms.load_messages(false);
      });
      this.e.down.addEventListener('click', function(){
        ms.load_messages(true);
      });
      this.e.all.addEventListener('click', function(){
        ms.load_messages( (ms.o.iPrevId==0) );
      });

      this.set_button_visibility();
    };

    cf.prototype = {
      set_button_visibility: function() {
        var o = this.o;

        var iPrevId = (o.iPrevId!=0) ? o.iPrevId : o.iFirstInTable-1;
        var iNextId = (o.iNextId!=0) ? o.iNextId : o.iLastInTable+1;
        var nDiff = (iNextId - iPrevId) - 1;

        this.e.up.style.display = 'none';
        this.e.down.style.display = 'none';
        this.e.all.style.display = 'none';

        if( nDiff>0 ){

          if( nDiff>nMsgContext && (o.iPrevId==0 || o.iNextId==0) ){
            nDiff = nMsgContext;
          }

          if( nDiff<=nMsgContext && o.iPrevId!=0 && o.iNextId!=0 ){
            this.e.all.style.display = 'block';
            this.e.all.innerText = (
              zUpArrow + " Load " + nDiff + " more " + zDownArrow
            );
          }else{
            if( o.iPrevId!=0 ) this.e.up.style.display = 'block';
            if( o.iNextId!=0 ) this.e.down.style.display = 'block';
          }
        }
      },

      load_messages: function(bDown) {
        var iFirst = 0;           /* msgid of first message to fetch */
        var nFetch = 0;           /* Number of messages to fetch */
        var iEof = 0;             /* last msgid in spacers range, plus 1 */

        var e = this.e;
        var o = this.o;

        if( this.bIgnoreClick ) return;
        this.bIgnoreClick = true;

        /* Figure out the required range of messages. */
        if( bDown ){
          iFirst = this.o.iNextId - nMsgContext;
          if( iFirst<this.o.iFirstInTable ){
            iFirst = this.o.iFirstInTable;
          }
        }else{
          iFirst = this.o.iPrevId+1;
        }
        nFetch = nMsgContext;
        iEof = (this.o.iNextId > 0) ? this.o.iNextId : this.o.iLastInTable+1;
        if( iFirst+nFetch>iEof ){
          nFetch = iEof - iFirst;
        }


        const ms = this;
        F.fetch("chat-query",{
          urlParams:{
            q: '',
            n: nFetch,
            i: iFirst
          },
          responseType: "json",

          onerror:function(err){
            console.error(err);
            alert(err.toString());
          },

          onload:function(jx){
            const firstChildOfBelow = e.below.firstChild;
            jx.msgs.forEach((m) => {
              var mw = new MessageWidget(m);
              if( bDown ){
                e.below.insertBefore(mw.e.body, firstChildOfBelow);
              }else{
                D.append(e.above, mw.e.body);
              }
            });

            if( bDown ){
              o.iNextId -= jx.msgs.length;
            }else{
              o.iPrevId += jx.msgs.length;
            }

            ms.set_button_visibility();
            ms.bIgnoreClick = false;
          }
        });
      }
    };

    return cf;
  })(); /* MessageSpacer */

  /* This is called to submit a search - because the user clicked the
  ** search button or pressed Enter in the input box.
  */
  const submit_search = function() {
    const v = E1('#textinput').value;
    F.fetch("chat-query",{
      urlParams:{
        q: v
      },
      responseType: "json",

      onerror:function(err){
        console.error(err);
        alert(err.toString());
      },

      onload:function(jx){
        var res = E1('#results');
        var previd = 0;

        D.clearElement(res);
        jx.msgs.forEach((m) => {
          var mw = new MessageWidget(m);
          var spacer = new MessageSpacer({
            first: jx.first,
            last: jx.last,
            previd: previd,
            nextid: m.msgid
          });

          D.append( res, spacer.e.body );
          D.append( res, mw.e.body );

          previd = m.msgid;
        });

        if( jx.msgs.length>0 ){
          var spacer = new MessageSpacer({
            first: jx.first,
            last: jx.last,
            previd: previd,
            nextid: 0
          });
          D.append( res, spacer.e.body );
        } else {
          res.innerHTML = '<center><i>No query results</i></center>';
        }

        window.scrollTo(0, E1('body').scrollHeight);
      }
    });
  }

  /* Add event listeners to call submit_search() if the user presses Enter
  ** or clicks the search button.
  */
  E1('#searchbutton').addEventListener('click', function(){
    submit_search();
  });
  E1('#textinput').addEventListener('keydown', function(ev){
    if( 13==ev.keyCode ){
      /* If the key pressed was Enter */
      submit_search();
    }
  });

  /* Focus the input widget */
  E1('#textinput').focus();

});
