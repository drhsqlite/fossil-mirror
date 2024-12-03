# The "--tk" option to various merge commands prepends one or more
# "set fossilcmd(NAME) {...}" lines to this file, then runs this file using
# "tclsh" in order to show a graphical analysis of the merge results.
# A typical "set fossilcmd" line looks like this:
#
#     set fossilcmd(file1.txt) {| "./fossil" diff --tcl -i -v}
#
# This header comment is stripped off by the "mkbuiltin.c" program.
#
package require Tk

array set CFG_light {
  TITLE      {Fossil Merge}
  LN_COL_BG  #dddddd
  LN_COL_FG  #444444
  TXT_COL_BG #ffffff
  TXT_COL_FG #000000
  MKR_COL_BG #444444
  MKR_COL_FG #dddddd
  CHNG_BG    #d0d070
  ADD_BG     #c0ffc0
  RM_BG      #ffc0c0
  HR_FG      #444444
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

array set CFG_dark {
  TITLE      {Fossil Merge}
  LN_COL_BG  #dddddd
  LN_COL_FG  #444444
  TXT_COL_BG #3f3f3f
  TXT_COL_FG #dcdccc
  MKR_COL_BG #444444
  MKR_COL_FG #dddddd
  CHNG_BG    #6a6a00
  ADD_BG     #57934c
  RM_BG      #ef6767
  HR_FG      #444444
  HR_PAD_TOP 4
  HR_PAD_BTM 8
  FN_BG      #5e5e5e
  FN_FG      #ffffff
  FN_PAD     5
  ERR_FG     #ee0000
  PADX       5
  WIDTH      80
  HEIGHT     45
  LB_HEIGHT  25
}

array set CFG_arr {
  0          CFG_light
  1          CFG_dark
}

array set CFG [array get $CFG_arr($darkmode)]

if {![namespace exists ttk]} {
  interp alias {} ::ttk::scrollbar {} ::scrollbar
  interp alias {} ::ttk::menubutton {} ::menubutton
}

proc dehtml {x} {
  set x [regsub -all {<[^>]*>} $x {}]
  return [string map {&amp; & &lt; < &gt; > &#39; ' &quot; \"} $x]
}

proc cols {} {
  return [list .lnA .txtA .lnB .txtB .lnC .txtC .lnD .txtD]
}

proc colType {c} {
  regexp {[a-z]+} $c type
  return $type
}

proc getLine {mergetxt N iivar} {
  upvar $iivar ii
  if {$ii>=$N} {return -1}
  set x [lindex $mergetxt $ii]
  incr ii
  return $x
}

proc readMerge {fossilcmd} {
  global mergetxt
  if {![info exists mergetxt]} {
    set in [open $fossilcmd r]
    fconfigure $in -encoding utf-8
    set mergetxt [read $in]
    close $in
  }
  foreach c [cols] {
    $c delete 1.0 end
  }
  set lnA 1
  set lnB 1
  set lnC 1
  set lnD 1
  foreach {A B C D} $mergetxt {
    set key1 [string index $A 0]
    if {$key1=="S"} {
      set N [string range $A 1 end]
      incr lnA $N
      incr lnB $N
      incr lnC $N
      incr lnD $N
      .lnA insert end ...\n hrln
      .txtA insert end [string repeat . 30]\n hrtxt
      .lnB insert end ...\n hrln
      .txtB insert end [string repeat . 30]\n hrtxt
      .lnC insert end ...\n hrln
      .txtC insert end [string repeat . 30]\n hrtxt
      .lnD insert end ...\n hrln
      .txtD insert end [string repeat . 30]\n hrtxt
      continue
    }
    set key2 [string index $B 0]
    set key3 [string index $C 0]
    set key4 [string index $D 0]
    if {$key4=="X"} {set dtag rm} {set dtag -}
    if {$key1=="."} {
      .lnA insert end \n -
      .txtA insert end \n $dtag
    } elseif {$key1=="N"} {
      .nameA config -text [string range $A 1 end]
    } else {
      .lnA insert end $lnA\n -
      incr lnA
      .txtA insert end [string range $A 1 end]\n $dtag
    }
    if {$key2=="."} {
      .lnB insert end \n -
      .txtB insert end \n $dtag
    } else {
      .lnB insert end $lnB\n -
      incr lnB
      if {$key4=="2"} {set tag chng} {set tag $dtag}
      if {$key2=="1"} {
        .txtB insert end [string range $A 1 end]\n $tag
      } elseif {$key2=="N"} {
        .nameB config -text [string range $B 1 end]
      } else {
        .txtB insert end [string range $B 1 end]\n $tag
      }
    }
    if {$key3=="."} {
      .lnC insert end \n -
      .txtC insert end \n $dtag
    } else {
      .lnC insert end $lnC\n -
      incr lnC
      if {$key4=="3"} {set tag add} {set tag $dtag}
      if {$key3=="1"} {
        .txtC insert end [string range $A 1 end]\n $tag
      } elseif {$key3=="2"} {
        .txtC insert end [string range $B 1 end]\n chng
      } elseif {$key3=="N"} {
        .nameC config -text [string range $C 1 end]
      } else {
        .txtC insert end [string range $C 1 end]\n $tag
      }
    }
    if {$key4=="." || $key4=="X"} {
      .lnD insert end \n -
      .txtD insert end \n $dtag
    } else {
      .lnD insert end $lnD\n -
      incr lnD
      if {$key4=="1"} {
        .txtD insert end [string range $A 1 end]\n -
      } elseif {$key4=="2"} {
        .txtD insert end [string range $B 1 end]\n chng
      } elseif {$key4=="3"} {
        .txtD insert end [string range $C 1 end]\n add
      } elseif {$key4=="N"} {
        .nameD config -text [string range $D 1 end]
      } else {
        .txtD insert end [string range $D 1 end]\n -
      }
    }
  }
  foreach c [cols] {
    set type [colType $c]
    if {$type ne "txt"} {
      $c config -width 6; # $widths($type)
    }
    $c config -state disabled
  }
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
  foreach side {A B C D} {
    set sb .sbx$side
    set xview [.txt$side xview]
#    if {[lindex $xview 0] > 0 || [lindex $xview 1] < 1} {
#      grid $sb
#      eval $sb set $xview
#    } else {
#      grid remove $sb
#    }
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
bind . <Control-q> exit
bind . <Control-p> {catch searchPrev; break}
bind . <Control-n> {catch searchNext; break}
bind . <Escape><Escape> exit
bind . <Destroy> {after 0 exit}
bind . <Tab> {cycleDiffs; break}
bind . <<PrevWindow>> {cycleDiffs 1; break}
bind . <Control-f> {searchOnOff; break}
bind . <Control-g> {catch searchNext; break}
bind . <Return> {
  event generate .bb.files <1>
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


foreach {side syncCol} {A .txtB B .txtA C .txtC D .txtD} {
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
  $c tag config hrln -spacing1 $CFG(HR_PAD_TOP) -spacing3 $CFG(HR_PAD_BTM) \
     -foreground $CFG(HR_FG) -justify right
  $c tag config hrtxt  -spacing1 $CFG(HR_PAD_TOP) -spacing3 $CFG(HR_PAD_BTM) \
     -foreground $CFG(HR_FG) -justify center
  $c tag config fn -spacing1 $CFG(FN_PAD) -spacing3 $CFG(FN_PAD)
  bindtags $c ". $c Text all"
  bind $c <1> {focus %W}
}

label .nameA
label .nameB
label .nameC
label .nameD -text {Merge Result}
::ttk::scrollbar .sby -command {.txtA yview} -orient vertical
::ttk::scrollbar .sbxA -command {.txtA xview} -orient horizontal
::ttk::scrollbar .sbxB -command {.txtB xview} -orient horizontal
::ttk::scrollbar .sbxC -command {.txtC xview} -orient horizontal
::ttk::scrollbar .sbxD -command {.txtD xview} -orient horizontal
frame .spacer

readMerge $fossilcmd
update idletasks

proc searchOnOff {} {
  if {[info exists ::search]} {
    unset ::search
    .txtA tag remove search 1.0 end
    .txtB tag remove search 1.0 end
    .txtC tag remove search 1.0 end
    .txtD tag remove search 1.0 end
    pack forget .bb.sframe
    focus .
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
  switch $w {
    .txtA {set other .txtB}
    .txtB {set other .txtC}
    .txtC {set other .txtD}
    default {set other .txtA}
  }
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
::ttk::button .bb.search -text {Search} -command searchOnOff
pack .bb.quit -side left
# pack .bb.files -side left
pack .bb.search -side left
grid rowconfigure . 1 -weight 1
set rn 0
foreach {lnwid txtwid} [cols] {
  grid columnconfigure . $rn            -weight 1 -uniform a
  grid columnconfigure . [expr {$rn+1}] -weight 1 -uniform b
  incr rn 2
}
grid .bb -row 0 -columnspan 8
grid .nameA -row 1 -column 1 -sticky ew
grid .nameB -row 1 -column 3 -sticky ew
grid .nameC -row 1 -column 5 -sticky ew
grid .nameD -row 1 -column 7 -sticky ew
eval grid [cols] -row 2 -sticky nsew
grid .sby -row 2 -column 8 -sticky ns
grid .sbxA -row 3 -column 1 -sticky ew
grid .sbxB -row 3 -column 3 -sticky ew
grid .sbxC -row 3 -column 5 -sticky ew
grid .sbxD -row 3 -column 7 -sticky ew


.spacer config -height [winfo height .sbxA]
wm deiconify .
