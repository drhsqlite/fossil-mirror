# The "fossil patch" command

The "[fossil patch](/help?cmd=patch)" command is designed to transfer
uncommitted changes from one check-out to another, including transfering
those changes to other machines.

For example, if you are working on a Windows desktop and you want to
test your changes on a Linux server before you commit, you can use the
"fossil patch push" command to make a copy of all your changes on the
remote Linux server:

   fossil patch push linuxserver:/path/to/checkout

In the previous line "linuxserver" is the name of the remote machine and
"/path/to/checkout" is an existing checkout directory for the same project
on the remote machine.

The "fossil patch push" command works by first creating a patch file,
then transfering that patch file to the remote machine using "ssh", then
applying the patch.  If you do not have ssh available, you can break these
steps apart as follows:

  1.  On the local machine: `fossil patch create mypatch.patch`
  2.  Move "mypatch.patch" to the remote machine.
  3.  On the remote machine: `fossil patch apply mypatch.patch`

Step 2 can be accomplished by a variety of means including
posting the mypatch.patch file on [chat](./chat.md) or sending
it as an email attachment.

## Setup

The "fossil patch push" and "fossil patch pull" commands will only work if you have
"ssh" available on the local machine and if "fossil" is on the default
PATH on the remote machine.

To check if Fossil is installed correctly on the remote, try a command
like this:

    ssh -T remote "fossil version"

If the command above shows a recent version of Fossil, then you should be
set to go.  If you get "fossil not found", or if the version shown is too
old, put a newer fossil executable on the default PATH.  The default PATH
can be shown using:

    ssh -T remote 'echo $PATH'

### Custom PATH Caveat

On Unix-like systems, the init script for the user's login shell
(e.g. `~/.profile` or `~/.bash_profile`) may be configured to *not do
anything* when running under a non-interactive shell. Thus a fossil
binary installed to a custom directory might not be found. To allow
the patch command to use a fossil binary installed in a directory
which is normally added to the PATH via the interactive shell's init
script, it may be useful to disable that check. For example,
Ubuntu-derived systems sometimes start their default `.bashrc` with
something like:

```
# If not running interactively, don't do anything:
[ -z "$PS1" ] && return
# Or:
case $- in
    *i*) ;;
    *) return;;
esac
```

Commenting out that check will allow the patch command to run, for
example, `~/bin/fossil` if `~/bin` is added to the PATH via the init
script. To disable that check *only* when the shell is *not* running
over an SSH connection, something like the following should suffice:

```
if [ -z "$SSH_CONNECTION" ]; then
  # ... the is-interactive check goes here ...
fi
```


## Implementation Details

The "fossil patch create" command records all of the local, uncommitted
changes in an SQLite database file.  If the argument to "fossil patch create"
is a filename, then the patch-file database is written into that file.
If the argument is "-" then the database is written on standard output.

The "fossil patch apply" command reads the patch-file database 
and applies it to the local check-out.  If a filename is given as an
argument, then the database is read from that file.  If the argument is "-"
then the database is read from standard input.

Hence the command:

    fossil patch push remote:projectA

Is equivalent to:

    fossil patch create - | ssh -T remote 'cd projectA;fossil patch apply -'

Likewise, a command like this:

    fossil patch pull remote:projB

could be entered like this:

    ssh -T remote 'cd projB;fossil patch create -' | fossil patch apply -

The "fossil patch view" command just opens the patch-file database and prints
a summary of its contents on standard output.
