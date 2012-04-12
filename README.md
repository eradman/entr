Event Notify Test Runner
========================

**entr** - a utility for running arbitrary commands when files change. Uses
[kqueue(2)][kqueue_2] to avoid polling. Reads a list of files provided on STDIN
and runs the supplied command if any of them are modified.

Installation
------------

    make test
    PREFIX=$HOME/local/ make install

Usage
-----

Run make when a source file changes:

    ls *.[hc] | entr make test


To watch for changes in any Python file and run some tests:

    find . -name *.py | entr ./test.sh

Platforms
---------

* OpenBSD 5.0
* FreeBSD 9.0
* NetBSD 5.1
* DragonFly 3.0

Implementation Notes
--------------------

Some applications attempt to make atomic writes by writing a new file and then
deleting the original. ***entr*** deals with this by closing the old file
descriptor and reopening it using the same pathname. Since there may be a delay
while the new file is renamed a retry loop is employed.


[kqueue_2]: http://www.openbsd.org/cgi-bin/man.cgi?query=kqueue&apropos=0&sektion=0&manpath=OpenBSD+Current&format=html

