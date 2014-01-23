#!/bin/ksh
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

typeset -i tests=0
typeset -i assertions=0

function try { let tests+=1; this="$1"; }
trap 'printf "$0: exit code $? on line $LINENO\nFAIL: $this\n"; exit 1' ERR

function assert {
	let assertions+=1
	[[ "$1" == "$2" ]] && { printf "."; return; }
	printf "\nFAIL: $this\n'$1' != '$2'\n"; exit 1
}

function pause { sleep 0.4; }
function setup { rm -f $tmp/*.out $tmp/file?; touch $tmp/file{1,2}; sleep 0.2; }
system_tmp=$(cd /tmp; pwd -P)
tmp=$(cd $(mktemp -d $system_tmp/entr_regress.XXXXXXXXXX); pwd -P)

# rebuild

[ -f entr ] || {
	./configure
	make clean
	make
}

# tests

try "no arguments"
	./entr 2> /dev/null || code=$?
	assert $code 2

try "reload and clear options with no command to run"
	./entr -r -c 2> /dev/null || code=$?
	assert $code 2

try "empty input"
	echo "" | ./entr echo 2> /dev/null || code=$?
	assert $code 2

try "no regular files provided as input"
	mkdir $tmp/dir1
	ls $tmp | ./entr echo 2> /dev/null || code=$?
	rmdir $tmp/dir1
	assert $code 1

try "watch and exec a program that is overwritten"
	setup
	cp $(which ls) $tmp/ls
	chmod 755 $tmp/ls
	echo $tmp/ls | ./entr $tmp/ls $tmp/file1 > $tmp/exec.out &
	bgpid=$!
	pause

	cp $(which ls) $tmp/ls
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/exec.out)" "$(ls $tmp/file1)"

try "exec single shell command when a file is renamed and replaced"
	setup
	ls $tmp/file* | ./entr file $tmp/file2 > $tmp/exec.out &
	bgpid=$!
	pause

	mv $tmp/file2 $tmp/file~
	pause
	touch $tmp/file2
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/exec.out)" "$tmp/file2: empty"

try "exec single shell command when a file is removed and replaced"
	setup
	ls $tmp/file* | ./entr file $tmp/file2 > $tmp/exec.out &
	bgpid=$!
	pause

	rm $tmp/file2
	pause
	touch $tmp/file2
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/exec.out)" "$tmp/file2: empty"

try "exec single shell command using command substitution"
	setup
	ls $tmp/file2 | ./entr file /_ > $tmp/exec.out &
	bgpid=$!
	pause

	rm $tmp/file2
	pause
	touch $tmp/file2
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/exec.out)" "$tmp/file2: empty"

try "restart a server when a file is modified"
	setup
	echo "started." > $tmp/file1
	ls $tmp/file2 | ./entr -r tail -f $tmp/file1 2> /dev/null > $tmp/exec.out &
	bgpid=$!
	pause
	assert "$(cat $tmp/exec.out)" "started."

	echo 456 >> $tmp/file2
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/exec.out)" "$(printf 'started.\nstarted.')"

try "exec single shell command when two files change simultaneously"
	setup
	ln $tmp/file1 $tmp/file3
	ls $tmp/file* | ./entr sh -c 'echo ping' > $tmp/exec.out &
	bgpid=$!
	pause

	echo 456 >> $tmp/file1
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/exec.out)" "ping"

try "read each filename from a named pipe as they're modified"
	setup
	ls $tmp/file* | ./entr +$tmp/notify &
	bgpid=$!
	pause
	cat $tmp/notify > $tmp/namedpipe.out &
	pause

	echo 123 >> $tmp/file1
	pause
	echo 789 >> $tmp/file2
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/namedpipe.out | sed 's/.*\///')" "$(printf 'file1\nfile2')"

try "read each filename from a named pipe until a file is removed"
	setup
	ls $tmp/file* | ./entr +$tmp/notify 2> /dev/null || code=$? &
	bgpid=$!
	pause
	cat $tmp/notify > $tmp/namedpipe.out &
	pause

	echo 123 >> $tmp/file1
	pause
	rm $tmp/file2
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/namedpipe.out | sed 's/.*\///')" "$(printf 'file1')"
	assert $code 1

tty > /dev/null && {
try "exec an interactive utility when a file changes"
	setup
	ls $tmp/file* | ./entr sh -c 'tty | colrm 9; sleep 0.3' > $tmp/exec.out &
	bgpid=$!
	pause

	echo 456 >> $tmp/file2
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/exec.out | tr '/pts' '/tty')" "/dev/tty"
}

# cleanup
rm -r $tmp

echo; echo "$tests tests and $assertions assertions PASSED"
exit 0
