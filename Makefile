CC = pcc
CFLAGS = -g
CFLAGS+= -std=c99 -Wall -Werror

all: entr entr_spec

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
