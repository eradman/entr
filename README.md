Event Notify Test Runner
========================

A utility for running arbitrary commands when files change. Uses [kqueue(2)] or
[inotify(7)] to avoid polling.  `entr` was written to make rapid feedback and
automated testing natural and completely ordinary.

Source Installation - BSD, Mac OS, and Linux
--------------------------------------------

    ./configure
    make test
    make install

To see available build options run `./configure -h`

Docker and Windows Subsystem for Linux
--------------------------------------

Incomplete inotify support on WSL and Docker for Mac can cause `entr`
to respond inconsistently. Since version 4.4, `entr` includes a workaround:
Set the environment variable `ENTR_INOTIFY_WORKAROUND`.

`entr` will confirm the workaround is enabled:

```
entr: broken inotify workaround enabled
```

Man Page Examples
-----------------

Rebuild a project if source files change, limiting output to the first 20 lines:

    $ find src/ | entr sh -c 'make | head -n 20'

Launch and auto-reload a node.js server:

    $ ls *.js | entr -r node app.js

Clear the screen and run a query after the SQL script is updated:

    $ echo my.sql | entr -cp psql -f /_

Rebuild project if a source file is modified or added to the src/ directory:

    $ while sleep 0.1; do ls src/*.rb | entr -d make; done

Auto-reload a web server, or terminate if the server exits

    $ ls * | entr -rz ./httpd

News
----

A release history as well as features in the upcoming release are covered in the
[NEWS](NEWS) file.

[kqueue(2)]: http://man.openbsd.org/kqueue.2
[inotify(7)]: http://man.he.net/?section=all&topic=inotify
