# EXAMPLE MAKEFILE

# Set the name of your application:
APPLICATION = MY_CSP_APPLICATION

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# If BOARD is native, then we can optionally enable AUTO_EXIT=1 before make
ifeq ($(BOARD),native)
AUTO_EXIT ?= 0
ifeq ($(AUTO_EXIT),1)
	CFLAGS += -DNATIVE_AUTO_EXIT
endif

endif

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/RIOT
EXTERNAL_MODULE_DIR ?= $(CURDIR)/modules

PROJECT_BASE ?= $(CURDIR)

# If using out-of-tree modules, use this command
# EXTERNAL_MODULE_DIRS += $(PROJECT_BASE)/modules
USEMODULE += csp

# To use Round Robin scheduling, set RR to 1 or compile with RR=1 before make command.
RR ?= 0
ifeq (1,$(RR))
	USEMODULE += ztimer_usec
	USEMODULE += sched_round_robin
endif

# Debug tools
DEVELHELP ?= 1
DEBUG     ?= 1

CSTD      ?= -std=gnu23

ifeq ($(CSTD), 1)
	CFLAGS += $(CSTD)
endif

WCONVERSION ?= 0
ifeq ($(WCONVERSION),1)
  CFLAGS += -Wconversion
endif

include $(RIOTBASE)/Makefile.include

