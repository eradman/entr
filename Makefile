DESTDIR=${HOME}/local
CC = cc
CFLAGS = -g
CFLAGS+= -std=c99 -Wall

all: entr

test: entr_spec entr
	@echo "Running tests"
	@./entr_spec

debug: entr_spec
	gdb -q entr_spec

entr: entr.c
	${CC} ${CFLAGS} entr.c -o $@
	@chmod +x $@

entr_spec: entr_spec.c entr.c
	${CC} ${CFLAGS} -lpthread entr_spec.c -o $@
	@chmod +x $@

clean:
	rm -rf entr_one entr entr_spec *.o

install:
	@echo "Installing"
	@mkdir -p ${DESTDIR}/bin
	cp entr ${DESTDIR}/bin/
	@mkdir -p ${DESTDIR}/man1
	cp entr.1 ${DESTDIR}/man1/

uninstall:
	@echo "Uninstalling"
	rm ${DESTDIR}/bin/entr
	rm ${DESTDIR}/man1/dwm.1

