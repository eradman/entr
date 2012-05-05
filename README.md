Event Notify Test Runner
========================

***entr*** - a utility for running arbitrary commands when files change. Uses
[kqueue(2)][kqueue_2] to avoid polling. Reads a list of files provided on STDIN
and runs the supplied command if any of them are modified.

Installation - BSD/Mac OS
-------------------------

    make test
    PREFIX=$HOME/local make install

Installation - Debian Linux
---------------------------

Install libkqueue

    aptget libkqueue

Build entr

    CFLAGS="-D_GNU_SOURCE" LDFLAGS="-lkqueue -lpthread" make test 
    PREFIX=$HOME/local make install

Installation - Red Hat Linux
----------------------------

Get & install [libkqueue][libkqueue]

    tar -zxvf libkqueue-1.0.5.tar.gz
    cd libkqueue-1.0.5
    ./configure
    make
    make rpm
    sudo rpm -ivh pkg/libkqueue-1.0-1.x86_64.rpm
    sudo ln -s /usr/include/kqueue/sys/event.h /usr/include/sys/event.h

Build entr

    CFLAGS="-D_GNU_SOURCE" LDFLAGS="-lkqueue -lpthread" make test 
    PREFIX=$HOME/local make install

Examples
--------

Run `make test` when a source file changes:

    ls *.[hc] | entr make test


To watch for changes in any Python file and run some tests:

    find . -name *.py | entr ./test.sh

Supported Platforms
-------------------

* OpenBSD 5.0
* FreeBSD 9.0
* NetBSD 5.1
* DragonFly 3.0
* Mac OS 10.6
* RHEL 5.7

Implementation Notes
--------------------

It's not uncommon for version control software to update a large set of files
when they're submitted, but we don't want to run the utility one for each file
that is modified. To combat this `entr` ignores events until the subprocess
ends.

Some applications attempt to make atomic writes by writing a new file and then
deleting the original. `entr` deals with this by closing the old file descriptor
and reopening it using the same pathname. Since there may be a delay while the
new file is renamed a retry loop is employed.

Related Projects
----------------

[guard][guard] - command line tool to easily handle events on file system
modifications  
[watchr][watchr] - Modern continuous testing (flexible alternative to Autotest)  

Releases History
----------------

1.2 Support for Linux via [libkqueue][libkqueue] _2012-04-26_

1.1 Support for Mac OS added. _2012-04-17_  

1.0 Tested on all the major BSDs, included in OpenBSD 5.2 ports under
`sysutils/entr`. _2012-04-12_  


[kqueue_2]: http://www.openbsd.org/cgi-bin/man.cgi?query=kqueue&apropos=0&sektion=0&manpath=OpenBSD+Current&format=html
[libkqueue]: http://mark.heily.com/book/export/html/52
[guard]: https://github.com/guard/guard
[watchr]: https://github.com/mynyml/watchr
