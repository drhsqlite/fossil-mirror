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

proc prev_next_diff { prev_next } {
  set range [.txtA tag nextrange active 1.0 end]
  if {$prev_next eq "prev"} {
    set idx0 [lindex $range 0]
    if {$idx0 eq ""} {set idx0 end}
    if {[.txtA compare $idx0 > @0,[winfo height .txtA]]} {
      set idx0 [.txtA index @0,[winfo height .txtA]]
    }
    set idx ""
    foreach tag [list add rm chng fn] {
      foreach w [list  .txtA .txtB] {
        lassign [$w tag prevrange $tag $idx0 1.0] a b
        if { $idx eq "" || ($a ne "" && [$w compare $a > $idx]) } {
          set idx $a
          set idx_end $b
          set tagB $tag
          set wB $w
        }
      }
    }
    if {$idx ne ""} {
      while 1 {
        lassign [$wB tag prevrange $tagB $idx 1.0] a b
        if {$b ne "" && [$wB compare $b == "$idx - 1 l lineend"]} {
          set idx $a
        } else {
          break
        }
      }
    }
  } else {
    set idx0 [lindex $range 1]
    if { $idx0 eq "" } { set idx0 1.0 }
    if { [.txtA compare $idx0 < @0,0] } {
      set idx0 [.txtA index @0,0]
    }
    set idx ""
    foreach tag [list add rm chng fn] {
      foreach w [list  .txtA .txtB] {
        lassign [$w tag nextrange $tag $idx0 end] a b
        if { $idx eq "" || ($a ne "" && [$w compare $a < $idx]) } {
          set idx $a
          set idx_end $b
          set tagB $tag
          set wB $w
        }
      }
    }
    if { $idx ne "" } {
      while 1 {
        lassign [$wB tag nextrange $tagB $idx_end end] a b
        if { $a ne "" && [$wB compare $a == "$idx_end + 1 l linestart"] } {
          set idx_end $b
        } else {
          break
        }
      }
    }
  }
  if { $idx eq "" } {
    bell
    return
  }
  set idx [.txtA index "$idx linestart"]
  if { $tagB ne "fn" } {
    set idx_end [.txtA index "$idx_end +1l linestart"]
  }
  .txtA tag remove active 1.0 end
  .txtA tag add active $idx $idx_end
  .txtA tag configure active -borderwidth 2 -relief raised\
                     -background #eeeeee -foreground black
  if { $tagB ne "fn" } {
    .txtA tag lower active
  } else {
    .txtA tag raise active
  }
  .txtA see 1.0
  .txtA see $idx
}

proc searchText {} {
  set rangeA [.txtA tag nextrange search 1.0 end]
  set rangeB [.txtB tag nextrange search 1.0 end]
  set idx0 [lindex $rangeA 1]
  if { $idx0 eq "" } { set idx0 [lindex $rangeB 1] }
  if { $idx0 eq "" } { set idx0 1.0 }
  set word [.bb.search get]
  if { [.txtA compare $idx0 < @0,0] } {
    set idx0 [.txtA index @0,0]
  }
  if { [info exists ::this_does_not_find] } {
    if { $::this_does_not_find eq  [list $idx0 $word] } {
      set idx0 1.0
    }
    unset ::this_does_not_find
  }
  set idx ""
  foreach w [list  .txtA .txtB] {
    foreach regexp [list 0 1] {
      switch $regexp {
        0 { set rexFlag "-exact" }
        1 { set rexFlag "-regexp" }
      }
      set err [catch {
        $w search -nocase $rexFlag -count count $word $idx0 end 
      } idx_i]
      if {!$err && $idx_i ne ""
           && ($idx eq "" || [$w compare $idx_i < $idx])} {
        set idx $idx_i
        set countB $count
        set wB $w
      }
    }
  }
  .txtA  tag remove search 1.0 end
  .txtB  tag remove search 1.0 end
  if { $idx eq "" } {
    bell
    set ::this_does_not_find [list $idx0 $word]
    return
  }
  set idx_end [$wB index "$idx + $countB c"]
  $wB tag add search $idx $idx_end
  $wB tag configure search -borderwidth 2 -relief raised\
                    -background orange -foreground black
  $wB tag raise search
  $wB see 1.0
  $wB see $idx
}

