"use strict";
(function(F/*fossil object*/){
  const E = (s)=>document.querySelector(s),
        EA = (s)=>document.querySelectorAll(s),
        D = F.dom;

  /**
     Creates a TabManager. If passed a truthy first argument, it is
     passed to init(). If passed a truthy second argument, it must be
     an Object holding configuration options:

     { 
       tabAccessKeys: boolean (=true)
          If true, tab buttons are assigned "accesskey" values
          equal to their 1-based tab number.
     }
  */
  const TabManager = function(domElem, options){
    this.e = {};
    this.options = F.mergeLastWins(TabManager.defaultOptions , options);
    if(domElem) this.init(domElem);
  };

  /**
     Default values for the options object passed to the TabManager
     constructor. Changing these affects the defaults of all
     TabManager instances instantiated after that point.
  */
  TabManager.defaultOptions = {
    tabAccessKeys: true
  };

  /**
     Internal helper to normalize a method argument to a tab
     element. arg may be a tab DOM element, a selector string, or an
     index into tabMgr.e.tabs.childNodes. Returns the corresponding
     tab element.
  */
  const tabArg = function(arg,tabMgr){
    if('string'===typeof arg) arg = E(arg);
    else if(tabMgr && 'number'===typeof arg && arg>=0){
      arg = tabMgr.e.tabs.childNodes[arg];
    }
    return arg;
  };

  /**
    Sets sets the visibility of tab element e to on or off. e MUST be
    a TabManager tab element.
  */
  const setVisible = function(e,yes){
    D[yes ? 'removeClass' : 'addClass'](e, 'hidden');
  };

  TabManager.prototype = {
    /**
       Initializes the tabs associated with the given tab container
       (DOM element or selector for a single element). This must be
       called once before using any other member functions of a given
       instance, noting that the constructor will call this if it is
       passed an argument. 

       The tab container must have an 'id' attribute. This function
       looks through the DOM for all elements which have
       data-tab-parent=thatId. For each one it creates a button to
       switch to that tab and moves the element into this.e.tabs,
       *possibly* injecting an intermediary element between
       this.e.tabs and the element.

       The label for each tab is set by the data-tab-label attribute
       of each element, defaulting to something not terribly useful.

       When it's done, it auto-selects the first tab unless a tab has
       a truthy numeric value in its data-tab-select attribute, in
       which case the last tab to have such a property is selected.

       This method must only be called once per instance. TabManagers
       may be nested but must not share any tabs instances.

       Returns this object.

       DOM elements of potential interest to users:

       this.e.container = the outermost container element.

       this.e.tabBar = the button bar. Each "button" (whether it's a
       buttor not is unspecified) has a class of .tab-button.

       this.e.tabs = the parent for all of the tab elements.

       It is legal, within reason, to manipulate these a bit, in
       particular this.e.container, e.g. by adding more children to
       it. Do not remove elements from the tabs or tabBar, however, or
       the tab state may get sorely out of sync.

       CSS classes: the container element has whatever class(es) the
       client sets on. this.e.tabBar gets the 'tab-bar' class and
       this.e.tabs gets the 'tabs' class. It's hypothetically possible
       to move the tabs to either side or the bottom using only CSS,
       but it's never been tested.
    */
    init: function(container){
      container = tabArg(container);
      const cID = container.getAttribute('id');
      if(!cID){
        throw new Error("Tab container element is missing 'id' attribute.");
      }
      const c = this.e.container = container;
      this.e.tabBar = D.addClass(D.div(),'tab-bar');
      this.e.tabs = D.addClass(D.div(),'tabs');
      D.append(c, this.e.tabBar, this.e.tabs);
      let selectIndex = 0;
      EA('[data-tab-parent='+cID+']').forEach((c,n)=>{
        if(+c.dataset.tabSelect) selectIndex=n;
        this.addTab(c);
      });
      return this.switchToTab(selectIndex);
    },

    /**
       For the given tab element, unique selector string, or integer
       (0-based tab number), returns the button associated with that
       tab, or undefined if the argument does not match any current
       tab.
    */
    getButtonForTab: function(tab){
      tab = tabArg(tab,this);
      var i = -1;
      this.e.tabs.childNodes.forEach(function(e,n){
        if(e===tab) i = n;
      });
      return i>=0 ? this.e.tabBar.childNodes[i] : undefined;
    },
    /**
       Adds the given DOM element or unique selector as the next
       tab in the tab container, adding a button to switch to
       the tab. Returns this object.

       If this object's options include a truthy tabAccessKeys then
       each tab button gets assigned an accesskey attribute equal to
       its 1-based index in the tab list. e.g. key 1 is the first tab
       and key 5 is the 5th. Whether/how that accesskey is accessed is
       dependent on the browser and its OS:

       https://developer.mozilla.org/en-US/docs/Web/HTML/Global_attributes/accesskey
    */
    addTab: function f(tab){
      if(!f.click){
        f.click = function(e){
         e.target.$manager.switchToTab(e.target.$tab);
        };
      }
      tab = tabArg(tab);
      tab.remove();
      D.append(this.e.tabs, D.addClass(tab,'tab-panel'));
      const tabCount = this.e.tabBar.childNodes.length+1;
      const lbl = tab.dataset.tabLabel || 'Tab #'+tabCount;
      const btn = D.addClass(D.append(D.span(), lbl), 'tab-button');
      D.append(this.e.tabBar,btn);
      btn.$manager = this;
      btn.$tab = tab;
      if(this.options.tabAccessKeys){
        D.attr(btn, 'accesskey', tabCount);
      }
      btn.addEventListener('click', f.click, false);
      return this;
    },

    /**
       Internal. Fires a new CustomEvent to all listeners which have
       registered via this.addEventListener().
     */
    _dispatchEvent: function(name, detail){
      try{
        this.e.container.dispatchEvent(
          new CustomEvent(name, {detail: detail})
        );
      }catch(e){
        /* ignore */
      }
      return this;
    },

    /**
       Registers an event listener for this object's custom events.
       The callback gets a CustomEvent object with a 'detail'
       propertly holding any tab-related state for the event. The events
       are:

       - 'before-switch-from' is emitted immediately before a new tab
       is switched away from. detail = the tab element being switched
       away from.

       - 'before-switch-to' is emitted immediately before a new tab is
       switched to.  detail = the tab element.

       - 'after-switch-to' is emitted immediately after a new tab is
       switched to.  detail = the tab element.

       Any exceptions thrown by listeners are caught and ignored, to
       avoid that they knock the tab state out of sync.

       Returns this object.
    */
    addEventListener: function(eventName, callback){
      this.e.container.addEventListener(eventName, callback, false);
      return this;
    },

    /**
       Inserts the given DOM element immediately after the tab bar.
       Intended for a status bar or similar always-visible component.
       Returns this object.
    */
    addCustomWidget: function(e){
      this.e.container.insertBefore(e, this.e.tabs);
      return this;
    },

    /**
       If the given DOM element, unique selector, or integer (0-based
       tab number) is one of this object's tabs, the UI makes that tab
       the currently-visible one, firing any relevant events. Returns
       this object. If the argument is the current tab, this is a
       no-op, and no events are fired.
    */
    switchToTab: function(tab){
      tab = tabArg(tab,this);
      const self = this;
      if(tab===this._currentTab) return this;
      else if(this._currentTab){
        this._dispatchEvent('before-switch-from', this._currentTab);
      }
      delete this._currentTab;
      this.e.tabs.childNodes.forEach((e,ndx)=>{
        const btn = this.e.tabBar.childNodes[ndx];
        if(e===tab){
          if(D.hasClass(e,'selected')){
            return;
          }
          self._dispatchEvent('before-switch-to',tab);
          setVisible(e, true);
          this._currentTab = e;
          D.addClass(btn,'selected');
          self._dispatchEvent('after-switch-to',tab);
        }else{
          if(D.hasClass(e,'selected')){
            return;
          }
          setVisible(e, false);
          D.removeClass(btn,'selected');
        }
      });
      return this;
    }
  };

  F.TabManager = TabManager;
})(window.fossil);
