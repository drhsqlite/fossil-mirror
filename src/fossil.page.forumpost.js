/**
   Code for the forum family of pages. Requires fossil.X where X is
   (copybutton, pikchr, confirmer, attach, tabs).
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

    constructor(opt){
      opt = this.#opt = F.nu({
        // todo: defaults once we determine the options
        // replyTo: hash
        // edit: hash
      }, opt);
      opt.isNew = !opt.edit && !opt.replyTo;
      const e = this.#e = F.nu({
        mimetype: F.nu(),
        button: F.nu()
      });
      const wrapper = e.widget = D.addClass(D.div(), 'ForumPostEditor');
      D.clearElement(wrapper);
      if( !opt.inReplyTo ){
        e.titleBar = D.addClass(D.div(),'titlebar');
        e.title = D.addClass(D.input('text'), 'title');
        e.titleBar.append(
          D.append(D.span(), "Title:"),
          e.title
        );
        wrapper.append(e.titleBar);
      }
      e.mimetype.wrapper = D.addClass(D.div(), 'mimetype-wrapper');
      e.mimetype.select = D.addClass(D.select(), 'mimetype-select');
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
      e.button.submit = D.button("Submit", e=>this.#submit());
      e.button.submit.setAttribute('disabled', '');
      e.buttons = D.addClass(D.div(), 'buttons');
      wrapper.append(e.buttons);

      e.err = D.addClass(D.div(), 'error', 'hidden');
      wrapper.append(e.err);
      e.err.addEventListener('dblclick',()=>this.reportError());

      const idPrefix = 'FormPostEditor'+(++idCounter)/* TabManager requires IDs */;
      { /* Tabs... */
        e.tabs = D.attr(
          D.addClass(D.div(), 'tab-container'),
          'id', idPrefix+'-tabs'
        );
        this.#tabs = new F.TabManager(e.tabs);
        wrapper.append( e.tabs );

        e.tabEdit = D.div();
        e.tabEdit.classList.add('editor-wrapper');
        e.editor = D.attr(
          D.addClass(D.textarea(), 'editor'),
          'placeholder',
          'Your content...'
        );
        e.tabEdit.append(e.editor);
        e.tabEdit.dataset.tabLabel = 'Edit';
        this.#tabs.addTab( e.tabEdit );

        e.preview = D.addClass(D.div(), 'preview');
        e.preview.dataset.tabLabel = 'Preview';
        this.#tabs.addTab( e.preview );
        this.#tabs.addEventListener('before-switch-to', (ev)=>{
          this.#activeTab = ev.detail;
          if( e.preview === this.#activeTab ){
            this.#e.button.preview.click();
          }
        });
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
          addButtonLabel: 'Attach'
        });
        e.buttons.append( e.button.addAttach = this.#att.takeAddButton() );
        this.#toDisable.push( e.button.addAttach );
      }
      e.buttons.append(e.button.preview, e.button.submit);
      this.#toDisable.push(e.button.preview);
      if( this.#att ){
        wrapper.append(this.#att.widget);
      }
    }/*constructor*/

    get widget(){
      return this.#e.widget;
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

    async #fetchPreview(){
      /* TODO: fetch preview */
      this.#isWaiting = false;
      D.enable(this.#toDisable);
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
      D.disable(this.#toDisable, e.button.submit);
      e.preview.textContent = "Fetching preview...";
      this.#fetchPreview()
        .then(()=>{
          e.preview.textContent = "TODO: actually fetch the preview "+Date.now();
          D.enable(this.#toDisable, e.button.submit);
        })
        .catch(e=>{
          this.reportError(e.message);
          D.enable(this.#toDisable);
        });
    }

    #submit(){
      if( this.#isWaiting ) return;
      this.#isWaiting = true;
      const e = this.#e;
      D.disable(e.button.submit);
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

  })/*F.onPageLoad callback*/;
})(window.fossil);
