EE_BIN = input_port_probe.elf
EE_OBJS = input_port_probe.o
EE_LIBS = -lpad -lc
EE_INCS = -I../../src
EE_WARNFLAGS = -Wall -Wextra -Werror
HOST_CC ?= cc

.PHONY: all check-host clean-local
all: $(EE_BIN)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

check-host:
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -I../../src -I. port_projection_test.c -o port_projection_test
	./port_projection_test

clean-local:
	rm -f input_port_probe.o input_port_probe.elf port_projection_test
