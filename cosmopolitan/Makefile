COSMOCC ?= ${HOME}/Downloads/cosmocc/bin/cosmocc

OBJDIR = objdir
OUTDIR = out

SOURCES = \
		../src/main.c \
		../src/parser.c \
		../src/helpers.c \
		../src/archiver.c \
		../src/algorithms/linear_congruential_gen.c \
		../src/data_structures/linked_list.c \
		../src/data_structures/hash_map.c \
		../src/data_structures/priority_heap.c

HEADERS = \
		../src/parser.h \
		../src/parser_internal.h \
		../src/helpers.h \
		../src/archiver.h \
		../src/algorithms/linear_congruential_gen.h \
		../src/data_structures/linked_list.h \
		../src/data_structures/hash_map.h \
		../src/data_structures/priority_heap.h \
		../src/platforms.h

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