proc reopen { action } {
  if { ![regexp {[|]\s*(.*)} $::fossilcmd {} cmdList] } { return }
  set f [lindex $cmdList 0]
  set args_with_arg \
     [list binary branch context c diff-binary from r to W width]
  set skip_args [list html internal i side-by-side y tk]
  lassign "" argsDict files
  for { set i 2 } { $i < [llength $cmdList] } { incr i } {
    if { [string match "-*" [lindex $cmdList $i]] } {
      set n [string trimleft [lindex $cmdList $i] "-"]
      if { $n in $args_with_arg } {
        dict set argsDict $n [lindex $cmdList $i+1]
        incr i
      } elseif { $n ni $skip_args } {
        dict set argsDict $n 1
      }
    } else {
      lappend files [lindex $cmdList $i]
    }
  }
  switch $action {
    togglewhitespace {
      if { [dict exists $argsDict w]
           || [dict exists $argsDict ignore-all-space] } {
        dict unset argsDict w
        dict unset argsDict ignore-all-space
      } else {
        dict set argsDict w 1
      }
    }
    onefile {
      set range [.txtA tag nextrange fn "@0,0" "@0,[winfo height .txtA] +1l"]
      if { $range eq "" } { return }
      set file [string trim [.txtA get {*}$range]]
      set files [list $file]
      regexp -line {local-root:\s+(.*)} [exec $f info] {} dir
      cd $dir
    }
    allfiles {
      set files ""
    }
    prev -
    next {
      set widget [focus]
      if { $widget eq ".txtA" } {
        set from_to from
        if { ![dict exists $argsDict from] } {
          dict set argsDict from current
        }
      } elseif { $widget eq ".txtB" } {
        set from_to to
        if { ![dict exists $argsDict to] } {
          dict set argsDict to ckout
        }
      } else {
        tk_messageBox -message "Click on one of the panes to select it"
        return
      }
      lassign "" parent child current tag
      set err [catch { exec $f info [dict get $argsDict $from_to] } info]
      if { $err } {
        if { [dict get $argsDict $from_to] eq "ckout" } {
          set err [catch { exec $f info } info]
          if { !$err } { regexp {checkout:\s+(\S+)} $info {} parent }
        } else {
          bell
          return
        }
      } else {
        regexp {uuid:\s+(\S+)\s+(\S+)} $info {} current date
        regexp {parent:\s+(\S+)} $info {} parent
        regexp {child:\s+(\S+)} $info {} child
      }
      if { [llength $files] == 1 } {
        set file [lindex $files 0]
        set err [catch { exec $f finfo -b -limit 100 $file } info]
        if { $err } {
          bell
          return
        }
        if { $current eq "" } {
          if { $action eq "prev" } {
            regexp {^\S+} $info tag
          }
        } else {
          set current [string range $current 0 9]
          set prev ""
          set found 0
          foreach line [split $info \n] {
            regexp {(\S+)\s+(\S+)} $line {} currentL dateL
            if { $found } {
              set tag $currentL
              break
            } elseif { $currentL eq $current || $dateL < $date } {
              if { $action eq "next" } {
                set tag $prev
                break
              }
              set found 1
            }
            set prev $currentL  
          }
        }
      } else {
        if { $action eq "prev" } {
          set tag $parent
        } else {
          set tag $child
        }
      }
      if { $tag eq "" && $action eq "prev" } {
        bell
        return
      }
      if { $tag ne "" } {
        dict set argsDict $from_to $tag
      } else {
        dict unset argsDict $from_to
      }
      if { $from_to eq "to" && ![dict exists $argsDict from] } {
        dict set argsDict from current
      }
    }
  }

  set f_args ""
  dict for "n v" $argsDict {
    if { $n in $args_with_arg } {
      lappend f_args -$n $v
    } else {
      lappend f_args -$n
    }
  }
  lappend f_args {*}$files

  # note: trying to put two contiguous "-" gives an error
  exec $f diff -tk {*}$f_args &
  exit        
}

proc fossil_ui {} {
  if { ![regexp {[|]\s*(.*)} $::fossilcmd {} cmdList] } { return }
  set f [lindex $cmdList 0]
  exec $f ui &
}

