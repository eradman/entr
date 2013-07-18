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

If you're work environment spans many machines, you may find it useful to
build a static binary and install `entr` to your home directory

    ./configure
    CFLAGS="-static" make test
    PREFIX=$HOME/local make install

Installation - Mac OS/Homebrew
------------------------------

    brew tap mitchty/entr
    brew install entr

Installation - Ports
--------------------

Available in OpenBSD ports, FreeBSD ports, and pkgsrc under `sysutils/entr`.

Examples
--------

Recompile if source files change

    $ find. -name '*.c' | entr make

Launch and auto-reload a node.js server

    $ ls *.js | entr -r node index.js

Convert Markdown files to HTML using a FIFO. Only files that change will be
processed.

    $ ls *.md | entr +notify &
    $ while read F
    > do
    >   markdown2html $F
    > done < notify

Next Release: 2.2
-----------------

* Ignore directories and special files, using available file descriptors for
  regular files only
* Exit with error code 2 if usage is incorrect or if no regular files are
  provided as input
* Exit with informative error message if the list of regular files exceeds the
  hard limit for the user's login class
* More coverage in unit tests
* Don't use GCC-specific flags in Makefile
* Execute mode can run a file under watch by retrying if text file is busy
* On platforms that support O_EVTONLY (MacOS) watching files does prevent the
  and unmount of removable media

Releases History
----------------

2.1 Zero-dependency build on Linux using built-in compatibility layer _2013-07-01_

2.0 More portable build; runs on old architectures without C99 support
_2013-06-17_

1.9 New auto-reload option _2013-04-13_

1.8 Loosing a file under watch is always fatal _2012-12-05_

1.7 Successfully stat deleted files before running a command _2012-11-20_

1.6 Works with NFS mounts on Linux, no need for pthreads on BSD _2012-08-10_

1.5 Support interactive applications by opening a TTY _2012-07-29_

1.4 New regression tests and better Linux support _2012-05-22_

1.3 New FIFO mode and better support of Mac OS _2012-05-17_

1.2 Support for Linux via libkqueue _2012-04-26_

1.1 Support for Mac OS added. _2012-04-17_  

1.0 Tested on all the major BSDs _2012-04-12_  

[kqueue_2]: http://www.openbsd.org/cgi-bin/man.cgi?query=kqueue&manpath=OpenBSD+Current&format=html
[inotify_7]: http://man.he.net/?section=all&topic=inotify
