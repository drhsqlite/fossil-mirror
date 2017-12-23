TH1 Scripts
===========

TH1 is a very small scripting language used to help generate web-page
content in Fossil.

Origins
-------

TH1 began as a minimalist re-implementation of the Tcl scripting language.
There was a need to test the SQLite library on Symbian phones, but at that
time all of the test cases for SQLite were written in Tcl and Tcl could not
be easily compiled on the SymbianOS.  So TH1 was developed as a cut-down
version of Tcl that would facilitate running the SQLite test scripts on
SymbianOS.

The testing of SQLite on SymbianOS was eventually accomplished by other
means.  But Fossil was first being designed at about the same time.
Early prototypes of Fossil were written in pure Tcl.  But as the development
shifted toward the use of C-code, the need arose to have a Tcl-like
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

The original Tcl language after when TH1 is modeled has a very rich
repertoire of commands.  TH1, as it is designed to be minimalist and
embedded has a greatly reduced command set.  The following bullets
summarize the commands available in TH1:

  *  array exists VARNAME
  *  array names VARNAME
  *  break
  *  catch SCRIPT ?VARIABLE?
  *  continue
  *  error ?STRING?
  *  expr EXPR
  *  for INIT-SCRIPT TEST-EXPR NEXT-SCRIPT BODY-SCRIPT
  *  if EXPR SCRIPT (elseif EXPR SCRIPT)* ?else SCRIPT?
  *  info commands
  *  info exists VARNAME
  *  info vars
  *  lindex LIST INDEX
  *  list ARG ...
  *  llength LIST
  *  lsearch LIST STRING
  *  proc NAME ARG-LIST BODY-SCRIPT
  *  rename OLD NEW
  *  return ?-code CODE? ?VALUE?
  *  set VARNAME VALUE
  *  string compare STR1 STR2
  *  string first NEEDLE HAYSTACK ?START-INDEX?
  *  string index STRING INDEX
  *  string is CLASS STRING
  *  string last NEEDLE HAYSTACK ?START-INDEX?
  *  string length STRING
  *  string range STRING FIRST LAST
  *  string repeat STRING COUNT
  *  unset VARNAME
  *  uplevel ?LEVEL? SCRIPT
  *  upvar ?FRAME? OTHERVAR MYVAR ?OTHERVAR MYVAR?

All of the above commands work as in the original Tcl.  Refer to the
<a href="https://www.tcl-lang.org/man/tcl/contents.htm">Tcl documentation</a>
for details.

Summary of Core TH1 Variables
-----------------------------

  *  tcl\_platform(engine) -- _This will always have the value "TH1"._
  *  tcl\_platform(platform) -- _This will have the value "windows" or "unix"._
  *  th\_stack\_trace -- _This will contain error stack information._

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
  *  dir
  *  enable\_output
  *  encode64
  *  getParameter
  *  glob\_match
  *  globalState
  *  hascap
  *  hasfeature
  *  html
  *  htmlize
  *  http
  *  httpize
  *  insertCsrf
  *  linecount
  *  markdown
  *  puts
  *  query
  *  randhex
  *  redirect
  *  regexp
  *  reinitialize
  *  render
  *  repository
  *  searchable
  *  setParameter
  *  setting
  *  stime
  *  styleHeader
  *  styleFooter
  *  tclEval
  *  tclExpr
  *  tclInvoke
  *  tclIsSafe
  *  tclMakeSafe
  *  tclReady
  *  trace
  *  unversioned content
  *  unversioned list
  *  utime
  *  verifyCsrf
  *  wiki

Each of the commands above is documented by a block comment above their
implementation in the th\_main.c or th\_tcl.c source files.

All commands starting with "tcl", with the exception of "tclReady",
require the Tcl integration subsystem be included at compile-time.
Additionally, the "tcl" repository setting must be enabled at runtime
in order to successfully make use of these commands.

<a name="anoncap"></a>TH1 anoncap Command
-----------------------------------------

  *  anoncap STRING...

Returns true if the anonymous user has all of the capabilities listed
in STRING.

<a name="anycap"></a>TH1 anycap Command
---------------------------------------

  *  anycap STRING

Returns true if the current user user has any one of the capabilities
listed in STRING.

<a name="artifact"></a>TH1 artifact Command
-------------------------------------------

  *  artifact ID ?FILENAME?

Attempts to locate the specified artifact and return its contents.  An
error is generated if the repository is not open or the artifact cannot
be found.

<a name="checkout"></a>TH1 checkout Command
-------------------------------------------

  *  checkout ?BOOLEAN?

