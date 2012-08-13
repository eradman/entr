Event Notify Test Runner
========================

A utility for running arbitrary commands when files change. Uses
[kqueue(2)][kqueue_2] to avoid polling. `entr` responds to file system events by
executing command line arguments or by writing to a FIFO.

`entr` was written to provide an efective means of incorporating micro-tests
into the daily workflow on UNIX platforms.

Installation - BSD & Mac OS
---------------------------

    make test
    make install

Installation - Debian Linux
---------------------------

    apt-get install libkqueue-dev
    make test -f Makefile.linux
    make install

Installation - Red Hat Linux
----------------------------

Get & install [libkqueue][libkqueue]

    ./configure
    make && make rpm
    sudo rpm -ivh pkg/libkqueue-1.0-1.x86_64.rpm

Build entr

    make test -f Makefile.linux
    make install

Examples
--------

Recompile if header files change

    $ find. -name '*.h' | entr make

Run tests if any file in the current directory changes, only printing
STDERR

    $ echo '.' | entr sh -c './test.sh > /dev/null'

Convert any altered Markdown in the current directory to HTML using a
FIFO

    $ ls *.md | entr +notify &
    $ while read F
    > do
    >   markdown2html $F
    > done < notify

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

Editors such as VIM support many nifty file processing facilities, but command
line instructions still require a terminal. I frequently use VIM in scripting,
so `entr` always attempts to open a TTY before entering it's event loop. `xargs`
on BSD provides this functionality using the `-o` flag.

Related Projects
----------------

[guard][guard] - command line tool to easily handle events on file system
modifications  
[watchr][watchr] - Modern continuous testing (flexible alternative to Autotest)  

Releases History
----------------

1.6 Works with NFS mounts on Linux, no need for pthreads on BSD _2012-08-10_

1.5 Support interactive applications by opening a TTY _2012-07-29_

1.4 New regression tests and better Linux support _2012-05-22_

1.3 New FIFO mode and better support of Mac OS _2012-05-17_

1.2 Support for Linux via [libkqueue][libkqueue] _2012-04-26_

1.1 Support for Mac OS added. _2012-04-17_  

1.0 Tested on all the major BSDs, included in OpenBSD 5.2 ports under
`sysutils/entr`. _2012-04-12_  


[kqueue_2]: http://www.openbsd.org/cgi-bin/man.cgi?query=kqueue&apropos=0&sektion=0&manpath=OpenBSD+Current&format=html
[libkqueue]: http://mark.heily.com/book/export/html/52
[guard]: https://github.com/guard/guard
[watchr]: https://github.com/mynyml/watchr
