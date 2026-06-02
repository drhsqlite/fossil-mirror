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
    */
    constructor(opt){
      this.#opt = opt = Object.assign(Object.create(null),{
        addButtonLabel: false,
        startWith: 0,
        limit: 7
      }, opt);
      const eBtnAdd = this.#e.btnAdd = D.addClass(
        D.button(this.#opt.addButtonLabel || 'Add attachment',
                 ()=>this.#addRow()),
        'attach-add-button'
      );
      eBtnAdd.type = 'button';
      this.#e.list = D.addClass(D.div(), 'attach-container');
      opt.container.appendChild(this.#e.list);
      this.#e.list.appendChild(eBtnAdd);
      if( opt.startWith > 0 ){
        for(let i = 0; i < opt.startWith; ++i ){
          this.#addRow();
        }
      }
    }

    #removeRow(rowObj){
      rowObj.eRow.remove();
      this.#rows = this.#rows.filter(v=>v!==rowObj);
      this.#updateBtnAdd();
      if( 0===this.#rows.length
          && 1===this.#opt.limit
          && 1===this.#opt.startWith ){
        /* Intended primarily for /addattach. */
        this.#addRow();
      }
    }

    /**
       Hide or show the Add button, as appropriate.
    */
    #updateBtnAdd(){
      const b = this.#e.btnAdd;
      if( this.#opt.limit>0 && this.#rows.length >= this.#opt.limit ){
        b.classList.add('hidden');
        //b.setAttribute('disabled','');
        //F.toast.warning("Attachment form limit reached.");
      }else{
        b.classList.remove('hidden');
        //b.removeAttribute('disabled');
        this.#e.list.append(b/*move to the end*/);
      }
    }

    #addRow(){
      const id = ++idCounter;
      const rowObj = {
        id, file: null, mimeType: ''
      };
      const eRow = D.addClass(D.div(), 'attach-row');
      eRow.dataset.id = id;
      const eDropzone = D.addClass(D.div(), 'attach-dropzone');
      const eFile = D.addClass(
        D.input('file'), 'attach-file-input', 'hidden'
      );
      const eInfo = D.append(
        D.addClass(D.span(), 'attach-row-info'),
        "Select/drop file or click the outer border and tap your "+
        "platform's conventional Paste keyboard shortcut."
      );

      const eDesc = D.addClass(
        D.attr(D.textarea(), 'placeholder',
               'Optional description...'),
        'hidden', 'attach-desc'
      );
      const eRemove = D.addClass(
        D.button('Remove', (ev)=>{
          ev.stopPropagation();
          this.#removeRow(rowObj);
        }),
        'attach-row-remove'
      );
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
      eRow.addEventListener('paste', (e) => {
        const items = (e.clipboardData || e.originalEvent.clipboardData)?.items;
        if( !items ) return;
        for( let i = 0; i < items.length; ++i ){
          const item = items[i];
          if( item.type.indexOf('image') === 0 ) {
            e.preventDefault();
            const blob = item.getAsFile();
            this.#injestBlob(rowObj, blob);
            break;
          }else if( item.type === 'text/plain' ){
            e.preventDefault();
            item.getAsString((text) => {
              const blob = new File([text], `pasted-text-${id}.txt`,
                                    {type: 'text/plain'});
              this.#injestBlob(rowObj, blob);
            });
            break;
          }
        }
      });
      D.append(eRow, eDropzone, eDesc);
      rowObj.eDropzone = eDropzone;
      rowObj.eInfo = eInfo;
      rowObj.eDesc = eDesc;
      rowObj.eRow = eRow;
      this.#e.list.append(eRow);
      this.#rows.push( rowObj );
      this.#updateBtnAdd();
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
        file = new File([file], `pasted-image-${rowObj.id}.png`,
                        {type: file.type});
      }
      /*
        Fossil attachments treat the name as a unique-per-target key,
        with the newest one being the primary.  If a name is given
        twice, replace the prior entry before adding the new
        one. There are conceivable, but also unlikely, cases where
        this will have unintended side-effects, but that seems like a
        lesser evil than attaching the same file N times, leading to N
        attachment artifacts.
      */
      const old = this.#rowMatchingName(file.name);
      if( old && rowObj !== old){
        this.#removeRow(old);
      }
      rowObj.file = file;
      rowObj.mimeType = file.type || 'application/octet-stream';

      const lbl = file.name || 'Pasted Content';
      let szLbl;
      if( file.size < 500000 ){
        szLbl = file.size + ' bytes';
      }else if( file.size < 1000000 ){
        szLbl = (file.size / 1024).toFixed(2)+' KB';
      }else{
        szLbl = (file.size / (1024 * 1024)).toFixed(2)+' MB';
      }
      rowObj.eInfo.textContent = `${lbl} (${szLbl}, ${rowObj.mimeType})`;
      rowObj.eDropzone.classList.add('populated');
      rowObj.eDesc.classList.remove('hidden');
    }

    /**
       Returns an array of objects describing the currently-selected
       attachments.
    */
    collectState(){
      const rv = [];
      for(let r of this.#rows){
        if( !r.eDropzone?.classList?.contains?.('populated') ){
          continue;
        }
        rv.push(Object.assign(Object.create(null),{
          name: r.file.name || `pasted-content-${r.id}.${r.mimeType.split('/')[1] || 'txt'}`,
          content: r.file,
          description: r.eDesc?.value || '',
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
        if( s.mimetype ){
          fd.append(`${namePrefix}${suffix}_mimetype`, s.mimetype);
        }
      }
      return i;
    }
  }/*Attacher*/;

  F.Attacher = Attacher;

})(window.fossil);