Return the fully qualified directory name of the current checkout or an
empty string if it is not available.  Optionally, it will attempt to find
the current checkout, opening the configuration ("user") database and the
repository as necessary, if the boolean argument is non-zero.

<a name="combobox"></a>TH1 combobox Command
-------------------------------------------

  *  combobox NAME TEXT-LIST NUMLINES

Generates and emits an HTML combobox.  NAME is both the name of the
CGI parameter and the name of a variable that contains the currently
selected value.  TEXT-LIST is a list of possible values for the
combobox.  NUMLINES is 1 for a true combobox.  If NUMLINES is greater
than one then the display is a listbox with the number of lines given.

<a name="date"></a>TH1 date Command
-----------------------------------

  *  date ?-local?

Return a strings which is the current time and date.  If the -local
option is used, the date appears using localtime instead of UTC.

<a name="decorate"></a>TH1 decorate Command
-------------------------------------------

  *  decorate STRING

Renders STRING as wiki content; however, only links are handled.  No
other markup is processed.

<a name="dir"></a>TH1 dir Command
---------------------------------

  * dir CHECKIN ?GLOB? ?DETAILS?

Returns a list containing all files in CHECKIN. If GLOB is given only
the files matching the pattern GLOB within CHECKIN will be returned.
If DETAILS is non-zero, the result will be a list-of-lists, with each
element containing at least three elements: the file name, the file
size (in bytes), and the file last modification time (relative to the
time zone configured for the repository).

<a name="enable_output"></a>TH1 enable\_output Command
------------------------------------------------------

  *  enable\_output BOOLEAN

Enable or disable sending output when the combobox, puts, or wiki
commands are used.

<a name="encode64"></a>TH1 encode64 Command
-------------------------------------------

  *  encode64 STRING

Encode the specified string using Base64 and return the result.

<a name="getParameter"></a>TH1 getParameter Command
---------------------------------------------------

  *  getParameter NAME ?DEFAULT?

Returns the value of the specified query parameter or the specified
default value when there is no matching query parameter.

<a name="glob_match"></a>TH1 glob\_match Command
------------------------------------------------

  *  glob\_match ?-one? ?--? patternList string

Checks the string against the specified glob pattern -OR- list of glob
patterns and returns non-zero if there is a match.

<a name="globalState"></a>TH1 globalState Command
-------------------------------------------------

  *  globalState NAME ?DEFAULT?

Returns a string containing the value of the specified global state
variable -OR- the specified default value.  The supported items are:

  1. **checkout** -- _Active local checkout directory, if any._
  1. **configuration** -- _Active configuration database file name, if any._
  1. **executable** -- _Fully qualified executable file name._
  1. **flags** -- _TH1 initialization flags._
  1. **log** -- _Error log file name, if any._
  1. **repository** -- _Active local repository file name, if any._
  1. **top** -- _Base path for the active server instance, if applicable._
  1. **user** -- _Active user name, if any._
  1. **vfs** -- _SQLite VFS in use, if overridden._

Attempts to query for unsupported global state variables will result
in a script error.  Additional global state variables may be exposed
in the future.

<a name="hascap"></a>TH1 hascap Command
---------------------------------------

  *  hascap STRING...

Returns true if the current user has all of the capabilities listed
in STRING.

<a name="hasfeature"></a>TH1 hasfeature Command
-----------------------------------------------

  *  hasfeature STRING

Returns true if the binary has the given compile-time feature enabled.
The possible features are:

  1. **ssl** -- _Support for the HTTPS transport._
  1. **legacyMvRm** -- _Support for legacy mv/rm command behavior._
  1. **execRelPaths** -- _Use relative paths with external diff/gdiff._
  1. **th1Docs** -- _Support for TH1 in embedded documentation._
  1. **th1Hooks** -- _Support for TH1 command and web page hooks._
  1. **tcl** -- _Support for Tcl integration._
  1. **useTclStubs** -- _Tcl stubs enabled in the Tcl headers._
  1. **tclStubs** -- _Uses Tcl stubs (i.e. linking with stubs library)._
  1. **tclPrivateStubs** -- _Uses Tcl private stubs (i.e. header-only)._
  1. **json** -- _Support for the JSON APIs._
  1. **markdown** -- _Support for Markdown documentation format._
  1. **unicodeCmdLine** -- _The command line arguments are Unicode._
  1. **dynamicBuild** -- _Dynamically linked to libraries._
  1. **see** -- _Uses the SQLite Encryption Extension._

Specifying an unknown feature will return a value of false, it will not
raise a script error.

