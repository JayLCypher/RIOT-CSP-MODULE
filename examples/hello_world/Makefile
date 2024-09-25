# name of your application
APPLICATION = hello_world

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../../RIOT
EXTERNAL_MODULE_DIRS ?= $(CURDIR)/../../modules

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

USEMODULE += csp
USEMODULE := $(filter-out core_msg, $(USEMODULE))

WCONVERSION ?= 0
ifeq ($(WCONVERSION),1)
  CFLAGS += -Wconversion
endif

STD ?= gnu23
CFLAGS += -std=$(STD)

include $(RIOTBASE)/Makefile.include
