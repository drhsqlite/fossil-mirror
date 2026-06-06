/**
   Code for the forum family of pages. Requires fossil.X where X is
   (copybutton, pikchr, confirmer, attach, tabs, storage).
*/
(function(F/*the fossil object*/){
  "use strict";
  /* JS code for /forumpost and friends. Requires fossil.dom
     and can optionally use fossil.pikchr. */
  const P = F.page, D = F.dom;

  let idCounter = 0;
  /**
     A WIP forum post editor widget for both new posts and responses.
  */
  class ForumPostEditor {
    /* Options */
    #opt;
    /* Dom elements */
    #e;
    /* Attacher */
    #att;
    /* Is waiting on a pending remote response. */
    #isWaiting = false;
    /* F.TabManager */
    #tabs;
    /* Elements to disable while an XHR is pending. */
    #toDisable = [];
    /* DOM element of the current active tab. */
    #activeTab;
    /* Extra input[type=hidden] fields imported from fossil's
       static page generation. */
    #extraFields;

    /**
       Options:

       opt.draftKey[string=undefined]: if set then this object's state
       will be stored in fossil.storage when the relevant input fields
       lose focus. If old state is found, the form is pre-populated
       from it.  The state is cleared on a successful submit.
    */
    constructor(opt){
      opt = this.#opt = F.nu({
        // todo: defaults once we determine the options
        // replyTo: hash
        // edit: hash
        draftKey: undefined
      }, opt);
      opt.isNewThread = !opt.replyTo && !opt.edit;
      if( !opt.draftKey) opt.draftKey = '';
      const e = this.#e = F.nu({
        mimetype: F.nu(),
        button: F.nu()
      });
      const wrapper = e.widget = D.addClass(D.div(), 'ForumPostEditor');
      D.clearElement(wrapper);
      if( !opt.inReplyTo ){
        e.titleBar = D.addClass(D.div(),'titlebar');
        e.title = D.addClass(D.input('text'), 'title');
        e.title.setAttribute('maxlength', 125);
        e.titleBar.append(
          D.append(D.span(), "Title:"),
          e.title
        );
        if( opt.draftKey ){
          const key = opt.draftKey+'.title';
          e.title.addEventListener('blur', ()=>{
            F.storage.set(key, e.title.value)
          });
          e.title.value = F.storage.get(key,'');
        }
        wrapper.append(e.titleBar);
      }
      e.mimetype.wrapper = D.addClass(D.div(), 'mimetype-wrapper');
      e.mimetype.select = D.addClass(D.select(), 'mimetype-select');
      this.#toDisable.push(e.mimetype.select);
      e.mimetype.label = D.span();
      e.mimetype.label.append(
        D.a(F.repoUrl('markup_help'), 'Markup style'),
        ':'
      );
      e.mimetype.wrapper.append(e.mimetype.label, e.mimetype.select);
      let i = 0;
      for(const [k,v] of Object.entries({
        'text/x-markdown': 'Markdown',
        'text/x-fossil-wiki': 'Fossil Wiki',
        'text/plain': 'Plain text'
      })) {
        const o = D.option(e.mimetype.select, k, v);
        if( !i++ ) o.setAttribute('selected', '');
      }

      e.button.preview = D.button("Preview", e=>this.#preview());
      e.button.submit = D.button("Submit");
      if( 1 ){
        F.confirmer(e.button.submit, {
          confirmText: "Confirm submit...",
          onconfirm: ()=>this.#submit()
        });
      }else{
        e.button.submit.addEventListener('click', ()=>this.#submit());
      }
      e.button.submit.setAttribute('disabled', '');
      e.buttons = D.addClass(D.div(), 'buttons');
      wrapper.append(e.buttons);

      e.err = D.addClass(D.div(), 'error', 'hidden');
      wrapper.append(e.err);
      e.err.addEventListener('dblclick',()=>this.reportError());

      const idPrefix = 'FormPostEditor'+(++idCounter)/* TabManager requires IDs */;
      { /* Main tabs... */
        e.tabs = D.attr(
          D.addClass(D.div(), 'tab-container'),
          'id', idPrefix+'-tabs'
        );
        this.#tabs = new F.TabManager(e.tabs);
        this.#tabs.addEventListener('before-switch-to', (ev)=>{
          this.#activeTab = ev.detail;
          if( e.preview === this.#activeTab ){
            this.#e.button.preview.click();
          }
        });
        wrapper.append( e.tabs );

        e.tabEdit = D.div();
        e.tabEdit.classList.add('editor-wrapper');
        e.editor = D.attr(
          D.addClass(D.textarea(), 'editor'),
          'placeholder',
          'Your message to other forum-goers...'
        );
        e.tabEdit.append(e.editor);
        e.tabEdit.dataset.tabLabel = 'Edit';
        this.#tabs.addTab( e.tabEdit );
        this.#tabs.switchToTab( e.tabEdit );
        if( opt.draftKey ){
          const key = opt.draftKey+'.content';
          this.editorContent = F.storage.get(key,'');
          e.editor.addEventListener(
            'blur', ()=>F.storage.set(key, this.editorContent)
          );
        }
        e.preview = D.addClass(D.div(), 'preview');
        e.preview.dataset.tabLabel = 'Preview';
        this.#tabs.addTab( e.preview );
      }

      if( F.user.enableDebug ){
        e.debug = D.addClass(D.div(), 'debug');
        e.debug.dataset.tabLabel = 'Debug';
        e.debug.setAttribute('id', idPrefix+'-debug');
        for(const [k,v] of Object.entries({
          dryrun: 'Dry run',
          domod: 'Require moderation approval',
          showqp: 'Show query parameters',
          fpsilent: 'Do not send notification emails'
        })){
          const lbl = D.label(false, v);
          lbl.prepend(D.checkbox(k));
          e.debug.append(lbl);
        }
        this.#tabs.addTab(e.debug);
      }
      e.buttons.append(e.mimetype.wrapper);
      if( F.user.mayAttachForum ){
        this.#att = new F.Attacher({
          reverse: true
        });
        //e.buttons.append( e.button.addAttach = this.#att.takeAddButton() );
        e.tabAttach = D.append(D.div(), this.#att.widget);
        e.tabAttach.setAttribute('id', idPrefix+'-attach');
        e.tabAttach.dataset.tabLabel = 'Attachments';
        this.#tabs.addTab(e.tabAttach);
        /* Reminder: we don't currently have a way to disable/enable
           an Attacher's controls. */
      }
      e.buttons.append(e.button.preview, e.button.submit);
      this.#toDisable.push(e.button.preview);

      if( opt.hiddenFields ){
        this.addHiddenFields( opt.hiddenFields );
        delete opt.hiddenFields;
      }

      { /* Shift-enter pieces... */
        const eCb = D.checkbox(1);
        const eLbl = D.label();
        const eHelp = D.append(
          D.span(), [
            'When checked, shift-enter will toggle between preview ',
            'and edit modes, which is generally useful but some ',
            'software keyboards misinteract with it. If the preview ',
            'starts when tapping Enter, turn this setting off.'
          ].join('')
        );
        eCb.checked = F.storage.getBool('edit-shift-enter-preview', true);
        eCb.addEventListener('change', (ev)=>{
          F.storage.set('edit-shift-enter-preview', eCb.checked);
        });
        F.helpButtonlets.setup(eHelp);
        eLbl.append("Shift-enter toggles preview?", eCb, eHelp);
        e.buttons.append(eLbl);
        const isShiftEnter = (ev)=>eCb.checked && ev.shiftKey && 13===ev.keyCode;
        e.editor.addEventListener('keydown',(ev)=>{
          /**
             If eCb.checked is true, a keyboard combo of shift-enter
             (from the editor) toggles between preview and edit modes.
             This is normally desired but at least one software
             keyboard is known to misinteract with this, treating an
             Enter after automatically-capitalized letters as a
             shift-enter:

             https://fossil-scm.org/forum/forumpost/dbd5b68366147ce8
          */
          if(!isShiftEnter(ev)) return;
          ev.preventDefault();
          ev.stopPropagation();
          e.editor.blur(/*force change event, if needed*/);
          this.#tabs.switchToTab(e.preview);
          this.#preview();
        }, false);
        // If we're in the preview tab, have ctrl-enter switch back to the editor.
        document.body.addEventListener('keydown',(ev)=>{
          if(!isShiftEnter(ev)) return;
          if(this.#activeTab !== e.tabEdit){
            ev.preventDefault();
            ev.stopPropagation();
            this.#tabs.switchToTab(e.tabEdit);
            e.editor.focus(/*slow as molasses for long docs, as focus()
                             forces a document reflow. */);
            return false;
          }
        }, true);
      }/*shift-enter preview bits*/

    }/*constructor*/

    /** This widget's top-most DOM element. */
    get widget(){
      return this.#e.widget;
    }

    get editorContent(){
      /* We wrap access to the editor's contents in a getter/setter so
         that we can eventually add optional use of a contenteditable
         edit field, as those are generally more comfortable. The code
         for that is in fossil.page.chat.js. */
      return this.#e.editor.value;
    }

    set editorContent(v){
      this.#e.editor.value = v;
    }

    /** Clears any draft state. */
    clearDraft(){
      const k = this.#opt.draftKey;
      if( k ){
        F.storage.remove(k+'.content');
        F.storage.remove(k+'.title');
      }
    }

    /**
       Reports an error by appending each argument to the error widget
       and unhiding it. If passed no arugments, it clears and hides
       the error widget.
    */
    reportError(...msg){
      const e = this.#e.err;
      D.clearElement(e);
      if( msg.length ){
        e.classList.remove('hidden');
        e.append(...msg);
      }else{
        e.classList.add('hidden');
      }
    }

    /**
       Adds a list of input[type=hidden] form fields to this object,
       imported from the server-generated HTML. This is used for collecting,
       e.g., the CSRF token.
    */
    addHiddenFields(list){
      this.#extraFields ??= [];
      for( const f of list ){
        if( 'title'===f.name && this.#opt.isNewThread ){
          if( !this.#e.title.value ){
            this.#e.title.value = f.value;
          }
        }else{
          this.#extraFields.push(f);
        }
      }
    }

    get mimetype(){
      return this.#e.mimetype.select.value;
    }

    async #fetchPreview(content){
      /* TODO: fetch preview */
      const e = this.#e;
      const fd = new FormData;
      for(const f of this.#extraFields){
        fd.append(f.name, f.value);
      }
      fd.append('mimetype', this.mimetype);
      fd.append('content', content);
      return window
        .fetch(F.repoUrl('wikiajax/preview'), {
          method: 'POST',
          body: fd
        })
        .then(r=>r.text())
        .then(t=>{
          if( /^\{.*}$/.test(t) ){
            const o = JSON.parse(t);
            throw new Error(o.error);
          }
          return t;
        });
    }

    #setPreviewContent(rawHtml){
      const preview = this.#e.preview;
      D.clearElement(preview);
      D.parseHtml(preview, rawHtml);
      if(F.pikchr && 'text/x-markdown'===this.mimetype){
        F.pikchr.addSrcView(
          preview.querySelectorAll('svg.pikchr')
        );
      }
    }

    async #preview(){
      if( this.#isWaiting ) return;
      const e = this.#e;
      if( e.preview !== this.#activeTab ){
        this.#tabs.switchToTab(e.preview);
        /* Will recurse into here */
        return;
      }
      this.#isWaiting = true;
      D.clearElement(e.preview);
      const content = this.editorContent.trim();
      if( !content ){
        return;
      }
      D.disable(this.#toDisable, e.button.submit);
      e.preview.textContent = "Fetching preview...";
      this.#fetchPreview(content)
        .then((c)=>{
          this.#setPreviewContent(c);
          D.enable(this.#toDisable, e.button.submit);
        })
        .catch(err=>{
          e.preview.textContent = "Error fetching preview: "+err.message;
          console.error("Error fetching preview:",err);
          this.reportError(err.message);
        })
        .finally(()=>{
          this.#isWaiting = false;
          D.enable(this.#toDisable);
        });
    }

    #validate(){
      let v = this.#e.title.value.trim();
      if( !v ){
        this.reportError("A non-empty title is required.");
        return;
      }
      return true;
    }

    #submit(){
      if( this.#isWaiting ) return;
      if( !this.#validate() ) return;
      this.#isWaiting = true;
      const e = this.#e;
      D.disable(e.button.submit);
      this.reportError("Submit is TODO.");
      if( opt.draftKey ){
        F.storage.remove(opt.draftKey+'.content');
        F.storage.remove(opt.draftKey+'.title');
      }
      /*
        TODO: save it, set #isWaiting=false, then handle error or
        redirect to the post (if this is a new post) or, if replying
        inline, replace this object with a static rendering from the
        response.
      */
    }

    async #fetchPost(){
      /*
        TODO: when editing an existing post, fetch the raw body of the
        post and populate this.e.
       */
    }
  }/*ForumPostEditor*/;
  F.ForumPostEditor = ForumPostEditor;

  /**
     When the page is loaded, this handler does the following:

     - Installs expand/collapse UI elements on "long" posts and collapses
     them.

     - Any pikchr-generated SVGs get a source-toggle button added to them
     which activates when the mouse is over the image or it is tapped.

     This is a harmless no-op if the current page has neither forum
     post constructs for (1) nor any pikchr images for (2), nor will
     NOT running this code cause any breakage for clients with no JS
     support: this is all "nice-to-have", not required functionality.
  */
  F.onPageLoad(function(){
    const scrollbarIsVisible = (e)=>e.scrollHeight > e.clientHeight;
    /* Returns an event handler which implements the post expand/collapse toggle
       on contentElem when the given widget is activated. */
    const getWidgetHandler = function(widget, contentElem){
      return function(ev){
        if(ev) ev.preventDefault();
        const wasExpanded = widget.classList.contains('expanded');
        widget.classList.toggle('expanded');
        contentElem.classList.toggle('expanded');
        if(wasExpanded){
          contentElem.classList.add('shrunken');
          contentElem.parentElement.scrollIntoView({
            /* This is non-standard, but !(MSIE, Safari) supposedly support it:
               https://developer.mozilla.org/en-US/docs/Web/API/Element/scrollIntoView#Browser_compatibility
            */ behavior: 'smooth'
          });
        }else{
          contentElem.classList.remove('shrunken');
        }
        return false;
      };
    };

    /* Adds an Expand/Collapse toggle to all div.forumPostBody
       elements which are deemed "too large" (those for which
       scrolling is currently activated because they are taller than
       their max-height). */
    document.querySelectorAll(
      'div.forumTime, div.forumEdit'
    ).forEach(function f(forumPostWrapper){
      const content = forumPostWrapper.querySelector('div.forumPostBody');
      if(!content || !scrollbarIsVisible(content)) return;
      const parent = content.parentElement,
            widget =  D.addClass(
              D.div(),
              'forum-post-collapser','bottom'
            ),
            rightTapZone = D.addClass(
              D.div(),
              'forum-post-collapser','right'
            );
      /* Repopulates the rightTapZone with arrow indicators. Because
         of the wildly varying height of these elements, This has to
         be done dynamically at init time and upon collapse/expand. Will not
         work until the rightTapZone has been added to the DOM. */
      const refillTapZone = function f(){
        if(!f.baseTapIndicatorHeight){
          /* To figure out how often to place an arrow in the rightTapZone,
             we simply grab the first header element from the page and use
             its hight as our basis for calculation. */
          const h1 = document.querySelector('h1, h2');
          f.baseTapIndicatorHeight = h1.getBoundingClientRect().height;
        }
        D.clearElement(rightTapZone);
        var rtzHeight = parseInt(window.getComputedStyle(rightTapZone).height);
        do {
          D.append(rightTapZone, D.span());
          rtzHeight -= f.baseTapIndicatorHeight * 8;
        }while(rtzHeight>0);
      };
      const handlerStep1 = getWidgetHandler(widget, content);
      const widgetEventHandler = ()=>{ handlerStep1(); refillTapZone(); };
      content.classList.add('with-expander');
      widget.addEventListener('click', widgetEventHandler, false);
      /** Append 3 children, which CSS will evenly space across the
          widget. This improves visibility over having the label
          in only the left, right, or center. */
      var i = 0;
      for( ; i < 3; ++i ) D.append(widget, D.span());
      if(content.nextSibling){
        forumPostWrapper.insertBefore(widget, content.nextSibling);
      }else{
        forumPostWrapper.appendChild(widget);
      }
      content.appendChild(rightTapZone);
      rightTapZone.addEventListener('click', widgetEventHandler, false);
      refillTapZone();
    })/*for-each div.forumTime|div.forumEdit*/;

    if(F.pikchr){
      F.pikchr.addSrcView();
    }

    const eStatus = document.querySelector(
      'form div.submenu select.submenuctrl[name="status"]'
    );
    if( eStatus ){
      /* Main /forum list. Remove the 'x' form element when eStatus
      ** changes, to avoid propagating x when changing the filter. */
      const pForm = eStatus.parentElement?.parentElement;
      if( pForm ){
        eStatus.addEventListener('change', ()=>{
          pForm.querySelector('input[type="hidden"][name="x"]')?.remove?.();
        }, true);
      }
    }else{
      /* One of the single-post edit/view pages.  Handle various UI
         controls and attempt to keep stray double-clicks from
         double-posting.
         https://fossil-scm.org/forum/info/6bd02466533aa131 */
      const formSubmitted = function(event){
        const form = event.target;
        if( form.dataset.submitted ){
          event.preventDefault();
          return;
        }
        form.dataset.submitted = '1';
        /** If the user is left waiting "a long time," disable the
            resubmit protection. If we don't do this and they tap the
            browser's cancel button while waiting, they'll be stuck with
            an unsubmittable form. */
        setTimeout(()=>{delete form.dataset.submitted}, 7000);
        return;
      };

      document.querySelectorAll("form").forEach(function(form){
        /* Set up controls for closing posts and setting thread
           status. */
        form.addEventListener('submit', formSubmitted);
        form
          .querySelectorAll("input.action-close, input.action-reopen")
          .forEach(function(e){
            e.classList.remove('hidden');
            F.confirmer(e, {
              confirmText: (e.classList.contains('action-reopen')
                            ? "Confirm re-open"
                            : "Confirm close"),
              onconfirm: ()=>form.submit()
            });
          });
        form
          .querySelectorAll("input[type='button'].action-status")
          .forEach(function(btn){
            btn.classList.remove('hidden');
            const sel = btn.previousElementSibling;
            const updateButton = ()=>{
              /* Enable btn only when the status has been locally
                 modified. */
              if( sel.dataset.initialValue ){
                if( sel.dataset.initialValue===sel.value ){
                  btn.setAttribute('disabled','');
                }else{
                  btn.removeAttribute('disabled');
                }
              }else{
                if(sel.selectedIndex===0){
                  btn.setAttribute('disabled','');
                }else{
                  btn.removeAttribute('disabled');
                }
              }
            };
            sel.addEventListener('change', updateButton, true);
            updateButton();
            F.confirmer(btn, {
              confirmText: "Confirm status change",
              onconfirm: ()=>form.submit()
            });
          });
      });
    }

    const eForumNew = document.body.classList.contains('cpage-forumnew')
          ? document.querySelector('#forumnew-placeholder')
          : null;
    if( eForumNew ){
      /* /forumnew */
      const fpe = new fossil.ForumPostEditor({
        draftKey: 'forumnew',
        hiddenFields: eForumNew.querySelectorAll('input[type=hidden]')
      });
      eForumNew.parentElement.insertBefore(fpe.widget, eForumNew);
      eForumNew.remove();
      fossil.page.fpe = fpe /* for testing via the console */;
    }/*eForumNew*/
  })/*F.onPageLoad callback*/;
})(window.fossil);