<a name="html"></a>TH1 html Command
-----------------------------------

  *  html STRING

Outputs the STRING escaped for HTML.

<a name="htmlize"></a>TH1 htmlize Command
-----------------------------------------

  *  htmlize STRING

Escape all characters of STRING which have special meaning in HTML.
Returns the escaped string.

<a name="http"></a>TH1 http Command
-----------------------------------

  *  http ?-asynchronous? ?--? url ?payload?

Performs an HTTP or HTTPS request for the specified URL.  If a
payload is present, it will be interpreted as text/plain and
the POST method will be used; otherwise, the GET method will
be used.  Upon success, if the -asynchronous option is used, an
empty string is returned as the result; otherwise, the response
from the server is returned as the result.  Synchronous requests
are not currently implemented.

<a name="httpize"></a>TH1 httpize Command
-----------------------------------------

  *  httpize STRING

Escape all characters of STRING which have special meaning in URI
components.  Returns the escaped string.

<a name="insertCsrf"></a>TH1 insertCsrf Command
-----------------------------------------------

  *  insertCsrf

While rendering a form, call this command to add the Anti-CSRF token
as a hidden element of the form.

<a name="linecount"></a>TH1 linecount Command
---------------------------------------------

  *  linecount STRING MAX MIN

Returns one more than the number of \n characters in STRING.  But
never returns less than MIN or more than MAX.

<a name="markdown"></a>TH1 markdown Command
-------------------------------------------

  *  markdown STRING

Renders the input string as markdown.  The result is a two-element list.
The first element contains the body, rendered as HTML.  The second element
is the text-only title string.

<a name="puts"></a>TH1 puts Command
-----------------------------------

  *  puts STRING

Outputs the STRING unchanged.

<a name="query"></a>TH1 query Command
-------------------------------------

  *  query ?-nocomplain? SQL CODE

Runs the SQL query given by the SQL argument.  For each row in the result
set, run CODE.

In SQL, parameters such as $var are filled in using the value of variable
"var".  Result values are stored in variables with the column name prior
to each invocation of CODE.

<a name="randhex"></a>TH1 randhex Command
-----------------------------------------

  *  randhex N

Returns a string of N*2 random hexadecimal digits with N<50.  If N is
omitted, use a value of 10.

<a name="redirect"></a>TH1 redirect Command
-------------------------------------------

  *  redirect URL ?withMethod?

Issues an HTTP redirect to the specified URL and then exits the process.
By default, an HTTP status code of 302 is used.  If the optional withMethod
argument is present and non-zero, an HTTP status code of 307 is used, which
should force the user agent to preserve the original method for the request
(e.g. GET, POST) instead of (possibly) forcing the user agent to change the
method to GET.

<a name="regexp"></a>TH1 regexp Command
---------------------------------------

  *  regexp ?-nocase? ?--? exp string

Checks the string against the specified regular expression and returns
non-zero if it matches.  If the regular expression is invalid or cannot
be compiled, an error will be generated.

<a name="reinitialize"></a>TH1 reinitialize Command
---------------------------------------------------

  *  reinitialize ?FLAGS?

Reinitializes the TH1 interpreter using the specified flags.

<a name="render"></a>TH1 render Command
---------------------------------------

  *  render STRING

Renders the TH1 template and writes the results.

<a name="repository"></a>TH1 repository Command
-----------------------------------------------

  *  repository ?BOOLEAN?

Returns the fully qualified file name of the open repository or an empty
string if one is not currently open.  Optionally, it will attempt to open
the repository if the boolean argument is non-zero.

<a name="searchable"></a>TH1 searchable Command
-----------------------------------------------

  *  searchable STRING...

Return true if searching in any of the document classes identified
by STRING is enabled for the repository and user has the necessary
capabilities to perform the search.  The possible document classes
are:

  1. **c** -- _Check-in comments_
  1. **d** -- _Embedded documentation_
  1. **t** -- _Tickets_
  1. **w** -- _Wiki_

To be clear, only one of the document classes identified by each STRING
needs to be searchable in order for that argument to be true.  But all
arguments must be true for this routine to return true.  Hence, to see
if ALL document classes are searchable:

        if {[searchable c d t w]} {...}

But to see if ANY document class is searchable:

        if {[searchable cdtw]} {...}

This command is useful for enabling or disabling a "Search" entry on the
menu bar.

<a name="setParameter"></a>TH1 setParameter Command
---------------------------------------------------

  *  setParameter NAME VALUE

Sets the value of the specified query parameter.

<a name="setting"></a>TH1 setting Command
-----------------------------------------

  *  setting name

Gets and returns the value of the specified setting.

