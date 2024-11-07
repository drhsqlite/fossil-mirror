# Standalone HTTP Server

The easiest way to set up a Fossil server is to use either the
[`server`](/help/server) or [`ui`](/help/ui) command:

*  **fossil server** _REPOSITORY_
*  **fossil ui** _REPOSITORY_

The _REPOSITORY_ argument is either the name of the repository file or a
directory containing many repositories named “`*.fossil`”.  Both of these
commands start a Fossil server, usually on TCP port 8080, though a
higher numbered port will be used instead if 8080 is already occupied.

You can access these using URLs of the form **http://localhost:8080/**,
or if _REPOSITORY_ is a directory, URLs of the form
**http://localhost:8080/**_repo_**/** where _repo_ is the base name of
the repository file without the “`.fossil`” suffix.

There are several key differences between “`ui`” and “`server`”:

*   “`ui`” always binds the server to the loopback IP address (127.0.0.1)
    so that it cannot serve to other machines.

*   Anyone who visits this URL is treated as the all-powerful Setup
    user, which is why the first difference exists.
  
*   “`ui`” launches a local web browser pointed at this URL.

You can omit the _REPOSITORY_ argument if you run one of the above
commands from within a Fossil checkout directory to serve that
repository:

    $ fossil ui          # or...
    $ fossil server

You can abbreviate Fossil sub-commands as long as they are unambiguous.
“`server`” can currently be as short as “`ser`”.

You can serve a directory containing multiple `*.fossil` files like so:

    $ fossil server --port 9000 --repolist /path/to/repo/dir

There is an [example script](/file/tools/fslsrv) in the Fossil
distribution that wraps `fossil server` to produce more complicated
effects. Feel free to take it, study it, and modify it to suit your
local needs.

See the [online documentation](/help/server) for more information on the
options and arguments you can give to these commands.

*[Return to the top-level Fossil server article.](../)*
