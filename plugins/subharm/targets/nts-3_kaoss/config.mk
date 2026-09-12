##############################################################################
# Configuration for Makefile
#

PROJECT := subharm
PROJECT_TYPE := genericfx

##############################################################################
# Sources
#

UCSRC = header.c

UCXXSRC = unit.cc

UASMSRC =

UASMXSRC =

##############################################################################
# Include Paths
#

UINCDIR = ../../dsp ../../../common

##############################################################################
# Library Paths
#

ULIBDIR =

##############################################################################
# Libraries
#

# Prefer header-only float_math; keep -lm empty so NTS-3 does not leave UND libm.
ULIBS =

##############################################################################
# Macros
#

UDEFS =
