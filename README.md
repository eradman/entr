Event Notify Test Runner
========================

A utility for running arbitrary commands when files change. Uses
[kqueue(2)][kqueue_2] or [inotify(7)][inotify_7] to avoid polling. `entr`
responds to file system events by executing command line arguments or by writing
to a FIFO.

`entr` was written to provide to make rapid feedback and automated testing
natural and completely ordinary.

Installation - BSD, Mac OS, and Linux
-------------------------------------

    ./configure
    make test
    make install

To see available build options run `./configure -h`

Installation - Mac OS/Homebrew
------------------------------

    brew install entr

Installation - Ports
--------------------

Available in OpenBSD ports, FreeBSD ports, and pkgsrc under `sysutils/entr`.

Installation - Debian
---------------------

    apt-get install entr

Examples from `man entr`
------------------------

Clear the screen and rebuild a project if source files change:

    $ find src/ | entr make

Launch and auto-reload a node.js server:

    $ ls *.js | entr -r node app.js

Run an SQL query:

    $ echo my.sql | entr psql -f /_

Rebuild project if a source file is modified or added to the src/
directory:

    $ while sleep 1; do ls src/*.rb | entr -d rake; done

Convert individual Markdown files to HTML if they're modified:

    $ ls *.md | entr +notify &
    $ while read F; do
    >    markdown2html $F
    > done < notify

News
----

A release history as well as features in the upcoming release are covered in the
[NEWS][NEWS] file.

License
-------

Source is under and ISC-style license. See the [LICENSE][LICENSE] file for more
detailed information on the license used for compatibility libraries.

[kqueue_2]: http://www.openbsd.org/cgi-bin/man.cgi?query=kqueue&manpath=OpenBSD+Current&format=html
[inotify_7]: http://man.he.net/?section=all&topic=inotify
[NEWS]: http://www.bitbucket.org/eradman/entr/src/default/NEWS
[LICENSE]: http://www.bitbucket.org/eradman/entr/src/default/LICENSE
