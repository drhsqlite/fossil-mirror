"use strict";
/**
   Utility for interactive file attachment. Supports attachment
   selection from a file dialog, from the clipboard, or drag/drop.

   Requires that window.fossil has already been set up.
   Depends on fossil.dom.
*/
(function(namespace){
  "use strict";
  const F = namespace, D = fossil.dom;

  let idCounter = 0;

  /**
     Implements a multi-file selector widget. Intended to be plugged
     in to places in Fossil's UI where attachments can be assigned to
     an artifact.
  */
  class Attacher {
    #opt;
    #rows = [];
    #e = Object.create(null);
    #events = new EventTarget();

    /**
       Options:

       opt.container: DOM element to wrap this object in.

       opt.addButtonLabel: optional label for the "add attachment"
       button, defaulting to something generic.

       opt.limit: optional max number of attachments to allow.  This
       defaults to "some sensible value".

       opt.startWith[=0]: if >0 then that many file selection widgets
       are automatically activated, as if the user had tapped the Add
       button that many times.

       opt.description[=true]: if true then show the file description
       field, otherwise elide it.

       opt.controls = [array of DOM elements]. Optional DOM elements
       to inject into the UI element which wraps the "Add" button.
       See this.controlsElement.

       opt.listener = function or object: {add: func, remove: func,
       populate: func}: if these are functions they are registered as
       listeners for 'entry-added', 'entry-removed', and/or
       'entry-populated' events, described below. opt.listener.all, if
       set, is used as a fallback for any of 'add', 'remove', or
       'populate' which are not set. If opt.listener is a function
       then it behaves as if listener={all: thatFunction}.

       Events:

       This class fires CustomEvents for certain changes:

       'entry-added' and 'entry-removed' trigger when an attachment
       entry row is added/removed. Its event.detail is:

       {attacher: this, row: object, type: 'same as event type'}.

       'entry-populated' is triggered when a visible entry gets
       content attached to it, with the same detail structure as
       described above.

       The public structure of the row object passed to each is
       currently TBD.
    */
    constructor(opt){
      this.#opt = opt = F.nu({
        addButtonLabel: false,
        startWith: 0,
        limit: 0,
        dryRun: false,
        description: true
      }, opt);
      this.#e.body = D.addClass(D.div(), 'attach-widget');
      const eBtnAdd = this.#e.btnAdd = D.addClass(
        D.button(this.#opt.addButtonLabel || 'Add attachment',
                 ()=>this.#addRow()),
        'attach-add-button'
      );
      eBtnAdd.type = 'button';
      this.#e.err = D.addClass(D.div(), 'error', 'hidden');
      this.#e.body.append(this.#e.err);
      this.#e.err.addEventListener('dblclick',()=>this.reportError());

      const eControls = this.#e.controls =
            D.addClass(D.div(), 'attach-controls');
      eControls.append(eBtnAdd);
      opt.container.appendChild(this.#e.body);
      this.#e.body.appendChild(eControls);
      if( opt.listener ){
        const doCb = (eventType, key)=>{
          const f = (opt.listener instanceof Function)
                ? opt.listener
                : (opt.listener[key] || opt.listener.all);
          if( f instanceof Function ){
            this.addEventListener(eventType, f);
          }
        };
        doCb('entry-added', 'add');
        doCb('entry-removed', 'remove');
        doCb('entry-populated', 'populate');
      }
      if( 0 ){
        /* Add dry-run toggle for testing. */
        const eLbl = D.label(false, "Dry-run?");
        const eCb = D.checkbox(true);
        eLbl.append(eCb);
        eControls.append(eLbl);
        eCb.checked = opt.dryRun = true;
        eCb.addEventListener('change',()=>opt.dryRun=eCb.checked);
      }
      if( Array.isArray(opt.controls) ){
        eControls.append(...opt.controls);
      }
      if( opt.startWith > 0 ){
        for(let i = 0; i < opt.startWith; ++i ){
          this.#addRow();
        }
      }else{
        this.#updateControls();
      }
    }

    addEventListener(...args){
      return this.#events.addEventListener(...args);
    }

    removeEventListener(...args){
      return this.#events.removeEventListener(...args);
    }

    /** Returns true if any visible input widgets have content
        selected. */
    get isPopulated(){
      for(let r of this.#rows){
        if( r.file ) return true;
      }
      return false;
    }

    get isDryRun(){
      return !!this.#opt.dryRun;
    }
    /**
       Returns the DOM element (div.attach-controls) which wraps the
       "Add" button.  Clients may add buttons to it.
    */
    get controlsElement(){
      return this.#e.controls;
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

    #removeRow(rowObj){
      rowObj.e.row.remove();
      this.#rows = this.#rows.filter(v=>v!==rowObj);
      this.#updateControls();
      this.#events.dispatchEvent(
        new CustomEvent('entry-removed',{
          detail: F.nu({
            type: 'entry-removed',
            row: rowObj,
            attacher: this
          })
        })
      );
      if( false /* arguable */
          && 0===this.#rows.length
          && this.#opt.startWith>0 ){
        /* Intended primarily for /addattach. */
        this.#addRow();
      }
    }

    clear(){
      for(const r of [...this.#rows/*clone because #rows may change*/]){
        this.#removeRow(r);
      }
      this.reportError();
    }

    /**
       Hides or shows the Add button, as appropriate.
    */
    #updateControls(){
      const b = this.#e.btnAdd;
      if( this.#opt.limit>0 && this.#rows.length >= this.#opt.limit ){
        b.classList.add('hidden');
        D.disable(b);
        //F.toast.warning("Attachment form limit reached.");
      }else{
        b.classList.remove('hidden');
        D.enable(b);
        this.#e.body.append(this.#e.controls/*move to the end*/);
      }
    }

    /**
       Sets rowObj.e.err up with an error message, or clears it if
       passed only 1 argument.
    */
    #rowError(rowObj,...msg){
      let e = rowObj.e.err;
      if( e ){
        D.clearElement(e);
      }else{
        if( !msg.length ) return;
        e = rowObj.e.err = D.addClass(D.span(), 'error');
        rowObj.e.info.append(e);
      }
      if( msg.length ){
        e.append(...msg);
        e.classList.remove('hidden');
      }else{
        e.classList.add('hidden');
      }
    }

    #addRow(){
      const id = ++idCounter;
      const rowObj = F.nu({
        id, file: null, mimeType: ''
      });
      const eRow = D.addClass(D.div(), 'attach-row');
      eRow.dataset.id = id;
      const eDropzone = D.addClass(D.div(), 'attach-dropzone');
      const eFile = D.addClass(
        D.input('file'), 'attach-file-input', 'hidden'
      );
      const eInfo = D.addClass(D.span(), 'attach-row-info');
      const eFilename = D.append(
        D.addClass(D.span(), 'attach-filename'),
        "Select/drop file or click the outer border and tap your "+
        "platform's conventional <paste> keyboard shortcut."
      );
      const eSize = D.addClass(D.span(), 'attach-size');
      eInfo.append(eFilename, eSize);
      const eDesc = this.#opt.description
            ? D.addClass(
              D.attr(D.textarea(), 'placeholder',
                     'Optional description...'),
              'attach-desc'
            )
            : undefined;
      const eRemove = D.addClass(
        D.button('X', (ev)=>{
          ev.stopPropagation();
          this.#removeRow(rowObj);
        }),
        'attach-row-remove'
      );
      eRemove.setAttribute('title', 'Remove this attachment.');
      eRemove.type = 'button';

      D.append(eDropzone, eInfo, eFile, eRemove);
      eDropzone.addEventListener('click', ()=>eFile.click());
      eFile.addEventListener('change', (ev)=>{
        if( ev.target.files.length ){
          this.#injestBlob(rowObj, ev.target.files[0]);
        }
      });

      eDropzone.addEventListener('dragover', (ev)=>{
        ev.preventDefault();
        eDropzone.classList.add('dragover');
      });
      eDropzone.addEventListener('dragleave', (ev)=>{
        eDropzone.classList.remove('dragover');
      });
      eDropzone.addEventListener('drop', (ev)=>{
        ev.preventDefault();
        eDropzone.classList.remove('dragover');
        if( ev.dataTransfer.files.length ){
          this.#injestBlob(rowObj, ev.dataTransfer.files[0]);
        }
      });
      const pasteImage = (event, item)=>{
        if( item.type.indexOf('image') === 0 ) {
          event.preventDefault();
          const blob = item.getAsFile();
          this.#injestBlob(rowObj, blob);
          return true;
        }
        return false;
      };
      eDesc?.addEventListener?.('paste', (e) => {
        e.stopPropagation();
        const items = (e.clipboardData || e.originalEvent.clipboardData)?.items;
        if( !items ) return;
        for( let i = 0; i < items.length; ++i ){
          const item = items[i];
          if( pasteImage(e, item) ){
            break;
          }
        }
      });
      eRow.addEventListener('paste', (e) => {
        const items = (e.clipboardData || e.originalEvent.clipboardData)?.items;
        if( !items ) return;
        for( let i = 0; i < items.length; ++i ){
          const item = items[i];
          if( pasteImage(e, item) ){
            break;
          }else if( item.type === 'text/plain' ){
            e.preventDefault();
            item.getAsString((text) => {
              rowObj.overrideName = `pasted-text-${Date.now()}.txt`;
              const blob = new File([text], rowObj.overrideName,
                                    {type: 'text/plain'});
              this.#injestBlob(rowObj, blob);
            });
            break;
          }else if(0){
            e.preventDefault();
            const blob = undefined /* ??? */;
            this.#injestBlob(rowObj, blob);
            break;
          }
        }
      });
      eRow.append(eDropzone);
      if( eDesc ) eRow.append(eDesc);
      rowObj.e = F.nu({
        dropzone: eDropzone,
        info: eInfo,
        filename: eFilename,
        size: eSize,
        desc: eDesc,
        row: eRow,
        remove: eRemove
      });
      this.#e.body.append(eRow);
      this.#rows.push( rowObj );
      this.#updateControls();
      this.#events.dispatchEvent(
        new CustomEvent('entry-added',{
          detail: F.nu({
            type: 'entry-added',
            row: rowObj,
            attacher: this
          })
        })
      );
      if( 0 ){
        /* To allow immediate ctrl-v, we need a trick...
           But don't do this because it will interfere with, e.g.,
           the forum editor. */
        D.attr(eRow, 'tabindex', '-1');
        eRow.focus();
      }
    }

    #rowMatchingName(name){
      for(let r of this.#rows){
        if( r.file?.name===name ) return r;
      }
    }

    #injestBlob(rowObj, file){
      if( !file ) return;
      if( file.name === 'image.png' ){
        /* Workaround to attempt to avoid name collisions when pasting
           multiple images. We cannot, at this level, unambiguously
           distinguish a ctrl-v of bitmap data vs a ctrl-v of an image
           file copied via a desktop file manager. */
        rowObj.overrideName = `pasted-image-${Date.now()}.png`;
      }
      const old = this.#rowMatchingName(file.name);
      if( old && rowObj !== old ){
        /*
          Fossil attachments treat the name as a unique-per-target
          key, with the newest one being the primary.  If a name is
          given twice, remove the new entry and reuse the older
          one. There are conceivable, but also unlikely, cases where
          this will have unintended side-effects, e.g. attaching both
          /foo/bar and /baz/bar, but that seems like a lesser evil
          than attaching the same file N times, leading to N
          attachment artifacts.
        */
        /* recycle `old` instead to avoid UI flicker. */
        this.#rowError(old);
        this.#removeRow(rowObj);
        rowObj.e = old.e;
        //if( rowObj.e.eDesc ) rowObj.e.eDesc.value = '';
      }
      console.warn("rowObj, old",rowObj, old);
      if( rowObj.overrideName ){
        console.warn("Renaming file to",rowObj.overrideName);
        file = new File([file], rowObj.overrideName, {type: file.type});
        rowObj.overrideName = undefined;
      }

      let szLbl;
      if( file.size < 500000 ){
        szLbl = file.size + ' bytes';
      }else if( file.size < 1000000 ){
        szLbl = (file.size / 1024).toFixed(2)+' KB';
      }else{
        szLbl = (file.size / (1024 * 1024)).toFixed(2)+' MB';
      }
      this.#rowError(rowObj);
      rowObj.file = file;
      rowObj.mimeType = file.type || 'application/octet-stream';
      D.clearElement(rowObj.e.filename).append(file.name || 'Pasted Content');
      D.clearElement(rowObj.e.size).append(szLbl, ' ', rowObj.mimeType || '');
      rowObj.e.dropzone.classList.add('populated');
      if( rowObj.e.desc ){
        rowObj.e.desc.classList.remove('hidden');
      }
      if( rowObj.e.thumbnail ){
        rowObj.e.thumbnail.remove();
        rowObj.e.thumbnail = undefined;
      }
      if( file.type?.startsWith?.('image/') || file.type==='BITMAP' ){
        /* Add a thumbnail */
        const img = rowObj.e.thumbnail = D.img();
        rowObj.e.dropzone.insertBefore(img, rowObj.e.remove);
        img.classList.add('thumbnail');
        const reader = new FileReader();
        reader.onload = (e)=>img.setAttribute('src', e.target.result);
        reader.readAsDataURL(file);
      }
      if( file.size>F.config.attachmentSizeLimit ){
        /* Problem: tapping this link propagates its click event through
           to eDropzone. Thus... */
        const eLink = D.a(F.repoUrl('help/attachment-size-limit'),'limit');
        eLink.addEventListener('click', ev=>ev.stopPropagation());
        this.#rowError(rowObj, "Too large: ", eLink,
                       " is ",F.config.attachmentSizeLimit," bytes");
        rowObj.ok = false;
      }else if( !file.size ){
        this.#rowError(rowObj, "Cannot attach zero-byte files.");
        rowObj.ok = false;
      }else{
        rowObj.ok = true;
      }
      this.#events.dispatchEvent(
        new CustomEvent('entry-populated',{
          detail: F.nu({
            type: 'entry-populated',
            row: rowObj,
            attacher: this
          })
        })
      );
    }

    /**
       Returns an array of objects describing the currently-selected
       attachments.
    */
    collectState(){
      const rv = [];
      for(let r of this.#rows){
        if( !r.e.dropzone?.classList?.contains?.('populated') ){
          continue;
        }
        rv.push(F.nu({
          name: r.name || r.file.name || `pasted-content-${r.id}.${r.mimeType.split('/')[1] || 'txt'}`,
          content: r.file,
          description: r.e.desc?.value || '',
          mimeType: r.mimeType
        }));
      }
      return rv;
    }

    /**
       Populates the given FormData object with entries named
       ${namePrefix}${N}, each representing a selected file and N
       being a 1-based incremental counter. For entries which have a
       description, it also sets ${namePrefix}${N}_desc.
    */
    populateFormData(fd, namePrefix='file'){
      const st = this.collectState();
      let i = 0;
      for( ; i < st.length; ++i){
        const s = st[i];
        const suffix = i+1;
        fd.append(`${namePrefix}${suffix}`, s.content, s.name);
        const d = s.description?.trim?.();
        if( d ){
          fd.append(`${namePrefix}${suffix}_desc`, d);
        }
      }
      return i;
    }
  }/*Attacher*/;
  F.Attacher = Attacher;

  const eAttachWrapper = document.querySelector('#attachadd-form-wrapper');
  if( eAttachWrapper ){
    /* This page is /attachadd v2 or a workalike. eAttachWrapper holds
       input[type=hidden] fields for use in attaching files and is
       where we inject a file attachment widget. */
    eAttachWrapper.classList.remove('hidden');
    const urlArgs = new URLSearchParams(window.location.search);
    let zTarget = urlArgs.get('target');
    let zTo = urlArgs.get('to') || urlArgs.get('from');
    const eBtnSubmit = D.button("Submit");
    eBtnSubmit.type = 'button';
    const updateBtnSubmit = (attacher)=>{
      if( attacher.isPopulated ){
        eBtnSubmit.removeAttribute('disabled');
      }else{
        eBtnSubmit.setAttribute('disabled', '');
      }
    };
    const cbAttacherChange = (ev)=>{
      const a = ev.detail.attacher;
      updateBtnSubmit(a);
    };
    const att = new Attacher({
      container: eAttachWrapper,
      startWith: 1,
      listener: cbAttacherChange,
      controls: [eBtnSubmit],
      description: true
    });
    eBtnSubmit.addEventListener('click', async (ev)=>{
      att.reportError();
      const li = att.collectState();
      if( !li.length ) return;
      if( eBtnSubmit.dataset.submitted ) return;
      eBtnSubmit.dataset.submitted = 1;
      D.disable(eBtnSubmit);
      const fd = new FormData();
      let i = 0;
      for(const row of li){
        ++i;
        fd.append('file'+i, row.content);
        if( row.description ) fd.append('file'+i+'_desc', row.description);
      }
      for( const eIn of eAttachWrapper.querySelectorAll(
        'input[type="hidden"]'
      ) ){
        /* Copy over hidden input fields emitted by the server. */
        if( eIn.name==='target' ){
          zTarget = eIn.value;
        }else if( eIn.name==='to' || (eIn.name==='from' && !zTo) ){
          zTo = eIn.value;
        }
        fd.append(eIn.name, eIn.value)
      }
      if( att.isDryRun ){
        fd.append('dryrun', '1');
      }
      let err;
      const resp = await window.fetch(F.repoUrl('attachadd_ajax_post'), {
        method: 'POST',
        body: fd
      }).catch((e)=>{
        err = e;
      });
      D.enable(eBtnSubmit);
      delete eBtnSubmit.dataset.submitted;
      const jr = err ? undefined : await resp.json().catch(()=>{});
      if( err || jr?.error || !resp.ok ){
        const msg = err ? err.message : (jr?.error || resp.statusText);
        att.reportError("Attaching failed: ", msg);
      }else{
        att.clear();
        let to = zTo || jr?.redirect;
        if( to ){
          if( '/'!==to[0] ){
            to = F.repoUrl(to);
          }
          window.location = to;
        }else if( target ){
          window.location = '?target='+zTarget+'&'+Date.now();
        }
      }
    })/*submit handler*/;
    updateBtnSubmit(att);
    F.page.attacher = att /* only for testing via dev console */;
  }/* /attachadd */

})(window.fossil);
