COSMOCC ?= ${HOME}/Downloads/cosmocc/bin/cosmocc

OBJDIR = objdir
OUTDIR = out

SOURCES = \
		../src/main.c \
		../src/parser.c \
		../src/data_structures/linked_list.c

HEADERS = \
		../src/parser.h \
		../src/data_structures/linked_list.h

OBJECTS = $(addprefix ${OBJDIR}/,$(subst ..,PREVDIR,$(patsubst %.c,%.c.o,${SOURCES})))

all: actually_portable_simplearchiver

actually_portable_simplearchiver: ${OBJECTS}
	@mkdir -p "${OUTDIR}"
	${COSMOCC} -o "${OUTDIR}/actually_portable_simplearchiver" $^

.PHONY: clean

clean:
	rm -rf "${OUTDIR}"
	rm -rf "${OBJDIR}"

.SECONDEXPANSION:

${OBJDIR}/%.c.o: $$(subst PREVDIR,..,%.c) ${HEADERS}
	@mkdir -p "$(dir $@)"
	${COSMOCC} -c ${OTHER_FLAGS} -o $@ $<
