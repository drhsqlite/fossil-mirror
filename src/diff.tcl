# The "diff --tk" command outputs prepends a "set fossilcmd {...}" line
# to this file, then runs this file using "tclsh" in order to display the
# graphical diff in a separate window.  A typical "set fossilcmd" line
# looks like this:
#
#     set fossilcmd {| "./fossil" diff --html -y -i -v}
#
# This header comment is stripped off by the "mkbuiltin.c" program.
#
set prog {
package require Tk

array set CFG {
  TITLE      {Fossil Diff}
  LN_COL_BG  #dddddd
  LN_COL_FG  #444444
  TXT_COL_BG #ffffff
  TXT_COL_FG #000000
  MKR_COL_BG #444444
  MKR_COL_FG #dddddd
  CHNG_BG    #d0d0ff
  ADD_BG     #c0ffc0
  RM_BG      #ffc0c0
  HR_FG      #888888
  HR_PAD_TOP 4
  HR_PAD_BTM 8
  FN_BG      #444444
  FN_FG      #ffffff
  FN_PAD     5
  ERR_FG     #ee0000
  PADX       5
  WIDTH      80
  HEIGHT     45
  LB_HEIGHT  25
}

if {![namespace exists ttk]} {
  interp alias {} ::ttk::scrollbar {} ::scrollbar
  interp alias {} ::ttk::menubutton {} ::menubutton
}

proc dehtml {x} {
  set x [regsub -all {<[^>]*>} $x {}]
  return [string map {&amp; & &lt; < &gt; > &#39; ' &quot; \"} $x]
}

proc cols {} {
  return [list .lnA .txtA .mkr .lnB .txtB]
}

proc colType {c} {
  regexp {[a-z]+} $c type
  return $type
}

proc getLine {difftxt N iivar} {
  upvar $iivar ii
  if {$ii>=$N} {return -1}
  set x [lindex $difftxt $ii]
  incr ii
  return $x
}

proc readDiffs {fossilcmd} {
  global difftxt
  if {![info exists difftxt]} {
    set in [open $fossilcmd r]
    fconfigure $in -encoding utf-8
    set difftxt [split [read $in] \n]
    close $in
  }
  set N [llength $difftxt]
  set ii 0
  set nDiffs 0
  array set widths {txt 0 ln 0 mkr 0}
  while {[set line [getLine $difftxt $N ii]] != -1} {
    set fn2 {}
    if {![regexp {^=+ (.*?) =+ versus =+ (.*?) =+$} $line all fn fn2]
     && ![regexp {^=+ (.*?) =+$} $line all fn]
    } {
      continue
    }
    set errMsg ""
    set line [getLine $difftxt $N ii]
    if {[string compare -length 6 $line "<table"]
     && ![regexp {<p[^>]*>(.+)} $line - errMsg]} {
      continue
    }
    incr nDiffs
    set idx [expr {$nDiffs > 1 ? [.txtA index end] : "1.0"}]
    .wfiles.lb insert end $fn

    foreach c [cols] {
      if {$nDiffs > 1} {
        $c insert end \n -
      }
      if {[colType $c] eq "txt"} {
        $c insert end $fn\n fn
        if {$fn2!=""} {set fn $fn2}
      } else {
        $c insert end \n fn
      }
      $c insert end \n -

      if {$errMsg ne ""} continue
      while {[getLine $difftxt $N ii] ne "<pre>"} continue
      set type [colType $c]
      set str {}
      while {[set line [getLine $difftxt $N ii]] ne "</pre>"} {
        set len [string length [dehtml $line]]
        if {$len > $widths($type)} {
          set widths($type) $len
        }
        append str $line\n
      }

      set re {<span class="diff([a-z]+)">([^<]*)</span>}
      # Use \r as separator since it can't appear in the diff output (it gets
      # converted to a space).
      set str [regsub -all $re $str "\r\\1\r\\2\r"]
      foreach {pre class mid} [split $str \r] {
        if {$class ne ""} {
          $c insert end [dehtml $pre] - [dehtml $mid] [list $class -]
        } else {
          $c insert end [dehtml $pre] -
        }
      }
    }

    if {$errMsg ne ""} {
      foreach c {.txtA .txtB} {$c insert end [string trim $errMsg] err}
      foreach c [cols] {$c insert end \n -}
    }
  }

  foreach c [cols] {
    set type [colType $c]
    if {$type ne "txt"} {
      $c config -width $widths($type)
    }
    $c config -state disabled
  }
  if {$nDiffs <= [.wfiles.lb cget -height]} {
    .wfiles.lb config -height $nDiffs
    grid remove .wfiles.sb
  }

  return $nDiffs
}

proc viewDiff {idx} {
  .txtA yview $idx
  .txtA xview moveto 0
}

proc cycleDiffs {{reverse 0}} {
  if {$reverse} {
    set range [.txtA tag prevrange fn @0,0 1.0]
    if {$range eq ""} {
      viewDiff {fn.last -1c}
    } else {
      viewDiff [lindex $range 0]
    }
  } else {
    set range [.txtA tag nextrange fn {@0,0 +1c} end]
    if {$range eq "" || [lindex [.txtA yview] 1] == 1} {
      viewDiff fn.first
    } else {
      viewDiff [lindex $range 0]
    }
  }
}

proc xvis {col} {
  set view [$col xview]
  return [expr {[lindex $view 1]-[lindex $view 0]}]
}

proc scroll-x {args} {
  set c .txt[expr {[xvis .txtA] < [xvis .txtB] ? "A" : "B"}]
  eval $c xview $args
}

interp alias {} scroll-y {} .txtA yview

proc noop {args} {}

proc enableSync {axis} {
  update idletasks
  interp alias {} sync-$axis {}
  rename _sync-$axis sync-$axis
}

proc disableSync {axis} {
  rename sync-$axis _sync-$axis
  interp alias {} sync-$axis {} noop
}

proc sync-x {col first last} {
  disableSync x
  $col xview moveto [expr {$first*[xvis $col]/($last-$first)}]
  foreach side {A B} {
    set sb .sbx$side
    set xview [.txt$side xview]
    if {[lindex $xview 0] > 0 || [lindex $xview 1] < 1} {
      grid $sb
      eval $sb set $xview
    } else {
      grid remove $sb
    }
  }
  enableSync x
}

proc sync-y {first last} {
  disableSync y
  foreach c [cols] {
    $c yview moveto $first
  }
  if {$first > 0 || $last < 1} {
    grid .sby
    .sby set $first $last
  } else {
    grid remove .sby
  }
  enableSync y
}

wm withdraw .
wm title . $CFG(TITLE)
wm iconname . $CFG(TITLE)
# Keystroke bindings for on the top-level window for navigation and
# control also fire when those same keystrokes are pressed in the
# Search entry box.  Disable them, to prevent the diff screen from
# disappearing abruptly and unexpectedly when searching for "q".
#
# bind . <q> exit
# bind . <p> {catch searchPrev; break}
# bind . <n> {catch searchNext; break}
# bind . <Escape><Escape> exit
bind . <Destroy> {after 0 exit}
bind . <Tab> {cycleDiffs; break}
bind . <<PrevWindow>> {cycleDiffs 1; break}
bind . <Control-f> {searchOnOff; break}
bind . <Control-g> {catch searchNext; break}
bind . <Return> {
  event generate bb.files <1>
  event generate .bb.files <ButtonRelease-1>
  break
}
foreach {key axis args} {
  Up    y {scroll -5 units}
  k     y {scroll -5 units}
  Down  y {scroll 5 units}
  j     y {scroll 5 units}
  Left  x {scroll -5 units}
  h     x {scroll -5 units}
  Right x {scroll 5 units}
  l     x {scroll 5 units}
  Prior y {scroll -1 page}
  b     y {scroll -1 page}
  Next  y {scroll 1 page}
  space y {scroll 1 page}
  Home  y {moveto 0}
  g     y {moveto 0}
  End   y {moveto 1}
} {
  bind . <$key> "scroll-$axis $args; break"
  bind . <Shift-$key> continue
}

frame .bb
::ttk::menubutton .bb.files -text "Files"
if {[tk windowingsystem] eq "win32"} {
  ::ttk::style theme use winnative
  .bb.files configure -padding {20 1 10 2}
}
toplevel .wfiles
wm withdraw .wfiles
update idletasks
wm transient .wfiles .
wm overrideredirect .wfiles 1
listbox .wfiles.lb -width 0 -height $CFG(LB_HEIGHT) -activestyle none \
  -yscroll {.wfiles.sb set}
::ttk::scrollbar .wfiles.sb -command {.wfiles.lb yview}
grid .wfiles.lb .wfiles.sb -sticky ns
bind .bb.files <1> {
  set x [winfo rootx %W]
  set y [expr {[winfo rooty %W]+[winfo height %W]}]
  wm geometry .wfiles +$x+$y
  wm deiconify .wfiles
  focus .wfiles.lb
}
bind .wfiles <FocusOut> {wm withdraw .wfiles}
bind .wfiles <Escape> {focus .}
foreach evt {1 Return} {
  bind .wfiles.lb <$evt> {
    catch {
      set idx [lindex [.txtA tag ranges fn] [expr {[%W curselection]*2}]]
      viewDiff $idx
    }
    focus .
    break
  }
}
bind .wfiles.lb <Motion> {
  %W selection clear 0 end
  %W selection set @%x,%y
}

foreach {side syncCol} {A .txtB B .txtA} {
  set ln .ln$side
  text $ln
  $ln tag config - -justify right

  set txt .txt$side
  text $txt -width $CFG(WIDTH) -height $CFG(HEIGHT) -wrap none \
    -xscroll "sync-x $syncCol"
  catch {$txt config -tabstyle wordprocessor} ;# Required for Tk>=8.5
  foreach tag {add rm chng} {
    $txt tag config $tag -background $CFG([string toupper $tag]_BG)
    $txt tag lower $tag
  }
  $txt tag config fn -background $CFG(FN_BG) -foreground $CFG(FN_FG) \
    -justify center
  $txt tag config err -foreground $CFG(ERR_FG)
}
text .mkr

foreach c [cols] {
  set keyPrefix [string toupper [colType $c]]_COL_
  if {[tk windowingsystem] eq "win32"} {$c config -font {courier 9}}
  $c config -bg $CFG(${keyPrefix}BG) -fg $CFG(${keyPrefix}FG) -borderwidth 0 \
    -padx $CFG(PADX) -yscroll sync-y
  $c tag config hr -spacing1 $CFG(HR_PAD_TOP) -spacing3 $CFG(HR_PAD_BTM) \
     -foreground $CFG(HR_FG)
  $c tag config fn -spacing1 $CFG(FN_PAD) -spacing3 $CFG(FN_PAD)
  bindtags $c ". $c Text all"
  bind $c <1> {focus %W}
}

::ttk::scrollbar .sby -command {.txtA yview} -orient vertical
::ttk::scrollbar .sbxA -command {.txtA xview} -orient horizontal
::ttk::scrollbar .sbxB -command {.txtB xview} -orient horizontal
frame .spacer

if {[readDiffs $fossilcmd] == 0} {
  tk_messageBox -type ok -title $CFG(TITLE) -message "No changes"
  exit
}
update idletasks

proc saveDiff {} {
  set fn [tk_getSaveFile]
  if {$fn==""} return
  set out [open $fn wb]
  puts $out "#!/usr/bin/tclsh\n#\n# Run this script using 'tclsh' or 'wish'"
  puts $out "# to see the graphical diff.\n#"
  puts $out "set fossilcmd {}"
  puts $out "set prog [list $::prog]"
  puts $out "set difftxt \173"
  foreach e $::difftxt {puts $out [list $e]}
  puts $out "\175"
  puts $out "eval \$prog"
  close $out
}
proc invertDiff {} {
  global CFG
  array set x [grid info .txtA]
  if {$x(-column)==1} {
    grid config .lnB -column 0
    grid config .txtB -column 1
    .txtB tag config add -background $CFG(RM_BG)
    grid config .lnA -column 3
    grid config .txtA -column 4
    .txtA tag config rm -background $CFG(ADD_BG)
    .bb.invert config -text Uninvert
  } else {
    grid config .lnA -column 0
    grid config .txtA -column 1
    .txtA tag config rm -background $CFG(RM_BG)
    grid config .lnB -column 3
    grid config .txtB -column 4
    .txtB tag config add -background $CFG(ADD_BG)
    .bb.invert config -text Invert
  }
  .mkr config -state normal
  set clt [.mkr search -all < 1.0 end]
  set cgt [.mkr search -all > 1.0 end]
  foreach c $clt {.mkr replace $c "$c +1 chars" >}
  foreach c $cgt {.mkr replace $c "$c +1 chars" <}
  .mkr config -state disabled
}
proc searchOnOff {} {
  if {[info exists ::search]} {
    unset ::search
    .txtA tag remove search 1.0 end
    .txtB tag remove search 1.0 end
    pack forget .bb.sframe
  } else {
    set ::search .txtA
    if {![winfo exists .bb.sframe]} {
      frame .bb.sframe
      ::ttk::entry .bb.sframe.e -width 10
      pack .bb.sframe.e -side left -fill y -expand 1
      bind .bb.sframe.e <Return> {searchNext; break}
      ::ttk::button .bb.sframe.nx -text \u2193 -width 1 -command searchNext
      ::ttk::button .bb.sframe.pv -text \u2191 -width 1 -command searchPrev
      tk_optionMenu .bb.sframe.typ ::search_type \
           Exact {No Case} {RegExp} {Whole Word}
      .bb.sframe.typ config -width 10
      set ::search_type Exact
      pack .bb.sframe.nx .bb.sframe.pv .bb.sframe.typ -side left
    }
    pack .bb.sframe -side left
    after idle {focus .bb.sframe.e}
  }
}
proc searchNext {} {searchStep -forwards +1 1.0 end}
proc searchPrev {} {searchStep -backwards -1 end 1.0}
proc searchStep {direction incr start stop} {
  set pattern [.bb.sframe.e get]
  if {$pattern==""} return
  set count 0
  set w $::search
  if {"$w"==".txtA"} {set other .txtB} {set other .txtA}
  if {[lsearch [$w mark names] search]<0} {
    $w mark set search $start
  }
  switch $::search_type {
    Exact        {set st -exact}
    {No Case}    {set st -nocase}
    {RegExp}     {set st -regexp}
    {Whole Word} {set st -regexp; set pattern \\y$pattern\\y}
  }
  set idx [$w search -count count $direction $st -- \
              $pattern "search $incr chars" $stop]
  if {"$idx"==""} {
    set idx [$other search -count count $direction $st -- $pattern $start $stop]
    if {"$idx"!=""} {
      set this $w
      set w $other
      set other $this
    } else {
      set idx [$w search -count count $direction $st -- $pattern $start $stop]
    }
  }
  $w tag remove search 1.0 end
  $w mark unset search
  $other tag remove search 1.0 end
  $other mark unset search
  if {"$idx"!=""} {
    $w mark set search $idx
    $w yview -pickplace $idx
    $w tag add search search "$idx +$count chars"
    $w tag config search -background {#fcc000}
  }
  set ::search $w
}
::ttk::button .bb.quit -text {Quit} -command exit
::ttk::button .bb.invert -text {Invert} -command invertDiff
::ttk::button .bb.save -text {Save As...} -command saveDiff
::ttk::button .bb.search -text {Search} -command searchOnOff
pack .bb.quit .bb.invert -side left
if {$fossilcmd!=""} {pack .bb.save -side left}
pack .bb.files .bb.search -side left
grid rowconfigure . 1 -weight 1
grid columnconfigure . 1 -weight 1
grid columnconfigure . 4 -weight 1
grid .bb -row 0 -columnspan 6
eval grid [cols] -row 1 -sticky nsew
grid .sby -row 1 -column 5 -sticky ns
grid .sbxA -row 2 -columnspan 2 -sticky ew
grid .spacer -row 2 -column 2
grid .sbxB -row 2 -column 3 -columnspan 2 -sticky ew

.spacer config -height [winfo height .sbxA]
wm deiconify .
}
eval $prog
