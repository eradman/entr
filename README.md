Event Notify Test Runner
========================

**entr** - a utility for running arbitrary commands when files change. Uses
[kqueue(2)][kqueue_2] to avoid polling. Reads a list of files provided on STDIN
and runs the supplied command if any of them are modified.

Installation
------------

    make test
    PREFIX=$HOME/local/ make install

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

Releases History
----------------

1.1 Support for Mac OS added. _2012-04-12_
1.0 Tested on all the major BSDs, included in OpenBSD 5.2 ports under
`sysutils/entr`. _2012-04-12_


[kqueue_2]: http://www.openbsd.org/cgi-bin/man.cgi?query=kqueue&apropos=0&sektion=0&manpath=OpenBSD+Current&format=html

