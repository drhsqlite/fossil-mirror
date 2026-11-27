#
# Carry one round trip of a sync for scenario.test, and lose or damage
# it if told to.  Fossil runs this script as its --transport-command:
#
#     tclsh scenario-wire.tcl URL REQUESTFILE REPLYFILE
#
# SCN_EXE is the fossil executable and SCN_REPO the repository to talk
# to.  SCN_FAULT is 1 to lose the request, 2 to lose the reply after the
# other side has acted on the request, and 3 to deliver only the first
# half of the reply.  Any other value means no fault.
#
# The other side trusts the request as a local one, unless SCN_AUTH is
# set: then it checks the login card as a server on a network does.
# Fossil puts no login card on a request that it gives to a transport
# command.  If SCN_LOGIN is set, to USER:PASSWORD, this script adds the
# card that Fossil would add on a network; SCN_PROJECT is then the
# project code of the repository.
#
proc slurp {name} {
  set fd [open $name rb]
  set data [read $fd]
  close $fd
  return $data
}
proc spit {name data} {
  set fd [open $name wb]
  puts -nonewline $fd $data
  close $fd
}

lassign $argv url requestFile replyFile
set fault $env(SCN_FAULT)
if {$fault == 1} {
  spit $replyFile ""
  exit 0
}

# The SHA1 hash of a string, by way of "fossil sha1sum".
proc sha1 {text} {
  global env requestFile
  spit $requestFile.sha $text
  set sum [lindex [exec $env(SCN_EXE) sha1sum $requestFile.sha] 0]
  file delete $requestFile.sha
  return $sum
}

# Hand the request to "fossil http" on the other repository.
set request [slurp $requestFile]
if {[info exists env(SCN_LOGIN)]} {
  set colon [string first : $env(SCN_LOGIN)]
  set user [string range $env(SCN_LOGIN) 0 [expr {$colon-1}]]
  set password [string range $env(SCN_LOGIN) [expr {$colon+1}] end]
  set secret [sha1 $env(SCN_PROJECT)/$user/$password]
  set nonce [sha1 $request]
  set request "login $user $nonce [sha1 $nonce$secret]\n$request"
}
set head "POST /xfer HTTP/1.0\r\n"
append head "Content-Type: application/x-fossil-debug\r\n"
append head "Content-Length: [string length $request]\r\n\r\n"
spit $requestFile.req $head$request
set trust [expr {[info exists env(SCN_AUTH)] ? {} : {--localauth}}]
set failed [catch {
  exec $env(SCN_EXE) http --in $requestFile.req --out $requestFile.raw \
      --ipaddr 127.0.0.1 $env(SCN_REPO) {*}$trust 2>@1
}]
if {$failed} {
  file delete $requestFile.req $requestFile.raw
  exit 1
}

# The reply is what follows the first empty line.
set raw [slurp $requestFile.raw]
file delete $requestFile.req $requestFile.raw
set end [string first "\r\n\r\n" $raw]
set reply [expr {$end < 0 ? "" : [string range $raw [expr {$end+4}] end]}]
switch -- $fault {
  2 {set reply ""}
  3 {set reply [string range $reply 0 [expr {[string length $reply]/2 - 1}]]}
}
spit $replyFile $reply
