"use strict";
(function(F){
  const E = (s)=>document.querySelector(s),
        EA = (s)=>document.querySelectorAll(s),
        D = F.dom;

  const stopEvent = function(e){
    e.preventDefault();
    e.stopPropagation();
  };

  /**
     Creates a TabManager. If passed an argument, it is
     passed to init().
  */
  const TabManager = function(domElem){
    this.e = {};
    if(domElem) this.init(domElem);
  };

  const tabArg = function(arg){
    if('string'===typeof arg) arg = E(arg);
    return arg;
  };

  const setVisible = function(e,yes){
    D[yes ? 'removeClass' : 'addClass'](e, 'hidden');
  };

  TabManager.prototype = {
    /**
       Initializes the tabs associated with the given tab container
       (DOM element or selector for a single element).

       The tab container must have an 'id' attribute. This function
       looks through the DOM for all elements which have
       data-tab-parent=thatId. For each one it creates a button to
       switch to that tab and moves the element into this.e.tabs.

       When it's done, it auto-selects the first tab.

       This method must only be called once per instance. TabManagers
       may be nested but may not share any tabs.

       Returns this object.
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
      const childs = EA('[data-tab-parent='+cID+']');
      childs.forEach((c)=>this.addTab(c));
      return this.switchToTab(this.e.tabs.firstChild);
    },
    /**
       For the given tab element or unique selector string, returns
       the button associated with that tab, or undefined if the
       argument does not match any current tab.
    */
    getButtonForTab: function(tab){
      tab = tabArg(tab);
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
    */
    addTab: function(tab){
      tab = tabArg(tab);
      tab.remove();
      D.append(this.e.tabs, D.addClass(tab,'tab-panel'));
      const lbl = tab.dataset.tabLabel || 'Tab #'+this.e.tabs.childNodes.length;
      const btn = D.button(lbl);
      D.append(this.e.tabBar,btn);
      const self = this;
      btn.addEventListener('click',function(e){
        //stopEvent(e);
        self.switchToTab(tab);
      }, false);
      return this;
    },
    /**
       If the given DOM element or unique selector is one of this
       object's tabs, the UI makes that tab the currently-visible
       one. Returns this object.
    */
    switchToTab: function(tab){
      tab = tabArg(tab);
      const self = this;
      this.e.tabs.childNodes.forEach((e,ndx)=>{
        const btn = this.e.tabBar.childNodes[ndx];
        if(e===tab){
          setVisible(e, true);
          D.addClass(btn,'selected');
        }else{
          setVisible(e, false);
          D.removeClass(btn,'selected');
        }
      });
      return this;
    }
  };

  F.TabManager = TabManager;
})(window.fossil);
