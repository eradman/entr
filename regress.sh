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
#

typeset -i tests=0
typeset -i assertions=0

function try { let tests+=1; this="$1"; }
trap 'printf "$0: exit code $? on line $LINENO\nFAIL: $this\n"; exit 1' ERR
function assert {
	let assertions+=1
	[[ "$1" = "$2" ]] && { echo -n "."; return; }
	printf "\nFAIL: $this\n'$1' != '$2'\n"; exit 1
}

function pause { sleep 0.2; }
function setup { touch $tmp/file{1,2,3}; sleep 0.1; }
tmp=$(mktemp -d /tmp/entr_regress.XXXX)


try "no arguments"
	./entr 2> /dev/null || code=$?
	assert $code 1

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

try "exec single shell command when three files change simultaneously"
	setup
	ls $tmp/file* | ./entr sh -c 'echo ping; sleep 0.3' > $tmp/exec.out &
	bgpid=$!
	pause

	echo 456 >> $tmp/file2
	echo 789 >> $tmp/file3
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/exec.out)" "ping"

try "exec an interactive utility when a file changes"
	setup
	ls $tmp/file* | ./entr sh -c 'tty | colrm 9; sleep 0.3' > $tmp/exec.out &
	bgpid=$!
	pause

	echo 456 >> $tmp/file2
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/exec.out)" "/dev/tty"

try "read each filename from a named pipe as they're modified"
	setup
	ls $tmp/file* | ./entr +$tmp/notify &
	bgpid=$!
	pause
	cat $tmp/notify > $tmp/namedpipe.out &
	pause

	echo 123 >> $tmp/file1
	echo 789 >> $tmp/file3
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/namedpipe.out | sed 's/.*\///')" "$(echo -e 'file1\nfile3')"

try "read each filename from a named pipe until a file is removed"
	setup
	ls $tmp/file* | ./entr +$tmp/notify 2> /dev/null || code=$? &
	bgpid=$!
	pause
	cat $tmp/notify > $tmp/namedpipe.out &
	pause

	echo 123 >> $tmp/file1
	rm $tmp/file2
	pause
	kill -INT $bgpid

	wait $bgpid
	assert "$(cat $tmp/namedpipe.out | sed 's/.*\///')" "$(echo -e 'file1')"
	assert $code 1

# cleanup
#rm -r $tmp

echo; echo "$tests tests and $assertions assertions PASSED"
exit 0
