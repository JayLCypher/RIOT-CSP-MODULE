
# If no BOARD is found in the environment, use this default:
BOARD ?= native

.PHONY: all test clean

EXAMPLES := hello_world goroutines ipc_pingpong_csp packet_plexer thread_duel_csp workergroups

all: $(EXAMPLES)

hello_world: hello_world/
	make -C $@

goroutines: goroutines/
	make -C $@

ipc_pingpong_csp: ipc_pingpong_csp/
	make -C $@

packet_plexer: packet_plexer/
	make -C $@

thread_duel_csp: thread_duel_csp/
	make -C $@

workergroups: workergroups/
	make -C $@

clean: $(EXAMPLES)
	for example in $^ ; do \
		make -C $$example clean ; \
		rm -r $$example/bin ; \
	done