proc searchToggle {} {
  set err [catch { pack info .bb.search }]
  if { $err } {
    pack .bb.search -side left -padx 5 -after .bb.files
    tk::TabToWindow .bb.search
  } else {
    .txtA  tag remove search 1.0 end
    .txtB  tag remove search 1.0 end
    pack  forget .bb.search
    focus .
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
bind . <q> exit
bind . <Destroy> {after 0 exit}
bind . <Tab> {cycleDiffs; break}
bind . <<PrevWindow>> {cycleDiffs 1; break}
bind . <Return> {
  event generate .bb.files <1>
  event generate .bb.files <ButtonRelease-1>
  break
}
foreach {key axis args} {
  Up    y {scroll -5 units}
  Down  y {scroll 5 units}
  Left  x {scroll -5 units}
  Right x {scroll 5 units}
  Prior y {scroll -1 page}
  Next  y {scroll 1 page}
  Home  y {moveto 0}
  End   y {moveto 1}
} {
  bind . <$key> "scroll-$axis $args; break"
  bind . <Shift-$key> continue
}

frame .bb
::ttk::menubutton .bb.files -text "Files"
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
  #exit
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
  } else {
    grid config .lnA -column 0
    grid config .txtA -column 1
    .txtA tag config rm -background $CFG(RM_BG)
    grid config .lnB -column 3
    grid config .txtB -column 4
    .txtB tag config add -background $CFG(ADD_BG)
  }
  .mkr config -state normal
  set clt [.mkr search -all < 1.0 end]
  set cgt [.mkr search -all > 1.0 end]
  foreach c $clt {.mkr replace $c "$c +1 chars" >}
  foreach c $cgt {.mkr replace $c "$c +1 chars" <}
  .mkr config -state disabled
}
proc bind_key_do { cmd } {
  if { [focus] eq ".bb.search" } { return -code continue }
  uplevel #0 $cmd
  return -code break
}
::ttk::menubutton .bb.actions -text "Actions" -menu  .bb.actions.m
menu .bb.actions.m -tearoff 0
.bb.actions.m add command -label "Go to previous diff" -acc "p" -command "prev_next_diff prev"
.bb.actions.m add command -label "Go to next diff" -acc "n" -command "prev_next_diff next"
.bb.actions.m add separator
.bb.actions.m add command -label "Search" -acc "f" -command "searchToggle;"
.bb.actions.m add command -label "Toggle whitespace" -acc "w" -command "reopen togglewhitespace"
.bb.actions.m add separator
.bb.actions.m add command -label "View one file" -acc "1" -command "reopen onefile"
.bb.actions.m add command -label "View all files" -acc "a" -command "reopen allfiles"
.bb.actions.m add separator
.bb.actions.m add command -label "Older version" -acc "Shift-P" -command "reopen prev"
.bb.actions.m add command -label "Newer version" -acc "Shift-N" -command "reopen next"
.bb.actions.m add command -label "Fossil ui" -acc "u" -command "fossil_ui"
::ttk::button .bb.quit -text {Quit} -command exit
::ttk::button .bb.invert -text {Invert} -command invertDiff
::ttk::button .bb.save -text {Save As...} -command saveDiff
::ttk::entry .bb.search -width 12

bind  .bb.search <Return> "searchText; break"
bind  .bb.search <Escape> "searchToggle; break"

bind  . <Key-f> [list bind_key_do "searchToggle"]
bind  . <Key-w> [list bind_key_do "reopen togglewhitespace"]
bind  . <Key-1> [list bind_key_do "reopen onefile"]
bind  . <Key-a> [list bind_key_do "reopen allfiles"]
bind  . <Key-P> [list bind_key_do "reopen prev"]
bind  . <Key-N> [list bind_key_do "reopen next"]  
bind  . <Key-u> [list bind_key_do "fossil_ui"]  

lassign [list "(current)" "(ckout)"] from to
if { [regexp {[|]\s*(.*)} $::fossilcmd {} cmdList] } {
  set f [lindex $cmdList 0]
  if { [regexp {([-][-]?from|-r)\s+(\S+)} [join $cmdList " "] {} {} from] } {
    set err [catch { exec $f info $from } info]
    if { !$err } {
      regexp {uuid:\s+(\S+)\s+(\S+)\s+(\S+)} $info {} from date time
      set from "\[[string range $from 0 9]\] $date $time"
    }
  }
  if { [regexp {([-][-]?to)\s+(\S+)} [join $cmdList " "] {} {} to] } {
    set err [catch { exec $f info $to } info]
    if { !$err } {
      regexp {uuid:\s+(\S+)\s+(\S+)\s+(\S+)} $info {} to date time
      set to "\[[string range $to 0 9]\] $date $time"
    }
  }
}
   
ttk::label .bb.from -text $from
ttk::label .bb.to -text $to
   
pack .bb.from -side left -padx "2 25"
pack .bb.quit .bb.invert -side left
if {$fossilcmd!=""} {pack .bb.save -side left}
pack .bb.files -side left
pack .bb.actions -side left
pack .bb.to -side left -padx "25 2"
grid rowconfigure . 1 -weight 1
grid columnconfigure . 1 -weight 1
grid columnconfigure . 4 -weight 1
grid .bb -row 0 -columnspan 7
eval grid [cols] -row 1 -sticky nsew
grid .sby -row 1 -column 5 -sticky ns
grid .sbxA -row 2 -columnspan 2 -sticky ew
grid .spacer -row 2 -column 2
grid .sbxB -row 2 -column 3 -columnspan 2 -sticky ew

.spacer config -height [winfo height .sbxA]
wm deiconify .
}
eval $prog
