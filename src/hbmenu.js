/*
** Originally: Copyright © 2018 Warren Young
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Contact: wyoung on the Fossil forum, https://fossil-scm.org/forum/
** Modified by others.
**
*******************************************************************************
**
** This file contains the JS code used to implement the expanding hamburger
** menu on various skins.
**
** This was original the "js.txt" file for the default skin.  It was subsequently
** moved into src/hbmenu.js so that it could be more easily reused by other skins
** using the "builtin_request_js" TH1 command.
**
** Operation:
**
** This script expects the HTML to contain two elements:
**
**      <a id="hbbtn">       <--- The hamburger menu button
**      <nav id="hbdrop">    <--- Container for the hamburger menu
**      
** Bindings are made on hbbtn so that when it is clicked, the following
** happens:
**
**    1.  An XHR is made to /sitemap?popup to fetch the HTML for the
**        popup menu.
**
**    2.  The HTML for the popup is inserted into hddrop.
**
**    3.  The hddrop container is made visible.
**
** CSS rules are also needed to cause the hddrop to be initially invisible,
** and to correctly style and position the hddrop container.
*/
(function() {
  var hbButton = document.getElementById("hbbtn");
  if (!hbButton) return;   // no hamburger button
  if (!document.addEventListener) return; // Incompatible browser
  var panel = document.getElementById("hbdrop");
  if (!panel) return;   // site admin might've nuked it
  if (!panel.style) return;  // shouldn't happen, but be sure
  var panelBorder = panel.style.border;
  var panelInitialized = false;   // reset if browser window is resized
  var panelResetBorderTimerID = 0;   // used to cancel post-animation tasks

  // Disable animation if this browser doesn't support CSS transitions.
  //
  // We need this ugly calling form for old browsers that don't allow
  // panel.style.hasOwnProperty('transition'); catering to old browsers
  // is the whole point here.
  var animate = panel.style.transition !== null && (typeof(panel.style.transition) == "string");

  // The duration of the animation can be overridden from the default skin
  // header.txt by setting the "data-anim-ms" attribute of the panel.
  var animMS = panel.getAttribute("data-anim-ms");
  if (animMS) {           // not null or empty string, parse it
    animMS = parseInt(animMS);
    if (isNaN(animMS) || animMS == 0)
      animate = false;    // disable animation if non-numeric or zero
    else if (animMS < 0)
      animMS = 400;       // set default animation duration if negative
  }
  else                    // attribute is null or empty string, use default
    animMS = 400;

  // Calculate panel height despite its being hidden at call time.
  // Based on https://stackoverflow.com/a/29047447/142454
  var panelHeight;  // computed on first panel display
  function calculatePanelHeight() {

    // Clear the max-height CSS property in case the panel size is recalculated
    // after the browser window was resized.
    panel.style.maxHeight = '';

    // Get initial panel styles so we can restore them below.
    var es   = window.getComputedStyle(panel),
        edis = es.display,
        epos = es.position,
        evis = es.visibility;

    // Restyle the panel so we can measure its height while invisible.
    panel.style.visibility = 'hidden';
    panel.style.position   = 'absolute';
    panel.style.display    = 'block';
    panelHeight = panel.offsetHeight + 'px';

    // Revert styles now that job is done.
    panel.style.display    = edis;
    panel.style.position   = epos;
    panel.style.visibility = evis;
  }

  // Show the panel by changing the panel height, which kicks off the
  // slide-open/closed transition set up in the XHR onload handler.
  //
  // Schedule the change for a near-future time in case this is the
  // first call, where the div was initially invisible.  If we were
  // to change the panel's visibility and height at the same time
  // instead, that would prevent the browser from seeing the height
  // change as a state transition, so it'd skip the CSS transition:
  //
  // https://developer.mozilla.org/en-US/docs/Web/CSS/CSS_Transitions/Using_CSS_transitions#JavaScript_examples
  function showPanel() {
    // Cancel the timer to remove the panel border after the closing animation,
    // otherwise double-clicking the hamburger button with the panel opened will
    // remove the borders from the (closed and immediately reopened) panel.
    if (panelResetBorderTimerID) {
      clearTimeout(panelResetBorderTimerID);
      panelResetBorderTimerID = 0;
    }
    if (animate) {
      if (!panelInitialized) {
        panelInitialized = true;
        // Set up a CSS transition to animate the panel open and
        // closed.  Only needs to be done once per page load.
        // Based on https://stackoverflow.com/a/29047447/142454
        calculatePanelHeight();
        panel.style.transition = 'max-height ' + animMS +
            'ms ease-in-out';
        panel.style.overflowY  = 'hidden';
        panel.style.maxHeight  = '0';
      }
      setTimeout(function() {
        panel.style.maxHeight = panelHeight;
        panel.style.border    = panelBorder;
      }, 40);   // 25ms is insufficient with Firefox 62
    }
    panel.style.display = 'block';
    document.addEventListener('keydown',panelKeydown,/* useCapture == */true);
    document.addEventListener('click',panelClick,false);
  }

  var panelKeydown = function(event) {
    var key = event.which || event.keyCode;
    if (key == 27) {
      event.stopPropagation();   // ignore other keydown handlers
      panelToggle(true);
    }
  };

  var panelClick = function(event) {
    if (!panel.contains(event.target)) {
      // Call event.preventDefault() to have clicks outside the opened panel
      // just close the panel, and swallow clicks on links or form elements.
      //event.preventDefault();
      panelToggle(true);
    }
  };

  // Return true if the panel is showing.
  function panelShowing() {
    if (animate) {
      return panel.style.maxHeight == panelHeight;
    }
    else {
      return panel.style.display == 'block';
    }
  }

  // Check if the specified HTML element has any child elements. Note that plain
  // text nodes, comments, and any spaces (presentational or not) are ignored.
  function hasChildren(element) {
    var childElement = element.firstChild;
    while (childElement) {
      if (childElement.nodeType == 1) // Node.ELEMENT_NODE == 1
        return true;
      childElement = childElement.nextSibling;
    }
    return false;
  }

  // Reset the state of the panel to uninitialized if the browser window is
  // resized, so the dimensions are recalculated the next time it's opened.
  window.addEventListener('resize',function(event) {
    panelInitialized = false;
  },false);

  // Click handler for the hamburger button.
  hbButton.addEventListener('click',function(event) {
    // Break the event handler chain, or the handler for document → click
    // (about to be installed) may already be triggered by the current event.
    event.stopPropagation();
    event.preventDefault();  // prevent browser from acting on <a> click
    panelToggle(false);
  },false);

  function panelToggle(suppressAnimation) {
    if (panelShowing()) {
      document.removeEventListener('keydown',panelKeydown,/* useCapture == */true);
      document.removeEventListener('click',panelClick,false);
      // Transition back to hidden state.
      if (animate) {
        if (suppressAnimation) {
          var transition = panel.style.transition;
          panel.style.transition = '';
          panel.style.maxHeight = '0';
          panel.style.border = 'none';
          setTimeout(function() {
            // Make sure CSS transition won't take effect now, so restore it
            // asynchronously. Outer variable 'transition' still valid here.
            panel.style.transition = transition;
          }, 40);   // 25ms is insufficient with Firefox 62
        }
        else {
          panel.style.maxHeight = '0';
          panelResetBorderTimerID = setTimeout(function() {
            // Browsers show a 1px high border line when maxHeight == 0,
            // our "hidden" state, so hide the borders in that state, too.
            panel.style.border = 'none';
            panelResetBorderTimerID = 0;   // clear ID of completed timer
          }, animMS);
        }
      }
      else {
        panel.style.display = 'none';
      }
    }
    else {
      if (!hasChildren(panel)) {
        // Only get the sitemap once per page load: it isn't likely to
        // change on us.
        var xhr = new XMLHttpRequest();
        xhr.onload = function() {
          var doc = xhr.responseXML;
          if (doc) {
            var sm = doc.querySelector("ul#sitemap");
            if (sm && xhr.status == 200) {
              // Got sitemap.  Insert it into the drop-down panel.
              panel.innerHTML = sm.outerHTML;
              // Display the panel
              showPanel();
            }
          }
          // else, can't parse response as HTML or XML
        }
        // The extra "popup" query parameter is a single to the server that the
        // header and footer boiler-plate can be omitted.  The boiler-plate is
        // ignored if it is included.  The popup query parameter is just an
        // optimization.
        var url = hbButton.href + (hbButton.href.includes("?")?"&popup":"?popup")
        xhr.open("GET", url);
        xhr.responseType = "document";
        xhr.send();
      }
      else {
        showPanel();   // just show what we built above
      }
    }
  }
})();
