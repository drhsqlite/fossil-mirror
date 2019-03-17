# How To Mirror A Fossil Repository On GitHub

Beginning with Fossil version 2.9, can mirror a Fossil-based
project on GitHub by following these steps:

<ol>
<li><p>Create an account on GitHub if you do not have one already.  Log
    into that account.

<li><p>Create a new project.  GitHub will ask you if you want to prepopulate
    your project with various things like a README file.  Answer "no" to
    everything.  You want a completely blank project.  GitHub will then
    supply you with a URL for your project that will look something
    like this:

<blockquote>
https://github.com/username/project.git
</blockquote>

<li><p>Back on your workstation, move to a checkout for your project and
    type:

<blockquote>
fossil git export GITREPO --autopush https://<font color="orange">username</font>:<font color="red">password</font>@github.com/username/project.git
</blockquote>

<p>   In place of GITREPO above, put in some directory name that is not
      part of your source tree.  The directory need not exist - Fossil will
      create it if necessary.  This GITREPO directory will become a Git
      repository that holds a translation of your Fossil repository.

<p>   The --autopush option tells Fossil that you want to push the Git
      translation up to GitHub every time it is updated.
      Note that you will need to augment the URL supplied by GitHub
      to insert your account <font color="orange">username</font>
      and <font color="red">password</font>.

<p>   You can also run the command above outside of any open checkout
      of your project by supplying the "-R repository" option.

<li><p>Get some coffee.  Depending on the size of your project, the
       command above can run for several minutes.

<li><p>And you are done!  Assuming everything worked, your project is now
    mirrored on GitHub.

<li><p>Whenever you update your project, simply run this command to update
    the mirror:

<blockquote>
fossil git export
</blockquote>


<p>   When updating your project, you do not need to reenter the GITREPO
      or the --autopush.  Fossil remembers those things.  The initial
      mirroring operation probably took several minutes (or tens of minutes)
      but a typical update will happen in a second or less.
</ol>

## Notes:

  *  The mirroring is one-way.  If you check in changes on GitHub, those
     changes will not be reabsorbed by Fossil.  There are technical problems
     that make a two-way mirror all but impossible.

  *  The "fossil git export" command creates subprocesses that run "git"
     commands.  So you must have Git installed on your machine for any
     of this to work.

  *  The Git repository will have an extra unmanaged top-level directory named
     "`.mirror_state`" that contains one or more files.  Those files are
     used to store the intermediate state of the translation so that
     subsequent invocations of "fossil git export" will know where you
     left of last time and what new content needs to be moved over into
     Git.  Be careful not to mess with the `.mirror_state` directory or
     any of its contents.  Do not manages those files.  Do not edit or
     delete them.

  *  Only check-ins and simple tags are translated to Git.  Git does not
     support wiki or tickets or unversioned content or any of the other
     features of Fossil that make it so convenient to use, so those other
     elements cannot be mirrored in Git.

  *  In Git, all tags must be unique.  If your Fossil repository has the
     same tag on two or more check-ins, the tag will only be preserved on
     the chronologically newest check-in.

## Example GitHub Mirrors

As of this writing (2019-03-16) the Fossil self-repository is mirrored
on GitHub at:

>
<https://github.com/drhsqlite/fossil-mirror>

In addition, an experimental SQLite mirror is available:

>
<https://github.com/drhsqlite/sqlite-mirror>

The Fossil source repositories for both of these mirrors are at
<https://www2.fossil-scm.org/fossil> and <https://www2.sqlite.org/src>,
respectively.  On that machine, there is a cron job that runs at
17 minutes after the hour, every hour that does:

>
    /usr/bin/fossil sync -u -R /home/www/fossil/fossil.fossil
    /usr/bin/fossil sync -R /home/www/fossil/sqlite.fossil
    /usr/bin/fossil git export -R /home/www/fossil/fossil.fossil
    /usr/bin/fossil git export -R /home/www/fossil/sqlite.fossil

The initial two "sync" commands pull in changes from the primary
Fossil repositores for Fossil and SQLite.  The last two lines
export the changes to Git and push the results up to GitHub.
