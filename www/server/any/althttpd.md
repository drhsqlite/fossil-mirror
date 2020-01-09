# Serving via althttpd

[Althttpd][althttpd]
is a light-weight web server that has been used to implement the SQLite and
Fossil websites for well over a decade. Althttpd strives for simplicity,
security, ease of configuration, and low resource usage.

To set up a Fossil server as CGI on a host running the althttpd web
server, follow these steps.
<ol>
<li<p>Get the althttpd webserver running on the host.  This is easily
done by following the [althttpd documentation][althttpd].

<li><p>Create a CGI script for your Fossil respository.  The script will
be typically be two lines of code that look something like this:

~~~
    #!/usr/bin/fossil
    repository: /home/yourlogin/fossils/project.fossil
~~~

Modify the filenames to conform to your system, of course.  The
CGI script accepts [other options][cgi] besides the
repository:" line.  You can add in other options as you desire,
but the single "repository:" line is normally all that is needed
to get started.

<li><p>Make the CGI script executable.

<li><p>Verify that the fossil repository file and the directory that contains
the repository are both writable by whatever user the web server is
running and.
</ol>

And you are done.  Visit the URL that corresponds to the CGI script
you created to start using your Fossil server.

*[Return to the top-level Fossil server article.](../)*


[althttpd]:  https://sqlite.org/docsrc/doc/trunk/misc/althttpd.md
[cgi]:       ../../cgi.wiki
