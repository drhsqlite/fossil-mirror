# Show details of a 3-way merge operation.  The left-most column is the
# common ancestor.  The next two columns are edits of that common ancestor.
# The right-most column is the result of the merge.
#
# Several variables will have been initialized:
#
#    ncontext            The number of lines of context to show on each change
#
#    fossilexe           Pathname of the fossil program
#
#    filelist            A list of "merge-type filename" pairs.
#
#    darkmode            Boolean.  True for dark mode
#
#    debug               Boolean.  True for debugging output
#
# If the "filelist" global variable is defined, then it is a list of
# alternating "merge-type names" (ex: UPDATE, MERGE, CONFLICT, ERROR) and
# filenames.  In that case, the initial display shows the changes for
# the first pair on the list and there is a optionmenu that allows the
# user to select other fiels on the list.
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

proc readMerge {args} {
  global fossilexe ncontext current_file debug
  if {$ncontext=="All"} {
    set cmd "| $fossilexe merge-info -c -1"
  } else {
    set cmd "| $fossilexe merge-info -c $ncontext"
  }
  if {[info exists current_file]} {
    regsub {^[A-Z]+ } $current_file {} fn
    lappend cmd -tcl $fn
  }
  if {$debug} {
    regsub {^\| +} $cmd {} cmd2
    puts $cmd2
    flush stdout
  }
  if {[catch {
    set in [open $cmd r]
    fconfigure $in -encoding utf-8
    set mergetxt [read $in]
    close $in
  } msg]} {
    tk_messageBox -message "Unable to run command: \"$cmd\""
    set mergetxt {}
  }
  foreach c [cols] {
    $c config -state normal
    $c delete 1.0 end
  }
  set lnA 1
  set lnB 1
  set lnC 1
  set lnD 1
  foreach {A B C D} $mergetxt {
    set key1 [string index $A 0]
    if {$key1=="S"} {
      scan [string range $A 1 end] "%d %d %d %d" nA nB nC nD
      foreach x {A B C D} {
        set N [set n$x]
        incr ln$x $N
        if {$N>0} {
          .ln$x insert end ...\n hrln
          .txt$x insert end [string repeat . 30]\n hrtxt
        } else {
          .ln$x insert end \n hrln
          .txt$x insert end \n hrtxt
        }
      }
      continue
    }
    set key2 [string index $B 0]
    set key3 [string index $C 0]
    set key4 [string index $D 0]
    if {$key1=="."} {
      .lnA insert end \n -
      .txtA insert end \n -
    } elseif {$key1=="N"} {
      .nameA config -text [string range $A 1 end]
    } else {
      .lnA insert end $lnA\n -
      incr lnA
      if {$key1=="X"} {
        .txtA insert end [string range $A 1 end]\n rm
      } else {
        .txtA insert end [string range $A 1 end]\n -
      }
    }
    if {$key2=="."} {
      .lnB insert end \n -
      .txtB insert end \n -
    } elseif {$key2=="N"} {
      .nameB config -text [string range $B 1 end]
    } else {
      .lnB insert end $lnB\n -
      incr lnB
      if {$key4=="2"} {set tag chng} {set tag -}
      if {$key2=="1"} {
        .txtB insert end [string range $A 1 end]\n $tag
      } elseif {$key2=="X"} {
        .txtB insert end [string range $B 1 end]\n rm
      } else {
        .txtB insert end [string range $B 1 end]\n $tag
      }
    }
    if {$key3=="."} {
      .lnC insert end \n -
      .txtC insert end \n -
    } elseif {$key3=="N"} {
      .nameC config -text [string range $C 1 end]
    } else {
      .lnC insert end $lnC\n -
      incr lnC
      if {$key4=="3"} {set tag add} {set tag -}
      if {$key3=="1"} {
        .txtC insert end [string range $A 1 end]\n $tag
      } elseif {$key3=="2"} {
        .txtC insert end [string range $B 1 end]\n chng
      } elseif {$key3=="X"} {
        .txtC insert end [string range $C 1 end]\n rm
      } else {
        .txtC insert end [string range $C 1 end]\n $tag
      }
    }
    if {$key4=="."} {
      .lnD insert end \n -
      .txtD insert end \n -
    } elseif {$key4=="N"} {
      .nameD config -text [string range $D 1 end]
    } else {
      .lnD insert end $lnD\n -
      incr lnD
      if {$key4=="1"} {
        .txtD insert end [string range $A 1 end]\n -
      } elseif {$key4=="2"} {
        .txtD insert end [string range $B 1 end]\n chng
      } elseif {$key4=="3"} {
        .txtD insert end [string range $C 1 end]\n add
      } elseif {$key4=="X"} {
        .txtD insert end [string range $D 1 end]\n rm
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
  set mx $lnA
  if {$lnB>$mx} {set mx $lnB}
  if {$lnC>$mx} {set mx $lnC}
  if {$lnD>$mx} {set mx $lnD}
  global lnWidth
  set lnWidth [string length [format +%d $mx]]
  .lnA config -width $lnWidth
  .lnB config -width $lnWidth
  .lnC config -width $lnWidth
  .lnD config -width $lnWidth
  grid columnconfig . {0 2 4 6} -minsize $lnWidth
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
::ttk::menubutton .bb.diff2 -text {2-way diff} -menu .bb.diff2.m
menu .bb.diff2.m -tearoff 0
.bb.diff2.m add command -label {baseline vs. local} -command {two-way 12}
.bb.diff2.m add command -label {baseline vs. merge-in} -command {two-way 13}
.bb.diff2.m add command -label {local vs. merge-in} -command {two-way 23}

# Bring up a separate two-way diff between a pair of columns
# the argument is one of:
#    12       Baseline versus Local
#    13       Baseline versus Merge-in
#    23       Local versus Merge-in
#
proc two-way {mode} {
  global current_file fossilexe debug darkmode ncontext
  regsub {^[A-Z]+ } $current_file {} fn
  set cmd $fossilexe
  lappend cmd merge-info --diff$mode $fn -c $ncontext
  if {$darkmode} {
    lappend cmd --dark
  }
  if {$debug} {
    lappend cmd --tkdebug
    puts $cmd
    flush stdout
  }
  exec {*}$cmd &
}

set useOptionMenu 1
if {[info exists filelist]} {
  set current_file "[lindex $filelist 0] [lindex $filelist 1]"
  if {[llength $filelist]>2} {
    trace add variable current_file write readMerge
  
    if {$tcl_platform(os)=="Darwin" || [llength $filelist]<30} {
      set fnlist {}
      foreach {op fn} $filelist {lappend fnlist "$op $fn"}
      tk_optionMenu .bb.files current_file {*}$fnlist
    } else {
      set useOptionMenu 0
      ::ttk::menubutton .bb.files -text $current_file
      if {[tk windowingsystem] eq "win32"} {
        ::ttk::style theme use winnative
        .bb.files configure -padding {20 1 10 2}
      }
      toplevel .wfiles
      wm withdraw .wfiles
      update idletasks
      wm transient .wfiles .
      wm overrideredirect .wfiles 1
      set ht [expr {[llength $filelist]/2}]
      if {$ht>$CFG(LB_HEIGHT)} {set ht $CFG(LB_HEIGHT)}
      listbox .wfiles.lb -width 0 -height $ht -activestyle none \
        -yscroll {.wfiles.sb set}
      set mx 1
      foreach {op fn} $filelist {
        set n [string length $fn]
        if {$n>$mx} {set mx $n}
        .wfiles.lb insert end "$op $fn"
      }
      .bb.files config -width $mx
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
          set ii [%W curselection]
          set ::current_file [%W get $ii]
          .bb.files config -text $::current_file
          focus .
          break
        }
      }
      bind .wfiles.lb <Motion> {
        %W selection clear 0 end
        %W selection set @%x,%y
      }
    }
  }
}

label .bb.ctxtag -text "Context:"
set context_choices {3 6 12 25 50 100 All}
if {$ncontext<0} {set ncontext All}
trace add variable ncontext write readMerge
if {$tcl_platform(os)=="Darwin" || $useOptionMenu} {
  tk_optionMenu .bb.ctx ncontext {*}$context_choices
} else {
  ::ttk::menubutton .bb.ctx -text $ncontext
  if {[tk windowingsystem] eq "win32"} {
    ::ttk::style theme use winnative
    .bb.ctx configure -padding {20 1 10 2}
  }
  toplevel .wctx
  wm withdraw .wctx
  update idletasks
  wm transient .wctx .
  wm overrideredirect .wctx 1
  listbox .wctx.lb -width 0 -height 7 -activestyle none
  .wctx.lb insert end {*}$context_choices
  pack .wctx.lb
  bind .bb.ctx <1> {
    set x [winfo rootx %W]
    set y [expr {[winfo rooty %W]+[winfo height %W]}]
    wm geometry .wctx +$x+$y
    wm deiconify .wctx
    focus .wctx.lb
  }
  bind .wctx <FocusOut> {wm withdraw .wctx}
  bind .wctx <Escape> {focus .}
  foreach evt {1 Return} {
    bind .wctx.lb <$evt> {
      set ::ncontext [lindex $::context_choices [%W curselection]]
      .bb.ctx config -text $::ncontext
      focus .
      break
    }
  }
  bind .wctx.lb <Motion> {
    %W selection clear 0 end
    %W selection set @%x,%y
  }
}

foreach {side syncCol} {A .txtA B .txtB C .txtC D .txtD} {
  set ln .ln$side
  text $ln -width 6
  $ln tag config - -justify right

  set txt .txt$side
  text $txt -width $CFG(WIDTH) -height $CFG(HEIGHT) -wrap none \
    -xscroll ".sbx$side set"
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

set mxwidth [lindex [wm maxsize .] 0]
while {$CFG(WIDTH)>=40} {
  set wanted [expr {([winfo reqwidth .lnA]+[winfo reqwidth .txtA])*4+30}]
  if {$wanted<=$mxwidth} break
  incr CFG(WIDTH) -10
  .txtA config -width $CFG(WIDTH)
  .txtB config -width $CFG(WIDTH)
  .txtC config -width $CFG(WIDTH)
  .txtD config -width $CFG(WIDTH)
}

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
pack .bb.quit -side left -fill y
pack .bb.diff2 -side left -fill y
if {[winfo exists .bb.files]} {
  pack .bb.files -side left -fill y
}
pack .bb.ctxtag .bb.ctx -side left -fill y
pack .bb.search -side left -fill y
grid rowconfigure . 1 -weight 1 -minsize [winfo reqheight .nameA]
grid rowconfigure . 2 -weight 100
readMerge
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
grid columnconfigure . {0 2 4 6} \
   -weight 1 -uniform a -minsize [winfo reqwidth .lnA]
grid columnconfigure . {1 3 5 7} -weight 100 -uniform b

.spacer config -height [winfo height .sbxA]
wm deiconify .
