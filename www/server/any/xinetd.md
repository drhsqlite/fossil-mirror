# Serving via xinetd

Some operating systems have replaced the old Unix `inetd` daemon with
`xinetd`, which has a similar mission but with a very different
configuration file format.

The typical configuration file is either `/etc/xinetd.conf` or a subfile
in the `/etc/xinetd.d` directory. You need a configuration something
like this for Fossil:

    service http
    {
      port = 80
      socket_type = stream
      wait = no
      user = root
      server = /usr/bin/fossil
      server_args = http /home/fossil/repos/
    }

This example configures Fossil to serve multiple repositories under the
`/home/fossil/repos/` directory.

Beyond this, see the general commentary in our article on [the `inetd`
method](./inetd.md) as they also apply to service via `xinetd`.

*[Return to the top-level Fossil server article.](../)*
