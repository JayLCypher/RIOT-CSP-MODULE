# name of your application
APPLICATION = ipc_pingpong_csp

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../../RIOT
EXTERNAL_MODULE_DIRS ?= $(CURDIR)/../../modules

USEMODULE += csp

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

WCONVERSION ?= 0
ifeq ($(WCONVERSION),1)
  CFLAGS += -Wconversion
endif

CFLAGS += -std=gnu23

include $(RIOTBASE)/Makefile.include
