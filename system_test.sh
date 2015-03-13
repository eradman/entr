#!/bin/sh
#
# Copyright (c) 2012 Eric Radman <ericshane@eradman.com>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

# test runner

trap 'printf "$0: exit code $? on line $LINENO\nFAIL: $this\n"; exit 1' ERR \
	2> /dev/null || exec bash $0 "$@"
typeset -i tests=0
function try { let tests+=1; this="$1"; }

function assert {
	[[ "$1" == "$2" ]] && { printf "."; return; }
	printf "\nFAIL: $this\n'$1' != '$2'\n"; exit 1
}
function skip { printf "s"; }

function zz { sleep 0.25; }
function setup { rm -f $tmp/*; touch $tmp/file{1,2}; zz; }
tmp=$(cd $(mktemp -dt entr_system_test.XXXXXXXXXX); pwd -P)

# rebuild

[ -f entr ] || {
	./configure
	make clean
	make
}

# required utilities

utils="hg vim"
for util in $utils; do
	p=$(which $util 2> /dev/null) || {
		echo "ERROR: could not locate the '$util' utility" >&2
		echo "System tests depend on the following: $utils" >&2
		exit 1
	}
done

# tests

try "no arguments"
	./entr 2> /dev/null || code=$?
	assert $code 1

try "reload and clear options with no utility to run"
	./entr -r -c 2> /dev/null || code=$?
	assert $code 1

try "empty input"
	echo "" | ./entr echo 2> /dev/null || code=$?
	assert $code 1

try "no regular files provided as input"
	mkdir $tmp/dir1
	ls $tmp | ./entr echo 2> /dev/null || code=$?
	rmdir $tmp/dir1
	assert $code 1

try "exec single shell utility and exit when a file is added to an implicit watch path"
	setup
	ls $tmp/file* | ./entr -dp sh -c 'echo ping' >$tmp/exec.out 2>$tmp/exec.err \
	    || true &
	bgpid=$! ; zz
	touch $tmp/newfile
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "ping"
	assert "$(cat $tmp/exec.err)" "entr: directory altered"

try "exec single shell utility and exit when a file is added to a specific path"
	setup
	ls -d $tmp | ./entr -dp sh -c 'echo ping' >$tmp/exec.out 2>$tmp/exec.err \
	    || true &
	bgpid=$! ; zz
	touch $tmp/newfile
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "ping"
	assert "$(cat $tmp/exec.err)" "entr: directory altered"

try "do nothing when a file not monitored is changed in directory watch mode"
	setup
	ls $tmp/file2 | ./entr -dp echo "changed" >$tmp/exec.out 2>$tmp/exec.err &
	bgpid=$! ; zz
	echo "123" > $tmp/file1
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" ""
	assert "$(cat $tmp/exec.err)" ""

try "exec utility when a file is written by Vim in directory watch mode"
	setup
	ls $tmp/file* | ./entr -dp echo "changed" >$tmp/exec.out 2>$tmp/exec.err &
	bgpid=$! ; zz
	vim -e -s -u NONE -N \
	    -c ":r!date" \
	    -c ":wq" $tmp/file1 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "changed"
	assert "$(cat $tmp/exec.err)" ""

try "exec utility when a file is opened for write and then closed"
	setup
	echo "---" > $tmp/file1
	ls $tmp/file* | ./entr -p echo "changed" > $tmp/exec.out &
	bgpid=$! ; zz
	: > $tmp/file1 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	if [ $(uname | egrep 'Darwin|FreeBSD|DragonFly') ]; then
		skip "NOTE_TRUNCATE not supported"
	else
		assert "$(cat $tmp/exec.out)" "changed"
	fi

try "exec single utility when an entire stash of files is reverted"
	setup
	cp /usr/include/*.h $tmp/
	cd $tmp
	hg init
	hg add *.h
	hg commit -u "regression" -m "initial checkin"
	for f in `ls *.h`; do
		chmod 644 $f
		echo "" >> $f
	done
	cd - > /dev/null ; zz
	ls $tmp/*.h | ./entr -p echo "changed" > $tmp/exec.out &
	bgpid=$! ; zz
	cd $tmp
	hg revert *.h
	cd - > /dev/null ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "changed"

try "exec utility when a file is written by Vim"
	setup
	ls $tmp/file* | ./entr -p echo "changed" > $tmp/exec.out &
	bgpid=$! ; zz
	vim -e -s -u NONE -N \
	    -c ":r!date" \
	    -c ":wq" $tmp/file1 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "changed"

try "exec shell utility when a file is written by Vim with 'backup'"
	setup
	ls $tmp/file* | ./entr -p echo "changed" > $tmp/exec.out &
	bgpid=$! ; zz
	vim -e -s -u NONE -N \
	    -c ":set backup" \
	    -c ":r!date" \
	    -c ":wq" $tmp/file1 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "changed"

try "exec shell utility when a file is written by Vim with 'nowritebackup'"
	setup
	ls $tmp/file* | ./entr -p echo "changed" > $tmp/exec.out &
	bgpid=$! ; zz
	vim -e -s -u NONE -N \
	    -c ":set nowritebackup" \
	    -c ":r!date" \
	    -c ":wq" $tmp/file1 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "changed"

try "restart a server when a file is modified"
	setup
	echo "started." > $tmp/file1
	ls $tmp/file2 | ./entr -r tail -f $tmp/file1 2> /dev/null > $tmp/exec.out &
	bgpid=$! ; zz
	assert "$(cat $tmp/exec.out)" "started."
	echo 456 >> $tmp/file2 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "$(printf 'started.\nstarted.')"

try "Ensure that all shell subprocesses are terminated in restart mode"
	setup
	cat <<-SCRIPT > $tmp/go.sh
	#!/bin/sh
	trap 'echo "caught signal"; exit' TERM
	echo "running"; sleep 10
	SCRIPT
	chmod +x $tmp/go.sh
	ls $tmp/file2 | ./entr -r sh -c "$tmp/go.sh" 2> /dev/null > $tmp/exec.out &
	bgpid=$! ; zz
	kill -INT $bgpid ; zz
	assert "$(cat $tmp/exec.out)" "$(printf 'running\ncaught signal')"

try "exit with no action when restart and dirwatch flags are combined"
	setup
	echo "started." > $tmp/file1
	ls $tmp/file* | ./entr -rd tail -f $tmp/file1 2> /dev/null > $tmp/exec.out &
	bgpid=$! ; zz
	assert "$(cat $tmp/exec.out)" "started."
	touch $tmp/newfile
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "$(printf 'started.')"

try "exec single shell utility when two files change simultaneously"
	setup
	ln $tmp/file1 $tmp/file3
	ls $tmp/file* | ./entr -p sh -c 'echo ping' > $tmp/exec.out &
	bgpid=$! ; zz
	echo 456 >> $tmp/file1 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "ping"

try "exec shell utility on startup and after a file is changed"
	setup
	ls $tmp/file* | ./entr sh -c 'printf ping' > $tmp/exec.out &
	bgpid=$! ; zz
	echo 456 >> $tmp/file1 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "pingping"

try "exec using subsituting changed file argument"
	setup
	ls $tmp/file* | ./entr -p cat /_ > $tmp/exec.out &
	bgpid=$! ; zz
	echo 123 >> $tmp/file1
	echo 456 >> $tmp/file2 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "123"

try "exec single shell utility using utility substitution"
	setup
	ls $tmp/file1 $tmp/file2 | ./entr -p file /_ > $tmp/exec.out &
	bgpid=$! ; zz
	echo 456 >> $tmp/file2; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "$tmp/file2: ASCII text"

try "watch and exec a program that is overwritten"
	setup
	touch $tmp/script; chmod 755 $tmp/script
	echo $tmp/script | ./entr -p $tmp/script $tmp/file1 > $tmp/exec.out &
	bgpid=$! ; zz
	cat > $tmp/script <<-EOF
	#!/bin/sh
	echo vroom
	EOF
	zz ; kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	assert "$(cat $tmp/exec.out)" "vroom"

try "exec an interactive utility when a file changes"
	setup
	ls $tmp/file* | ./entr -p sh -c 'tty | colrm 9' 2> /dev/null > $tmp/exec.out &
	bgpid=$! ; zz
	echo 456 >> $tmp/file2 ; zz
	kill -INT $bgpid
	wait $bgpid || assert "$?" "130"
	if ! tty > /dev/null ; then
		skip "A TTY is not available"
	else
		assert "$(cat $tmp/exec.out | tr '/pts' '/tty')" "/dev/tty"
	fi

# cleanup
rm -r $tmp

echo; echo "$tests tests PASSED"
exit 0
