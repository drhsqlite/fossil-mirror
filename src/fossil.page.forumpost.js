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

  /*
    The problem: when previewing the bottom-most post of a thread, the
    preview widget's size changes cause the page to scroll
    unpredictably as the bottom boundary of the page moves. A weird
    workaround (not invented here) is to add dummy blank padding to
    the page to allow the preview widget to grow and shrink without
    (usually) scrolling, but whether it does so really depends on its
    size.

    We could maybe get the same effect by adding this size as
    padding-bottom to document.body instead of as a new element.
  */
  const dummyPadding = D.div();
  dummyPadding.style.height = '75em';
  /* Keep track of ForumPostEditor instances so we can remove this
     padding when none are active. */
  dummyPadding.refs = new Set();
  F.dummyPadding = dummyPadding /* only for debugging */;

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
    /* Persistent draft message object. */
    #draft;

    /**
       Options:

       opt.draftKey[string=undefined]: if set then this object's state
       will be stored in fossil.storage when the relevant input fields
       lose focus. If old state is found, the form is pre-populated
       from it. The state is cleared on a discard() or successful
       submit.

       opt.ondiscard[=function]: if set, a Discard button is added
       which, when activated, clears the current draft and removes
       this object's widget from the DOM. After doing so,
       opt.ondiscard() is called and passed this object. Exceptions
       thrown by ondiscard() are ignored but may be logged.

       opt.onsubmit[=function]: if set, this function is called
       immediately after the post has been successfully saved, and
       passed this object and a JSON-format response object from the
       save request. It is generally then up to the caller to close()
       this object and/or redirect to /forumpost/${arguments[1].uuid}.

       opt.onclose[=function]: like opt.onsubmit, this function is
       called when this.close() is called, and passed no arguments.
       onclose() is called before the widget is removed from the dom
       and _does not_ fire if it is not in the DOM.

       opt.hiddenFields: an optional list of input elements to
       incorporate into the form for requests which request the
       preview or save the post.

       opt.inReplyTo=uuid: if this is a response to a post, this
       is the full forum post uuid of the being-replied-to post.

       opt.edit=artifactObject: if this is an edit of an existing
       post, this is the full JSON-format artifact of the forum post
       the being-edited post, as returned by /ajax/artifact.json.

       opt.status: optional current status tag value for opt.edit,
       if known. This is used for pre-selecting a status value.
    */
    constructor(opt){
      opt = this.#opt = F.nu({
        // todo: defaults once we determine the options
        // inReplyTo: hash
        // fpid: hash
        draftKey: undefined
      }, opt);
      opt.isNewThread = !opt.inReplyTo && !opt.edit;
      if( opt.draftKey ){
        this.#draft = F.nu(F.storage.getJSON(opt.draftKey, {}));
      }
      const e = this.#e = F.nu({
        mimetype: F.nu(),
        button: F.nu()
      });
      //console.debug("Setting up FPE opt =",opt);
      const wrapper = e.widget = D.addClass(D.div(), 'ForumPostEditor');
      D.clearElement(wrapper);

      if( !opt.inReplyTo ){
        /* Title... */
        e.titleBar = D.addClass(D.div(),'titlebar');
        e.title = D.attr(
          D.addClass(D.input('text'), 'title'),
          'placeholder',
          'Thread title (required)'
        );
        e.title.setAttribute('maxlength', 125);
        e.titleBar.append(
          D.append(D.span(), "Title:"),
          e.title
        );
        if( this.#draft ){
          e.title.addEventListener('blur', ()=>{
            this.#draft.title = e.title.value;
            this.#storeDraft();
          });
          e.title.value = this.#draft.title || opt.edit?.H || '';
        }else if( opt.edit?.H ){
          e.title.value = opt.edit.H;
        }
        wrapper.append(e.titleBar);
      }

      { /* Mimetype... */
        e.mimetype.wrapper = D.addClass(D.div(), 'mimetype-wrapper');
        const sel = e.mimetype.select = D.addClass(D.select(), 'mimetype-select');
        this.#toDisable.push(sel);
        let i = 0;
        D.option(sel, '', '- Markup format -').disabled = true;
        for(const [k,v] of Object.entries({
          'text/x-markdown': 'Markdown',
          'text/x-fossil-wiki': 'Fossil Wiki',
          'text/plain': 'Plain text'
        })) {
          D.option(sel, k, v);
        }
        sel.value = opt.mimetype
          || this.#draft?.mimetype
          || F.storage.get('forum-mimetype', sel.options[1].value);
        sel.addEventListener('change',ev=>{
          if( this.#draft && this.#draft.mimetype!==ev.target.value ){
            this.#draft.mimetype = ev.target.value;
            this.#storeDraft();
          }
          F.storage.set('forum-mimetype', ev.target.value);
        });
        e.mimetype.wrapper.append(sel);
      }

      e.buttons = D.addClass(D.div(), 'buttons');
      { /* Preview/submit buttons... */
        e.button.preview = D.button("Preview", e=>this.#preview());
        e.button.submit = D.button("Submit");
        if( opt.ondiscard instanceof Function ){
          e.button.discard = D.button('Discard');
        }
        if( 1 ){
          F.confirmer(e.button.submit, {
            confirmText: "Confirm submit...",
            onconfirm: ()=>this.#submit()
          });
          if( e.button.discard ){
            F.confirmer(e.button.discard, {
              confirmText: "Really discard?",
              onconfirm: ()=>this.discard()
            });
          }
        }else{
          e.button.submit.addEventListener('click', ()=>this.#submit());
          if( e.button.discard ){
            e.button.submit.addEventListener('click', ()=>this.discard());
          }
        }
        e.button.submit.setAttribute('disabled', '');
        wrapper.append(e.buttons);

        e.error = D.addClass(D.div(), 'error', 'hidden');
        wrapper.append(e.error);
        e.error.addEventListener('dblclick',()=>this.reportError());
      }

      if( opt.captcha ){
        const eCap = opt.captcha;
        const w = D.div();
        w.style.display = 'flex';
        w.style.flexDirection = 'row';
        w.style.gap = '1em';
        eCap.style.fontFamily = 'monospace';
        eCap.style.whiteSpace = 'pre';
        eCap.style.fontSize = '50%';
        e.captcha = D.attr(D.input('text'), 'size', 8);
        w.append("Enter captcha value:", e.captcha);
        wrapper.append(eCap, w);
        eCap.classList.remove('hidden');
      }

      const idPrefix = 'FormPostEditor'+(++idCounter)/* TabManager requires IDs */;
      { /* Main tabs... */
        e.tabs = D.attr(
          D.addClass(D.div(), 'tab-container'),
          'id', idPrefix+'-tabs'
        );
        this.#tabs = new F.TabManager(e.tabs);
        this.#tabs.addEventListener('before-switch-to', (ev)=>{
          //console.debug("Switching to tab",ev.detail);
          switch( (this.#activeTab = ev.detail) ){
            case e.preview:
              this.#e.button.preview.click();
              break;
            case e.help:
              if( e.help.$needsInit ){
                delete e.help.$needsInit;
                this.#initHelpTab();
              }
              break;
            case e.tabAttach:
              if( !this.#att ) this.#initAttacherTab();
              break;
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
        e.tabEdit.dataset.tabLabel = (opt.edit || !opt.inReplyTo)
          ? 'Edit' : 'Reply';
        this.#tabs.addTab( e.tabEdit );
        this.#tabs.switchToTab( e.tabEdit );
        if( this.#draft ){
          this.editorContent = this.#draft.content || opt.edit?.W || '';
          e.editor.addEventListener(
            'blur', ()=>{
              this.#draft.content = this.editorContent;
              this.#storeDraft();
            }
          );
        }else if( opt.edit?.W ){
          this.editorContent = opt.artifact.W;
        }
        e.preview = D.addClass(D.div(), 'preview');
        e.preview.dataset.tabLabel = 'Preview';
        this.#toDisable.push(e.button.preview);
        this.#tabs.addTab( e.preview );
      }

      if( F.user.enableDebug ){
        e.debug = D.addClass(D.div(), 'debug');
        e.debug.dataset.tabLabel = 'Debug';
        e.debug.setAttribute('id', idPrefix+'-debug');
        for(const [k,v] of Object.entries({
          dryrun: 'Dry run',
          domod: 'Require moderation approval',
          //showqp: 'Show query parameters',
          fpsilent: 'Do not send notification emails'
        })){
          const lbl = D.label(false, v);
          lbl.prepend(D.checkbox(k));
          e.debug.append(lbl);
        }
        this.#tabs.addTab(e.debug);
      }
      e.buttons.append(e.mimetype.wrapper);

      if( opt.edit
          && !opt.inReplyTo
          && F.config.forumStatuses?.length>0 ){
        const sel = e.status = D.select();
        D.option(sel, "", "- Status -").disabled = true;
        for( const status of F.config.forumStatuses ){
          D.option(sel, status.value, status.label);
        }
        e.buttons.append(sel);
        if( opt.status ){
          sel.value = opt.status;
        }else if( this.#draft ){
          if( this.#draft.status ){
            sel.value = this.#draft.status;
          }else{
            this.#draft.status = sel.value = F.config.forumStatuses[0].value;
          }
          sel.addEventListener('change',ev=>{
            const v = sel.value;
            if( this.#draft.status !== v ){
              this.#draft.status = v;
              this.#storeDraft();
            }
          });
        }
      }/*e.status*/

      if( F.user.mayAttachForum ){
        //e.buttons.append( e.button.addAttach = this.#att.takeAddButton() );
        e.tabAttach = D.div();
        e.tabAttach.setAttribute('id', idPrefix+'-attach');
        e.tabAttach.dataset.tabLabel = 'Attachments';
        this.#tabs.addTab(e.tabAttach);
        /* Reminder: we don't currently have a way to disable/enable
           an Attacher's controls during ajax traffic. */
      }
      e.buttons.append(e.button.preview, e.button.submit);
      if( e.button.discard ){
        e.buttons.append(e.button.discard);
        this.#toDisable.push(e.button.discard);
      }

      e.help = D.attr(D.div(), 'id', idPrefix+'-help');
      e.help.$needsInit = true;
      e.help.dataset.tabLabel = 'Help';
      this.#tabs.addTab(e.help);

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
        e.tabEdit.append(eLbl);
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

      if(0){ /* Needs to be optional */
        const elemsToToggle = document.body.querySelectorAll(
          ':scope > header, :scope > nav'
        );
        e.button.toggleHeader =
          D.button('Toggle header', e=>{
            for(const et of elemsToToggle){
              et.classList.toggle('hidden');
            }
          });
        e.buttons.append(e.button.toggleHeader);
      }

    }/*constructor*/

    /*
    ** Removes this object from the DOM. It has no side effects if
    ** it's not in the DOM.
    */
    close(){
      const e = this.#e.widget;
      if( e?.parentNode ){
        if( this.#opt.onclose instanceof Function ){
          try{this.#opt.onclose();}
          catch(e){
            console.error("ForumPostEditor.onclose() threw:",e);
          }
        }
        //console.debug("FPE discarding", this);
        e.classList.add('animate-exit');
        e.addEventListener('animationend', ()=>e.remove(), {once: true});
        dummyPadding.refs.delete(this);
        if( 0===dummyPadding.refs.size ){
          dummyPadding.remove();
        }
      }
    }

    /*
    ** Discards any draft edits then calls close(). If an ondiscard
    ** callback was provided to the constructor then it is called
    ** before the drafts are cleared and any exceptions it throws are
    ** ignored (but may be logged).
    */
    discard(){
      if( this.#opt.ondiscard instanceof Function ){
        try{this.#opt.ondiscard(this);}
        catch(e){
          console.error("ForumPostEditor.ondiscard() threw:",e);
        }
      }
      this.#clearDraft();
      this.close();
    }

    /** This widget's top-most DOM element. */
    get widget(){
      if( !dummyPadding.parentElement ){
        document.body.append(dummyPadding);
      }
      dummyPadding.refs.add(this);
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

    /**
       Reports an error by appending each argument to the error widget
       and unhiding it. If passed no arugments, it clears and hides
       the error widget.
    */
    reportError(...msg){
      const e = this.#e.error;
      D.clearElement(e);
      if( msg.length ){
        console.error('ForumPostEditor:',...msg);
        e.classList.remove('hidden');
        e.append(
          ...msg, D.br(),
          D.button("Clear", ()=>this.reportError())
          /* Looks horrid in the Blitz skin */
        );
      }else{
        e.classList.add('hidden');
      }
    }

    /**
       Adds a list of input[type=hidden] form fields to this object,
       imported from the server-generated HTML. This is used for
       collecting, e.g., the CSRF token and an initial page title.
    */
    addHiddenFields(list){
      this.#extraFields ??= [];
      for( const f of list ){
        if( !f ) continue;
        if( 'title'===f.name && this.#e.title ){
          if( f.value && this.#opt.isNewThread && !this.#e.title.value ){
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

    get title(){
      return this.#e.title?.value || this.#opt.edit?.H;
    }

    #initHelpTab(){
      const eh = this.#e.help;
      const list = D.ul();
      D.append(
        D.li(list),
        D.attr(D.a(F.repoUrl('markup_help'), 'Markup styles'),
               'target', '_new')
      );
      D.append(
        D.li(list),
        "WARNING: draft edits are keyed on the ID of the message they ",
        "are editing or responding to. Attempting to edit or reply to ",
        "the same post from multiple tabs will cause the most-recently-edited ",
        "one to overwrite the draft slot for that post. In browsers which support ",
        "Web Locks, a second attempt to edit or reply to a post will be blocked ",
        "and an error will be shown explaning the problme."
      );
      if( this.#e.status ){
        D.append(
          D.li(list),
          "Tip: changing just the status in the editor will change only that, ",
          "not a whole new (but unedited) copy of the post."
        );
      }
      eh.append(list);
    }

    #initAttacherTab(){
      this.#att = new F.Attacher({
        reverse: true
      });
      if( this.#opt.edit ){
        const eNote = D.append(
          D.div(),
          "Tip: attachments can be added to posts without editing them ",
          "by visiting ",
          D.attr(
            D.a(F.repoUrl('attachadd?target='+this.#opt.edit.uuid), '/attachadd'),
            'target',
            '_new'
          ),
          ".",
        );
        this.#e.tabAttach.append(eNote);
      }
      this.#e.tabAttach.append(this.#att.widget);
    }

    #newFormData(addThisContent){
      const fd = new FormData;
      for(const f of this.#extraFields){
        fd.append(f.name, f.value);
      }
      let v;
      if( this.#opt.inReplyTo ){
        fd.append( 'firt', this.#opt.inReplyTo );
      }else if( (v = (this.#e.title?.value?.trim?.() || this.#opt.edit?.H)) ){
        fd.append('title', v);
      }
      fd.append('mimetype', this.mimetype);
      fd.append('content', addThisContent || this.editorContent.trim());
      if( this.#e.captcha ){
        fd.append('captcha', this.#e.captcha.value);
      }
      return fd;
    }

    async #fetchPreview(content){
      /* TODO: fetch preview */
      const e = this.#e;
      const fd = /*no: this.#newFormData(content); */
            new FormData;
      let ext;
      switch(this.mimetype){
        case 'text/x-markdown':    ext = 'md';   break;
        case 'text/x-fossil-wiki': ext = 'wiki'; break;
        default:                   ext = 'txt';  break;
      }
      fd.append('filename', 'x.'+ext/*for mimetype determination*/);
      fd.append('content', this.editorContent.trim());
      return window
        .fetch(F.repoUrl('ajax/preview-text'),{
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
      /**
         Append the new content then remove the old, to help reduce
         jumping-around of the UI if the preview is cleared then
         repopulated.
      */
      const preview = this.#e.preview;
      const childs = [...preview.childNodes];
      D.parseHtml(preview, rawHtml);
      D.remove(childs);
      //preview.style.removeProperty('height');
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
      const content = this.editorContent.trim();
      //console.debug("content to preview", content);
      if( !content ){
        return;
      }
      if( 0
          && !e.preview.firstElementChild ){
        /* On an initial first preview, inherit the editor's height to
           reduce jumping-around of the UI. */
        if( 0 /* does not work: height of the editor is "auto" */ ){
          const c = window.getComputedStyle(e.editor/*tabEdit*/);
          e.preview.style.height = c.height;
        }else{
          e.preview.style.height = '20em';
        }
      }
      this.#isWaiting = true;
      D.disable(this.#toDisable, e.button.submit);
      this.#fetchPreview(content)
        .then((c)=>{
          this.#setPreviewContent(c);
          D.enable(e.button.submit);
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

    #validate(tgt){
      if( this.#e.captcha && 8!==this.#e.captcha.value.length ){
        this.reportError("Enter the captcha value.");
        return;
      }
      if( this.#e.title ){
        const v = this.#e.title.value.trim();
        if( !v ){
          this.reportError("A non-empty title is required.");
          return;
        }
      }
      return true;
    }

    #submit(){
      if( this.#isWaiting ) return;
      if( !this.#validate() ) return;
      this.#isWaiting = true;
      const e = this.#e;
      D.disable(e.button.submit);
      const fd = this.#newFormData();
      if( this.#e.status ){
        /* Send the status only if it was modified, otherwise we may
           add a superfluous tag. */
        const v = this.#e.status.value;
        if( this.#e.status.dataset.originalValue !== v ){
          fd.append("status", v);
        }
      }
      if( e.debug ){
        e.debug.querySelectorAll('input[type=checkbox]').forEach(cb=>{
          if( cb.checked ){
            fd.append(cb.value, 1);
            //console.debug("Forum post debug option:",cb);
          }
        });
      }
      if( this.#att ){
        this.#att.populateFormData(fd);
      }
      //console.warn("Ready to submit",fd);
      if( 0 ){
        this.#isWaiting = false;
        return;
      }
      const resp = window.fetch(F.repoUrl('forumajax_save'), {
        method: 'POST',
        body: fd
      }).then(r=>r.json())
        .then(j=>{
          j = F.nu(j);
          console.debug("forum post editor response:",j);
          if( j.error ){
            throw new Error(j.error);
          }else if( j.message ){
            /* This is only for use in debugging during
             * development. */
            this.reportError(j.message);
            return;
          }
          if( 1 ){
            this.#clearDraft();
            if( this.#opt.onsubmit instanceof Function ){
              try{this.#opt.onsubmit(this, j);}
              catch(e){
                console.error("ForumPostEditor.onsubmit() threw: ", e);
              }
            }
            /*
              if( this.#opt.edit?.uuid === j.uuid ) then we know the
              content did not change, but it's possible that attachments
              and/or a status tag did. Ergo, we need to unconditionally
              reload to render those changes (if any). The other option
              is to tell the user "nothing changed" and leave them in
              the editor, but that could be a lie because we don't know
              if any attachments or tags were changed.
            */
            else if( 0 ){
              if( this.#opt.edit.uuid === j.uuid
                  && !j.statusModified && 0===j.attachedCount ){
                this.reportError("No changes made.");
              }else{
                window.location = F.repoUrl('forumpost/'+j.uuid);
              }
            }
          }else{
            this.reportError(
              "Saving worked but we're ignoring it and staying here."
            );
          }
        })
        .catch((e)=>this.reportError(e.message))
        .finally(()=>this.#isWaiting = false);
    }

    #storeDraft(){
      if( this.#draft ){
        this.#draft.mtime = Date.now();
        F.storage.setJSON(this.#opt.draftKey, this.#draft);
      }
    }

    /** Clears any persistent draft state. Does not clear the UI
        widgets. */
    #clearDraft(){
      if( this.#draft ){
        F.storage.remove(this.#opt.draftKey);
        this.#draft = F.nu();
      }
    }

    /**
       Looks for editing draft keys matching either a fixed key or a
       regex, and removes each matching one which is older than the
       given number of days. Pass days=0 to purge all entries
       immediately.
    */
    static purgeOldDrafts(key, days=10){
      const age = (3600 * 24 * days) * 1000/*ms*/;
      const now = Date.now();
      const check = (k)=>{
        const o = F.storage.getJSON(k);
        if( o && (!days || (o.mtime+age < now)) ){
          F.storage.remove(k);
        }
      };
      if( key instanceof RegExp ){
        for(const k of F.storage.keys(false).filter(v=>key.test(v))){
          check(k);
        }
      }else{
        check(key);
      }
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

     1. Installs expand/collapse UI elements on "long" posts and collapses
     them.

     2. Any pikchr-generated SVGs get a source-toggle button added to them
     which activates when the mouse is over the image or it is tapped.

     3. Plugs in a new edit/reply widget to forum posts.

     This is a harmless no-op if the current page has neither forum
     post constructs for (1) and (3) nor any pikchr images for (2),
     nor will NOT running this code cause any breakage for clients
     with no JS support: this is all "nice-to-have", not required
     functionality.
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

    /* Page-specific style tweaks for a ForumPostEditor instance. */
    const initFPEWidget = (ePost, fpe)=>{
      const w = fpe.widget;
      w.style.borderTop = '1px dotted';
      //w.style.marginTop = '0.35em';
      /* Adding an "Editing..." <h3> here adds way too much space */
      ePost.append(w);
      w.classList.add('animate-entrance');
      requestAnimationFrame(() => {
        w.scrollIntoView({
          behavior: 'smooth',
          block: 'nearest',
          inline: 'nearest'
        });
      });
    };

    const plugInEditor =
          (new URL(window.location).searchParams).get('nojs')===null;

    const eForumNew = (
      plugInEditor
        && (
          document.body.classList.contains('cpage-forumnew')
            || document.body.classList.contains('cpage-forume1')
        ))
          ? document.body.querySelector('#forumnew-placeholder')
          : null;
    if( plugInEditor && eForumNew ){
      /* /forumnew and /forume1 */
      const fpe = new F.ForumPostEditor({
        draftKey: 'draft-forumnew',
        hiddenFields: eForumNew.querySelectorAll('input[type=hidden]'),
        ondiscard: ()=>{
          window.location = F.repoUrl('forum');
        },
        onsubmit: (fpe, response)=>{
          window.location = F.repoUrl('forumpost/'+response.uuid);
        }
      });
      eForumNew.parentElement.insertBefore(fpe.widget, eForumNew);
      eForumNew.remove();
      fossil.page.fpe = fpe /* for testing via the console */;
    }/*eForumNew*/
    else if( plugInEditor
             && (document.body.classList.contains('cpage-forumpost')
                 || document.body.classList.contains('cpage-forumthread')) ){
      /* /forumpost and /forumthread. Take over the Edit/Reply buttons
         to use a ForumPostEditor. */

      const fetchPost = async (fpid)=>{
        return window.fetch(F.repoUrl('ajax/artifact.json?uuid='+fpid))
          .then(r=>r.json())
          .then(j=>{
            j = F.nu(j);
            if( j.error ) throw new Error(j.error);
            return j;
          });
      };

      const makeDraftKey = (prefix,uuid)=>{
        return prefix+'-'+uuid.substr(0,12);
      };

      /**
         Perform some init common to both Reply and Edit.  ePost = the
         forum post DOM element. eButton = the Reply or Edit
         button. eToDisable = an array of DOM elements which must be
         disabled when the editor is active and re-enabled when it is
         closed.
      */
      const setupEditReplyElement = (ePost, eButton, eToDisable)=>{
        /* Forum posts are indented in the main forum view to
           represent their place in the hierarchy. In order to gain
           some screen space, we shift the post to the left margin and
           arrange to shift it back when the editor is closed. We also
           record the original button label so that it can be
           restored on close. */
        ePost.dataset.originalMarginLeft = ePost.style.marginLeft;
        ePost.style.marginLeft = 'initial';
        eButton.dataset.originalLabel = eButton.innerText;
        D.disable(eToDisable);
      };

      /** Undoes the damage done by setupEditReplyElement(). */
      const restoreEditReplyElement = (ePost, eButton, eToDisable)=>{
        if( ePost.dataset.originalMarginLeft ){
          ePost.style.marginLeft = ePost.dataset.originalMarginLeft;
          delete ePost.dataset.originalMarginLeft;
        }
        if( eButton.dataset.originalLabel ){
          eButton.innerText = eButton.dataset.originalLabel;
          delete eButton.dataset.originalLabel;
        }
        D.enable(eToDisable);
      };

      /**
         Reports an error regarding the forum post element
         ePost, appending each entry in msg to a wrapper
         element with the class
      */
      const reportFPEError = (ePost,...msg)=>{
        const e = D.addClass(D.p(), 'error');
        e.append(
          ...msg,
          D.br(),
          D.button("Clear error", ()=>e.remove())
        );
        ePost.append(e);
      };

      /**
         Plug in an editor widget representing a reply to a post.
         form = a (.forum-post-single-controls > form) element.  The
         final 3 arguments are as documented for
         setupEditReplyElement().
      */
      const replyClicked = async (form, ePost, eBtnReply, eToDisable)=>{
        const fpid = ePost.dataset.fpid;
        const fEditHead = ePost.dataset.fedithead;
        const draftKey = makeDraftKey(
          'draft-reply', fEditHead
          /* The problem with firt as a key is that firt is not
             necessarily the root edit of that post, which is what we
             really want as a draft key so that the draft does not
             disappear if firt is later edited (giving us a new firt
             value here). */
            || fpid
        );
        let releaseLock;
        if( window.navigator.locks ){
          releaseLock = await new Promise((resolve)=>{
            window.navigator.locks.request(
              'fossil-'+draftKey,
              {ifAvailable: true},
              async (lock) => {
                if( !lock ){
                  /*lock contention*/
                  resolve(null);
                  return;
                }
                let release;
                const lockReleased = new Promise(res=>release=res);
                resolve(release);
                await lockReleased/*hold the lock open*/;
              });
          });
          if( !releaseLock ){
            reportFPEError(
              ePost,
              "This post is actively being replied to ",
              "in another tab. To avoid losing edits, ",
              "it cannot be opened here until the locking ",
              "tab is closed."
            );
            return;
          }
        }

        setupEditReplyElement(ePost, eBtnReply, eToDisable);
        eBtnReply.innerText = "Replying...";
        const ondone = (fpe, response)=>{
          /* onsubmit() and ondiscard() callback */
          restoreEditReplyElement(ePost, eBtnReply, eToDisable);
          //console.debug("ondiscard/onsubmit", fpe, artifact);
          if( response/*onsubmit()*/ ){
            window.location = F.repoUrl('forumpost/'+response.uuid);
            fpe.close();
          }else{/*ondiscard()*/
          }
        };
        const fpe = new F.ForumPostEditor(F.nu({
          hiddenFields: form.querySelectorAll(
            'input[type=hidden][name=csrf]'
            /* Do not inherit the fpid field, else this will become
               an edit to that post rather than a response. */
          ),
          ondiscard: ondone,
          onsubmit: ondone,
          onclose: ()=>{
            if( releaseLock ){
              releaseLock();
              releaseLock = null;
            }
          },
          inReplyTo: fpid,
          draftKey
        }));
        initFPEWidget(ePost, fpe);
      }/*replyClicked()*/;

      /**
         Plug in an editor widget representing an edit to a post.
         form = a (.forum-post-single-controls > form) element.  The
         final 3 arguments are as documented for
         setupEditReplyElement().
      */
      const editClicked = async (form, ePost, eBtnEdit, eToDisable)=>{
        const fpid = ePost.dataset.fpid;
        const firt = ePost.dataset.firt;
        const fEditHead = ePost.dataset.fedithead;
        const draftKey = makeDraftKey('draft-forumedit', fEditHead || fpid);
        let releaseLock;
        if( navigator.locks ){
          releaseLock = await new Promise((resolve) => {
            navigator.locks.request(
              'fossil-'+draftKey,
              {ifAvailable: true},
              async (lock)=>{
                if( !lock ){
                  resolve(null);
                  return;
                }
                let release;
                const lockReleased = new Promise(res=>release=res);
                resolve(release);
                await lockReleased;
              });
          });

          if( !releaseLock ){
            reportFPEError(
              ePost,
              "This post is actively being edited ",
              "in another tab. To avoid losing edits, ",
              "it cannot be opened here until the locking ",
              "tab is closed."
            );
            return;
          }
        }
        setupEditReplyElement(ePost, eBtnEdit, eToDisable);
        eBtnEdit.innerText = "Editing...";
        fetchPost(fpid)
          .then(artifact=>{
            const ondone = (fpe, response)=>{
              /* onsubmit() and ondiscard() callback */
              //console.debug("ondiscard/onsubmit", fpe, eToDisable);
              if( response/*onsubmit()*/ ){
                if( fpid === response.uuid
                    && !response.statusModified
                    && 0===response.attachedCount ){
                  fpe.reportError("No changes made.");
                }else{
                  restoreEditReplyElement(ePost, eBtnEdit, eToDisable);
                  window.location = F.repoUrl('forumpost/'+response.uuid);
                }
              }else{
                /*ondiscard()*/
                restoreEditReplyElement(ePost, eBtnEdit, eToDisable);
              }
            };
            const eStatusSelect = ePost.querySelector(
              ':scope > fieldset.forum-status-selection select[name=status]'
            );

            const fpe = new F.ForumPostEditor(F.nu({
              hiddenFields: form.querySelectorAll('input[type=hidden]'),
              ondiscard: ondone,
              onsubmit: ondone,
              onclose: ()=>{
                if(releaseLock){
                  releaseLock();
                  releaseLock = null;
                }
              },
              draftKey,
              edit: artifact,
              status: eStatusSelect?.value,
              inReplyTo: firt
            }));
            initFPEWidget(ePost, fpe);
          })
          .catch(err=>{
            if( releaseLock ){
              releaseLock();
              releaseLock = null;
            }
            restoreEditReplyElement(ePost, eBtnEdit, eToDisable);
            console.error("Error fetching post:", err);
            reportFPEError(ePost, "Error fetching post: ", err.message);
          });
      }/*editClicked()*/;

      document.body.querySelectorAll(
        '.forumpost-single-controls > form'
      ).forEach(form=>{
        /* For each forum post... */
        const eToDisable = [
          /* List of non-editor DOM elements which need to be disabled
             while the editor is active and re-enabled when it
             closes. */
        ];
        const eThePost = form.parentElement.parentElement/*main post DOM element*/;
        if( !eThePost?.dataset?.fpid ){
          /* The server injects these. */
          console.warn("Unexpected missing fpid", eThePost);
          return;
        }

        const checkButtonForDraft = (draftKeyPrefix, eBtn)=>{
          /* If a draft is found associated with eThePost, mark eBtn
             as a draft and set up storage event listeners to update
             the button as new drafts come and go. */
          const fpid = eThePost.dataset.fpid;
          const fEditHead = eThePost.dataset.fedithead;
          const draftKey = makeDraftKey(draftKeyPrefix, fEditHead || fpid);
          if( F.storage.contains(draftKey) ){
            eBtn.classList.add('draft');
          }
          F.storage.addEventListener('set', ({detail})=>{
            if( draftKey === detail.key ){
              eBtn.classList.add('draft');
            }
          });
          F.storage.addEventListener('remove', ({detail})=>{
            if( draftKey === detail.key ){
              eBtn.classList.remove('draft');
            }
          });
        };
        /* Replace the Reply and Edit buttons with ones which will activate
           a ForumPostEditor. */
        const btnReply = form.querySelector('input[type=submit][name=reply]');
        if( btnReply ){
          const b = D.button("Reply", ()=>replyClicked(form, eThePost, b, eToDisable));
          b.type = 'button'/*keep container form from submitting*/;
          eToDisable.push(b);
          checkButtonForDraft('draft-reply',b);
          btnReply.parentElement.insertBefore(b, btnReply);
          btnReply.remove();
        }
        const btnEdit = form.querySelector('input[type=submit][name=edit]');
        if( btnEdit ){
          const b = D.button("Edit", ()=>editClicked(form, eThePost, b, eToDisable));
          b.type = 'button'/*keep container form from submitting*/;
          eToDisable.push(b);
          checkButtonForDraft('draft-forumedit',b);
          btnEdit.parentElement.insertBefore(b, btnEdit);
          btnEdit.remove();
        }
        /* Problem: we really need to disable the status-selection
           button because it would redirect mid-edit. We wouldn't lose
           the edits but would lose any pending attachments. We work
           around this by disabling this selection and adding a new
           status selection widget in the editor, inheriting this
           one's value. */
        const eStatusChange = eThePost.querySelector(
          ':scope > fieldset.forum-status-selection'
        );
        if( eStatusChange ) eToDisable.push(eStatusChange);
      })/*for-each form*/;

    }/* /forumpost and /forumthread */

    document.body.querySelectorAll('.remove-on-load').forEach(e=>e.remove());
    document.body.querySelectorAll('.initially-hidden').forEach(e=>{
      /* This is a workaround for a span.help-buttonlet which we need
         to start hidden so that it does not show up for no-JS
         clients. */
      e.classList.remove('initially-hidden');
    });

    if( plugInEditor ){
      document.body.querySelectorAll('.remove-if-replaced').forEach(e=>e.remove());
      /* Purge old drafts only every now and then. */
      const now = Date.now();
      const lastPurge = +F.storage.get('forum-drafts-last-purge', 0);
      if( now - lastPurge > (24 * 60 * 60 * 1000 /*1 day ms*/) ){
        F.storage.set('forum-drafts-last-purge', now);
        setTimeout(()=>{
          /* Don't block the UI while we're doing I/O */
          F.ForumPostEditor.purgeOldDrafts(/^draft-(reply|forumedit)-.*/);
        }, 50);
      }
    }
  })/*F.onPageLoad callback*/;
})(window.fossil);
