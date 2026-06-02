"use strict";
/**
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
    */
    constructor(opt){
      this.#opt = opt = Object.assign(Object.create(null),opt);
      const eBtnAdd = this.#e.btnAdd = D.addClass(
        D.button(this.#opt.addButtonLabel || 'Add attachment',
                 ()=>this.#addRow()),
        'attach-add-button'
      );
      eBtnAdd.type = 'button';
      this.#e.list = D.addClass(D.div(), 'attach-container')
      opt.container.appendChild(this.#e.list);
      this.#e.list.appendChild(eBtnAdd);
    }

    #removeRow(id){
      const e = this.#opt.container.querySelector('[data-id="'+id+'"]');
      if( e ){
        e.remove();
        this.#rows = this.#rows.filter(v=>v.id!==+id);
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
        "Click to select, drop file, or paste content here"
      );
      D.append(eDropzone, eInfo, eFile);
      const eDesc = D.addClass(
        D.attr(D.textarea(), 'placeholder',
               'Optional description...'),
        'hidden'
      );
      const eRemove = D.addClass(
        D.button('Remove', ()=>this.#removeRow(id)),
        'attach-row-remove'
      );
      eRemove.type = 'button';

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
              const blob = new Blob([text], {type: 'text/plain'});
              blob.name = `pasted-text-${id}.txt`;
              this.#injestBlob(rowObj, blob);
            });
            break;
          }
        }
      });
      D.append(eRow, eDropzone, eDesc, eRemove);
      rowObj.eDropzone = eDropzone;
      rowObj.eInfo = eInfo;
      rowObj.eDesc = eDesc;
      this.#rows.push( rowObj );
      this.#e.list.append(eRow, this.#e.btnAdd);
    }

    #injestBlob(rowObj, file){
      if( !file ) return;
      rowObj.file = file;
      rowObj.mimeType = file.type || 'application/octet-stream';

      const lbl = file.name || 'Pasted Content';
      const szLbl = (file.size / 1024).toFixed(2)+' KB';
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
          name: r.file.name || `pasted-content-${r.id}.${row.mimeType.split('/')[1] || 'txt'}`,
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
      for(const ndx in st){
        const s = st[ndx];
        const suffix = ndx+1;
        fd.append(`${namePrefix}${suffix}`, s.content, s.name);
        const d = s.description?.trim?.();
        if( d ){
          fd.append(`${namePrefix}${suffix}_desc`, d);
        }
      }
      return st.length;
    }
  }/*Attacher*/;

  F.Attacher = Attacher;

})(window.fossil);
