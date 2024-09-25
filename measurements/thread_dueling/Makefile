# name of your application
APPLICATION = measure_thread_duel

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../../RIOT

# This defaults to build with round_robin using ztimer
RR ?= 1

USEMODULE += timex
USEMODULE += ztimer
USEMODULE += ztimer_usec
USEMODULE += csp

ifeq (1,$(RR))
  USEMODULE += sched_round_robin
endif

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 0

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

WCONVERSION ?= 0
ifeq ($(WCONVERSION),1)
  CFLAGS += -Wconversion
endif

CFLAGS += -std=gnu23

include $(RIOTBASE)/Makefile.include
