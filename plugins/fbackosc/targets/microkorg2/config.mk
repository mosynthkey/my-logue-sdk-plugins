##############################################################################
# Configuration for Makefile
#

PROJECT := fbackosc
PROJECT_TYPE := osc

##############################################################################
# Sources
#

CSRC = header.c

CXXSRC = unit.cc

ASMSRC =

ASMXSRC =

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

ULIBS = -lm

##############################################################################
# Macros
#

UDEFS = -Dwt_saw_notes=osc_wt_saw_notes \
        -Dwt_saw_lut_f=osc_wt_saw_lut_f \
        -Dwt_sqr_lut_f=osc_wt_sqr_lut_f
