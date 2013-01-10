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

