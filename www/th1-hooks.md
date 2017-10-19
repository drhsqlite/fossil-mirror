TH1 Hooks
=========

<big><big><big><font color="red">** DRAFT **</font></big></big></big>

The **TH1 hooks** feature allows <a href="th1.md">TH1</a> scripts to be
configured that can monitor, create, alter, or cancel the execution of
Fossil commands and web pages.

This feature requires the TH1 hooks feature to be enabled at compile-time.
Additionally, the "th1-hooks" repository setting must be enabled at runtime
in order to successfully make use of this feature.

TH1 Hook Related User-Defined Procedures
----------------------------------------

In order to activate TH1 hooks, one or more of the following user-defined
procedures should be defined, generally from within the "th1-setup" script
(setting) for a repository.  The following bullets summarize the available
TH1 hooks:

  *  command\_hook -- _Called before execution of a command._
  *  command\_notify -- _Called after execution of a command._
  *  webpage\_hook -- _Called before rendering of a web page._
  *  webpage\_notify -- _Called after rendering of a web page._

TH1 Hook Related Variables for Commands
---------------------------------------

  *  cmd\_name -- _Name of command being executed._
  *  cmd\_args -- _Current command line arguments._
  *  cmd\_flags -- _Bitmask of CMDFLAG values for the command being executed._

TH1 Hook Related Variables for Web Pages
----------------------------------------

  *  web\_name -- _Name of web page being rendered._
  *  web\_args -- _Current web page arguments._
  *  web\_flags -- _Bitmask of CMDFLAG values for the web page being rendered._

<a name="cmdReturnCodes"></a>TH1 Hook Related Return Codes for Commands
-----------------------------------------------------------------------

  *  TH\_OK -- _Command will be executed, notification will be executed._
  *  TH\_ERROR -- _Command will be skipped, notification will be skipped,
                  error message will be emitted._
  *  TH\_BREAK -- _Command will be skipped, notification will be skipped._
  *  TH\_RETURN -- _Command will be executed, notification will be skipped._
  *  TH\_CONTINUE -- _Command will be skipped, notification will be executed._

For commands that are not included in the Fossil binary, allowing their
execution will cause the standard "unknown command" error message to be
generated, which will typically exit the process.  Therefore, adding a
new command generally requires using the TH_CONTINUE return code.

<a name="webReturnCodes"></a>TH1 Hook Related Return Codes for Web Pages
------------------------------------------------------------------------

  *  TH\_OK -- _Web page will be rendered, notification will be executed._
  *  TH\_ERROR -- _Web page will be skipped, notification will be skipped,
                  error message will be emitted._
  *  TH\_BREAK -- _Web page will be skipped, notification will be skipped._
  *  TH\_RETURN -- _Web page will be rendered, notification will be skipped._
  *  TH\_CONTINUE -- _Web page will be skipped, notification will be executed._

For web pages that are not included in the Fossil binary, allowing their
rendering will cause the standard "Not Found" error message to be generated,
which will cause an HTTP 404 status code to be sent.  Therefore, adding a
new web page generally requires using the TH_CONTINUE return code.

<a name="triggerReturnCodes"></a>Triggering TH1 Return Codes from a Script
--------------------------------------------------------------------------

  *  TH\_OK -- _This is the default return code, nothing special needed._
  *  TH\_ERROR -- _Use the **error** command._
  *  TH\_BREAK -- _Use the **break** command._
  *  TH\_RETURN -- _Use the **return -code 5** command._
  *  TH\_CONTINUE -- _Use the **continue** command._

<a name="command_hook"></a>TH1 command_hook Procedure
-----------------------------------------------------

  *  command\_hook

This user-defined procedure, if present, is called just before the
execution of a command.  The name of the command being executed will
be stored in the "cmd\_name" global variable.  The arguments to the
command being executed will be stored in the "cmd\_args" global variable.
The associated CMDFLAG value will be stored in the "cmd\_flags" global
variable.  Before exiting, the procedure should trigger the return
<a href="#cmdReturnCodes">code</a> that corresponds to the desired action
to take next.

<a name="command_notify"></a>TH1 command_notify Procedure
---------------------------------------------------------

  *  command\_notify

This user-defined procedure, if present, is called just after the
execution of a command.  The name of the command being executed will
be stored in the "cmd\_name" global variable.  The arguments to the
command being executed will be stored in the "cmd\_args" global variable.
The associated CMDFLAG value will be stored in the "cmd\_flags" global
variable.  Before exiting, the procedure should trigger the return
<a href="#cmdReturnCodes">code</a> that corresponds to the desired action
to take next.

<a name="webpage_hook"></a>TH1 webpage_hook Procedure
-----------------------------------------------------

  *  webpage\_hook

This user-defined procedure, if present, is called just before the
rendering of a web page.  The name of the web page being rendered will
be stored in the "web\_name" global variable.  The arguments to the
web page being rendered will be stored in the "web\_args" global variable.
The associated CMDFLAG value will be stored in the "web\_flags" global
variable.  Before exiting, the procedure should trigger the return
<a href="#webReturnCodes">code</a> that corresponds to the desired action
to take next.

<a name="webpage_notify"></a>TH1 webpage_notify Procedure
---------------------------------------------------------

  *  webpage\_notify

This user-defined procedure, if present, is called just after the
rendering of a web page.  The name of the web page being rendered will
be stored in the "web\_name" global variable.  The arguments to the
web page being rendered will be stored in the "web\_args" global variable.
The associated CMDFLAG value will be stored in the "web\_flags" global
variable.  Before exiting, the procedure should trigger the return
<a href="#webReturnCodes">code</a> that corresponds to the desired action
to take next.
