TH1 Scripts
===========

TH1 is a very small scripting language used to help generate web-page
content in Fossil.

Origins
-------

TH1 began as a minimalist re-implementation of the TCL scripting language.
There was a need to test the SQLite library on Symbian phones, but at that
time all of the test cases for SQLite were written in Tcl and Tcl could not
be easily compiled on the SymbianOS.  So TH1 was developed as a cut-down
version of TCL that would facilitate running the SQLite test scripts on
SymbianOS.

The testing of SQLite on SymbianOS was eventually accomplished by other
means.  But Fossil was first being designed at about the same time.  
Early prototypes of Fossil were written in pure TCL.  But as the development
shifted toward the use of C-code, the need arose to have a TCL-like
scripting language to help with code generation.  TH1 was small and 
light-weight and used minimal resources and seemed ideally suited for the
task.

The name "TH1" stands "Test Harness 1", since that was its original purpose.

Overview
--------

TH1 is a string-processing language.  All values are strings.  Any numerical
operations are accomplished by converting from string to numeric, performing
the computation, then converting the result back into a string.  (This might
seem inefficient, but it is faster than people imagine, and numeric
computations do not come up very often for the kinds of work that TH1 does,
so it has never been a factor.)

A TH1 script consist of a sequence of commands.
Each command is terminated by the first (unescaped) newline or ";" character.
The text of the command (excluding the newline or semicolon terminator)
is broken into space-separated tokens.  The first token is the command
name and subsequent tokens are the arguments.  In this since, TH1 syntax
is similar to the familiar command-line shell syntax.

A token is any sequence of characters other than whitespace and semicolons.
Or, all text without double-quotes is a single token even if it includes
whitespace and semicolons.  Or, all text without nested {...} pairs is a
single token.

The nested {...} form of tokens is important because it allows TH1 commands
to have an appearance similar to C/C++.  It is important to remember, though,
that a TH1 script is really just a list of text commands, not a context-free
language with a grammar like C/C++.  This can be confusing to long-time
C/C++ programmers because TH1 does look a lot like C/C++.  But the semantics
of TH1 are closer to FORTH or Lisp than they are to C.

Consider the "if" command in TH1.

        if {$current eq "dev"} {
          puts "hello"
        } else {
          puts "world"
        }

The example above is a single command.  The first token, and the name
of the command, is "if".
The second token is '$current eq "dev"' - an expression.  (The outer {...}
are removed from each token by the command parser.)  The third token
is the 'puts "hello"', with its whitespace and newlines.  The fourth token
is "else".  And the fifth and last token is 'puts "world"'.

The "if" command word by evaluating its first argument (the second token)
as an expression, and if that expression is true, evaluating its
second argument (the third token) as a TH1 script.
If the expression is false and the third argument is "else" then
the fourth argument is evaluated as a TH1 expression.

So, you see, even though the example above spans five lines, it is really
just a single command.

Summary of Core TH1 Commands
----------------------------

The original TCL language after when TH1 is modeled has a very rich
repertoire of commands.  TH1, as it is designed to be minimalist and
embedded has a greatly reduced command set.  The following bullets
summarize the commands available in TH1:

  *  break
  *  catch SCRIPT ?VARIABLE?
  *  continue
  *  error ?STRING?
  *  expr EXPR
  *  for INIT-SCRIPT TEST-EXPR NEXT-SCRIPT BODY-SCRIPT
  *  if EXPR SCRIPT (elseif EXPR SCRIPT)* ?else SCRIPT?
  *  info exists VARNAME
  *  lindex LIST INDEX
  *  list ARG ...
  *  llength LIST
  *  proc NAME ARG-LIST BODY-SCRIPT
  *  rename OLD NEW
  *  return ?-code CODE? ?VALUE?
  *  set VARNAME VALUE
  *  string compare STR1 STR2
  *  string first NEEDLE HAYSTACK ?START-INDEX?
  *  string is CLASS STRING
  *  string last NEEDLE HAYSTACK ?START-INDEX?
  *  string length STRING
  *  string range STRING FIRST LAST
  *  string repeat STRING COUNT
  *  unset VARNAME
  *  uplevel ?LEVEL? SCRIPT
  *  upvar ?FRAME? OTHERVAR MYVAR ?OTHERVAR MYVAR?

All of the above commands works as in the original TCL.  Refer to the
TCL documentation for details.

TH1 Extended Commands
---------------------

There are many new commands added to TH1 and used to access the special
features of Fossil.  The following is a summary of the extended commands:

  *  anoncap
  *  anycap
  *  artifact
  *  checkout
  *  combobox
  *  date
  *  decorate
  *  enable_output
  *  getParameter
  *  globalState
  *  httpize
  *  hascap
  *  hasfeature
  *  html
  *  htmlize
  *  http
  *  linecount
  *  puts
  *  query
  *  randhex
  *  regexp
  *  reinitialize
  *  render
  *  repository
  *  searchable
  *  setParameter
  *  setting
  *  styleHeader
  *  styleFooter
  *  tclEval
  *  tclExpr
  *  tclInvoke
  *  tclReady
  *  trace
  *  stime
  *  utime
  *  wiki

Each of the commands above is documented by a block comment above their
implementation in the th_main.c or th_tcl.c source files.

All commands starting with "tcl", with the exception of "tclReady",
require the Tcl integration subsystem be included at compile-time.
Additionally, the "tcl" repository setting must be enabled at runtime
in order to successfully make use of these commands.

**To Do:** We would like to have a community volunteer go through and
copy the documentation for each of these commands (with appropriate
format changes and spelling and grammar corrections) into subsequent
sections of this document. It is suggested that the list of extension
commands be left intact - as a quick reference.  But it would be really
nice to also have the details of what each command does.
