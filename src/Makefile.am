##############################################################################
#  Makefile.am  –  Trigger-analysis library + simple external-link test
##############################################################################

AUTOMAKE_OPTIONS = foreign

#---------------------------------------------------------------------------
# 1)  Library to build
#---------------------------------------------------------------------------

lib_LTLIBRARIES = \
    libTrigger.la                       # was libPDC.la

AM_LDFLAGS = \
  -L$(libdir) \
  -L$(OFFLINE_MAIN)/lib \
  -L$(OFFLINE_MAIN)/lib64

AM_CPPFLAGS = \
  -I$(includedir) \
  -I$(OFFLINE_MAIN)/include \
  -isystem$(ROOTSYS)/include

# public header(s) installed under $(includedir)
pkginclude_HEADERS = \
  JetTriggerPlotter.h

libTrigger_la_SOURCES = \
  JetTriggerPlotter.cc

libTrigger_la_LIBADD = \
  -lcalo_io            \
  -lphool              \
  -lcalotrigger        \
  -lmbd_io             \
  -lSubsysReco         \
  -lcentrality_io      \
  -lfun4all            \
  -lg4dst              \
  -lg4detectors_io     \
  -lphg4hit

#---------------------------------------------------------------------------
# 2)  Tiny “does it link?” executable (no-inst – not installed)
#---------------------------------------------------------------------------

noinst_PROGRAMS = \
  testexternals

testexternals_SOURCES = testexternals.C
testexternals_LDADD   = libTrigger.la   # was libPDC.la

# generate a throw-away main() if no source provided
testexternals.C:
	@echo "// *** generated stub – do not commit" >  $@
	@echo "int main() { return 0; }"          >> $@

#---------------------------------------------------------------------------
# 3)  ROOT dictionary helper (optional; keep if you rely on rootcint)
#---------------------------------------------------------------------------

%_Dict.cc: %.h %LinkDef.h
	rootcint -f $@ @CINTDEFS@ -c $(DEFAULT_INCLUDES) $(AM_CPPFLAGS) $^

#---------------------------------------------------------------------------
# 4)  Local clean rule
#---------------------------------------------------------------------------

clean-local:
	rm -f $(BUILT_SOURCES)

