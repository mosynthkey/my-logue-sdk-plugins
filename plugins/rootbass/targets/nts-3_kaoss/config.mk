##############################################################################
# Configuration for Makefile
#

PROJECT := rootbass
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

# Prefer float_math; keep empty unless a true libm call sneaks in.
ULIBS =

##############################################################################
# Macros
#

UDEFS =