<a name="stime"></a>TH1 stime Command
-------------------------------------

  *  stime

Returns the number of microseconds of CPU time consumed by the current
process in system space.

<a name="styleHeader"></a>TH1 styleHeader Command
-------------------------------------------------

  *  styleHeader TITLE

Render the configured style header.

<a name="styleFooter"></a>TH1 styleFooter Command
-------------------------------------------------

  *  styleFooter

Render the configured style footer.

<a name="tclEval"></a>TH1 tclEval Command
-----------------------------------------

**This command requires the Tcl integration feature.**

  *  tclEval arg ?arg ...?

Evaluates the Tcl script and returns its result verbatim.  If a Tcl script
error is generated, it will be transformed into a TH1 script error.  The
Tcl interpreter will be created automatically if it has not been already.

<a name="tclExpr"></a>TH1 tclExpr Command
-----------------------------------------

**This command requires the Tcl integration feature.**

  *  tclExpr arg ?arg ...?

Evaluates the Tcl expression and returns its result verbatim.  If a Tcl
script error is generated, it will be transformed into a TH1 script
error.  The Tcl interpreter will be created automatically if it has not
been already.

<a name="tclInvoke"></a>TH1 tclInvoke Command
---------------------------------------------

**This command requires the Tcl integration feature.**

  *  tclInvoke command ?arg ...?

Invokes the Tcl command using the supplied arguments.  No additional
substitutions are performed on the arguments.  The Tcl interpreter
will be created automatically if it has not been already.

<a name="tclIsSafe"></a>TH1 tclIsSafe Command
---------------------------------------------

**This command requires the Tcl integration feature.**

  *  tclIsSafe

Returns non-zero if the Tcl interpreter is "safe".  The Tcl interpreter
will be created automatically if it has not been already.

<a name="tclMakeSafe"></a>TH1 tclMakeSafe Command
-------------------------------------------------

**This command requires the Tcl integration feature.**

  *  tclMakeSafe

Forces the Tcl interpreter into "safe" mode by removing all "unsafe"
commands and variables.  This operation cannot be undone.  The Tcl
interpreter will remain "safe" until the process terminates.  The Tcl
interpreter will be created automatically if it has not been already.

<a name="tclReady"></a>TH1 tclReady Command
-------------------------------------------

  *  tclReady

Returns true if the binary has the Tcl integration feature enabled and it
is currently available for use by TH1 scripts.

<a name="trace"></a>TH1 trace Command
-------------------------------------

  *  trace STRING

Generates a TH1 trace message if TH1 tracing is enabled.

<a name="unversioned_content"></a>TH1 unversioned content Command
-----------------------------------------------------------------

  *  unversioned content FILENAME

Attempts to locate the specified unversioned file and return its contents.
An error is generated if the repository is not open or the unversioned file
cannot be found.

<a name="unversioned_list"></a>TH1 unversioned list Command
-----------------------------------------------------------

  *  unversioned list

Returns a list of the names of all unversioned files held in the local
repository.  An error is generated if the repository is not open.

<a name="utime"></a>TH1 utime Command
-------------------------------------

  *  utime

Returns the number of microseconds of CPU time consumed by the current
process in user space.

<a name="verifyCsrf"></a>TH1 verifyCsrf Command
-----------------------------------------------

  *  verifyCsrf

Before using the results of a form, first call this command to verify
that this Anti-CSRF token is present and is valid.  If the Anti-CSRF token
is missing or is incorrect, that indicates a cross-site scripting attack.
If the event of an attack is detected, an error message is generated and
all further processing is aborted.

<a name="wiki"></a>TH1 wiki Command
-----------------------------------

  *  wiki STRING

Renders STRING as wiki content.

Tcl Integration Commands
------------------------

When the Tcl integration subsystem is enabled, several commands are added
to the Tcl interpreter.  They are used to allow Tcl scripts access to the
Fossil functionality provided via TH1.  The following is a summary of the
Tcl commands:

  *  th1Eval
  *  th1Expr

<a name="th1Eval"></a>Tcl th1Eval Command
-----------------------------------------

**This command requires the Tcl integration feature.**

  *  th1Eval arg

Evaluates the TH1 script and returns its result verbatim.  If a TH1 script
error is generated, it will be transformed into a Tcl script error.

<a name="th1Expr"></a>Tcl th1Expr Command
-----------------------------------------

**This command requires the Tcl integration feature.**

  *  th1Expr arg

Evaluates the TH1 expression and returns its result verbatim.  If a TH1
script error is generated, it will be transformed into a Tcl script error.
