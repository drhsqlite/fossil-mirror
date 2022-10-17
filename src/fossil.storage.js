(function(F){
  /**
     fossil.storage is a basic wrapper around localStorage
     or sessionStorage or a dummy proxy object if neither
     of those are available.
  */
  const tryStorage = function f(obj){
    if(!f.key) f.key = 'fossil.access.check';
    try{
      obj.setItem(f.key, 'f');
      const x = obj.getItem(f.key);
      obj.removeItem(f.key);
      if(x!=='f') throw new Error(f.key+" failed")
      return obj;
    }catch(e){
      return undefined;
    }
  };

  /** Internal storage impl for fossil.storage. */
  const $storage =
        tryStorage(window.localStorage)
        || tryStorage(window.sessionStorage)
        || tryStorage({
          // A basic dummy xyzStorage stand-in
          $$$:{},
          setItem: function(k,v){this.$$$[k]=v},
          getItem: function(k){
            return this.$$$.hasOwnProperty(k) ? this.$$$[k] : undefined;
          },
          removeItem: function(k){delete this.$$$[k]},
          clear: function(){this.$$$={}}
        });

  /**
     For the dummy storage we need to differentiate between
     $storage and its real property storage for hasOwnProperty()
     to work properly...
  */
  const $storageHolder = $storage.hasOwnProperty('$$$') ? $storage.$$$ : $storage;

  /**
     A prefix which gets internally applied to all fossil.storage
     property keys so that localStorage and sessionStorage across the
     same browser profile instance do not "leak" across multiple repos
     being hosted by the same origin server. Such polination is still
     there but, with this key prefix applied, it won't be immediately
     visible via the storage API.

     With this in place we can justify using localStorage instead of
     sessionStorage again.

     One implication, it was discovered after the release of 2.12, of
     using localStorage and sessionStorage, is that their scope (the
     same "origin" and client application/profile) allows multiple
     repos on the same origin to use the same storage. Thus a user
     editing a wiki in /repoA/wikiedit could then see those edits in
     /repoB/wikiedit. The data do not cross user- or browser
     boundaries, though, so it "might" arguably be called a bug. Even
     so, it was never intended for that to happen. Rather than lose
     localStorage access altogether, storageKeyPrefix was added so
     that we can sandbox that state for the various repos.

     See: https://fossil-scm.org/forum/forumpost/4afc4d34de

     Sidebar: it might seem odd to provide a key prefix and stick all
     properties in the topmost level of the storage object. We do that
     because adding a layer of object to sandbox each repo would mean
     (de)serializing that whole tree on every storage property change
     (and we update storage often during editing sessions).
     e.g. instead of storageObject.projectName.foo we have
     storageObject[storageKeyPrefix+'foo']. That's soley for
     efficiency's sake (in terms of battery life and
     environment-internal storage-level effort). Even so, it might (or
     might not) be useful to do that someday.
  */
  const storageKeyPrefix = (
    $storageHolder===$storage/*localStorage or sessionStorage*/
      ? (
        F.config.projectCode || F.config.projectName
          || F.config.shortProjectName || window.location.pathname
      )+'::' : (
        '' /* transient storage */
      )
  );

  /**
     A proxy for localStorage or sessionStorage or a
     page-instance-local proxy, if neither one is availble.

     Which exact storage implementation is uses is unspecified, and
     apps must not rely on it.
  */
  F.storage = {
    storageKeyPrefix: storageKeyPrefix,
    /** Sets the storage key k to value v, implicitly converting
        it to a string. */
    set: (k,v)=>$storage.setItem(storageKeyPrefix+k,v),
    /** Sets storage key k to JSON.stringify(v). */
    setJSON: (k,v)=>$storage.setItem(storageKeyPrefix+k,JSON.stringify(v)),
    /** Returns the value for the given storage key, or
        dflt if the key is not found in the storage. */
    get: (k,dflt)=>$storageHolder.hasOwnProperty(
      storageKeyPrefix+k
    ) ? $storage.getItem(storageKeyPrefix+k) : dflt,
    /** Returns true if the given key has a value of "true".  If the
        key is not found, it returns true if the boolean value of dflt
        is "true". (Remember that JS persistent storage values are all
        strings.) */
    getBool: function(k,dflt){
      return 'true'===this.get(k,''+(!!dflt));
    },
    /** Returns the JSON.parse()'d value of the given
        storage key's value, or dflt is the key is not
        found or JSON.parse() fails. */
    getJSON: function f(k,dflt){
      try {
        const x = this.get(k,f);
        return x===f ? dflt : JSON.parse(x);
      }
      catch(e){return dflt}
    },
    /** Returns true if the storage contains the given key,
        else false. */
    contains: (k)=>$storageHolder.hasOwnProperty(storageKeyPrefix+k),
    /** Removes the given key from the storage. Returns this. */
    remove: function(k){
      $storage.removeItem(storageKeyPrefix+k);
      return this;
    },
    /** Clears ALL keys from the storage. Returns this. */
    clear: function(){
      this.keys().forEach((k)=>$storage.removeItem(/*w/o prefix*/k));
      return this;
    },
    /** Returns an array of all keys currently in the storage. */
    keys: ()=>Object.keys($storageHolder).filter((v)=>(v||'').startsWith(storageKeyPrefix)),
    /** Returns true if this storage is transient (only available
        until the page is reloaded), indicating that fileStorage
        and sessionStorage are unavailable. */
    isTransient: ()=>$storageHolder!==$storage,
    /** Returns a symbolic name for the current storage mechanism. */
    storageImplName: function(){
      if($storage===window.localStorage) return 'localStorage';
      else if($storage===window.sessionStorage) return 'sessionStorage';
      else return 'transient';
    },

    /**
       Returns a brief help text string for the currently-selected
       storage type.
    */
    storageHelpDescription: function(){
      return {
        localStorage: "Browser-local persistent storage with an "+
          "unspecified long-term lifetime (survives closing the browser, "+
          "but maybe not a browser upgrade).",
        sessionStorage: "Storage local to this browser tab, "+
          "lost if this tab is closed.",
        "transient": "Transient storage local to this invocation of this page."
      }[this.storageImplName()];
    }
  };

})(window.fossil);
