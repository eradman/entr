Development Notes
=================

MacOS: open
-----------

Files can be opened with O_EVTONLY to prevent from being marked as open or in
use.

http://developer.apple.com/library/ios/#documentation/Performance/Conceptual/FileSystem/Articles/TrackingChanges.html

NetBSD: seekfn
--------------

    static fpos_t seekfn() { return 0; }

entr_spec.c:54: error: incompatible types in return

Instead returning an uninitialized value

    static fpos_t seekfn() { fpos_t pos; return pos; }

NetBSD  kqueue
--------------

entr.c:145: warning: assignment makes integer from pointer without a cast

* NetBSD uses intptr_t instead of (void *) for udata

They're not interested in fixing this

http://mail-index.netbsd.org/tech-userlevel/2012/08/15/msg006629.html

NetBSD NOTE_RENAME
------------------

when VIM saves a file on OpenBSD NOTE_RENAME and NOTE_DELETE are set, on NetBSD
only NOTE_DELETE is set.

Linux VFS
---------

File changes on local disk will result in NOTE_WRITE and NOTE_EXTEND
File changes on NFS mounts will sometimes results in NOTE_RENAME and NOTE_LINK

OpenBSD STATE
-------------

If entr is compiled with pthreads the process state is 'poll' instead of
'kqread'. Not a problem, but the status reflects indirection.

http://lists.freebsd.org/pipermail/freebsd-hackers/2005-February/010216.html

Triggering Events
-----------------



OpenBSD EVFILT_USER
-------------------

OpenBSD does not make unit testing with kqueue possible without touching the
file system

http://svn0.us-west.freebsd.org/base/head/tools/regression/kqueue/user.c
http://www.opensource.apple.com/source/xnu/xnu-1456.1.26/tools/tests/xnu_quick_test/kqueue_tests.c
http://mark.heily.com/book/export/html/52

Other options...

"Is there a way to either: post a dummy message to the queue, or to cancel the
waiting kevent call so that I can reload my array when I decide to?"

You can create a dummy UDP socket and add a _disabled_ write filter. Whenever
you want to wake up your kqueue thread, just enable the write filter (then
disable it again when it wakes up). That's what I do.

http://julipedia.meroh.net/2004/10/example-of-kqueue.html

