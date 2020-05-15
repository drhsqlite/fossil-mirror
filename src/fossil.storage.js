(function(F){
  /**
     fossil.store is a basic wrapper around localStorage
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
          $:{},
          setItem: function(k,v){this.$[k]=v},
          getItem: function(k){
            return this.$.hasOwnProperty(k) ? this.$[k] : undefined;
          },
          removeItem: function(k){delete this.$[k]},
          clear: function(){this.$={}}
        });

  /**
     A proxy for localStorage or sessionStorage or a
     page-instance-local proxy, if neither one is availble.

     Which exact storage implementation is uses is unspecified, and
     apps must not rely on it.
  */
  fossil.storage = {
    /** Sets the storage key k to value v, implicitly converting
        it to a string. */
    set: (k,v)=>$storage.setItem(k,v),
    /** Sets storage key k to JSON.stringify(v). */
    setJSON: (k,v)=>$storage.setItem(k,JSON.stringify(v)),
    /** Returns the value for the given storage key, or
        dflt if the key is not found in the storage. */
    get: function(k,dflt){
      return (
        this.isTransient() ? $storage.$ : $storage
      ).hasOwnProperty(k) ? $storage.getItem(k) : dflt;
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
    contains: function(k){
      return (
        this.isTransient() ? $storage.$ : $storage
      ).hasOwnProperty(k);
    },
    /** Removes the given key from the storage. */
    remove: (k)=>$storage.removeItem(k),
    /** Clears ALL keys from the storage. */
    clear: ()=>$storage.clear(),
    /** Returns an array of all keys currently in the storage. */
    keys: ()=>Object.keys($storage),
    /** Returns true if this storage is transient (only available
        until the page is reloaded), indicating that fileStorage
        and sessionStorage are unavailable. */
    isTransient: ()=>!($storage===window.localStorage
                       ||$storage===window.sessionStorage),
    /** Returns a symbolic name for the current storage mechanism. */
    storageImplName: function(){
      if($storage===window.localStorage) return 'localStorage';
      else if($storage===window.sessionStorage) return 'sessionStorage';
      else return 'transient';
    }
  };

})(window.fossil);
