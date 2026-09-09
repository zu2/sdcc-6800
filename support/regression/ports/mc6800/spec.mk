# Regression test specification for the mc6800 target running with uCsim

# path to uCsim
ifdef SDCC_BIN_PATH
  UCMC6800C = $(SDCC_BIN_PATH)/ucsim_m6800$(EXEEXT)

  AS_MC6800C = $(SDCC_BIN_PATH)/sdas6800$(EXEEXT)
else
  ifdef UCSIM_DIR
    UCMC6800A = $(UCSIM_DIR)/src/sims/m6800.src/ucsim_m6800$(EXEEXT)
  else
    UCMC6800A = $(top_builddir)/sim/ucsim/src/sims/m6800.src/ucsim_m6800$(EXEEXT)
    UCMC6800B = $(top_builddir)/bin/ucsim_m6800$(EXEEXT)
  endif

  EMU = $(WINE) $(shell if [ -f $(UCMC6800A) ]; then echo $(UCMC6800A); else echo $(UCMC6800B); fi)

  AS = $(WINE) $(top_builddir)/bin/sdas6800$(EXEEXT)

ifndef CROSSCOMPILING
  SDCCFLAGS += --nostdinc -I$(top_srcdir)
  LINKFLAGS += --nostdlib -L$(top_builddir)/device/lib/build/mc6800
endif
endif

ifdef CROSSCOMPILING
  SDCCFLAGS += -I$(top_srcdir)
endif

SDCCFLAGS += -mmc6800 --less-pedantic --out-fmt-ihx
LINKFLAGS += mc6800.lib

EMU_PORT_FLAG = -t 6800

OBJEXT = .rel
BINEXT = .ihx

# otherwise `make` deletes testfwk.rel and `make -j` will fail
.PRECIOUS: $(PORT_CASES_DIR)/%$(OBJEXT)

# Required extras
EXTRAS = $(PORT_CASES_DIR)/testfwk$(OBJEXT) $(PORT_CASES_DIR)/support$(OBJEXT)
include $(srcdir)/fwk/lib/spec.mk

%$(OBJEXT): %.asm
	$(AS) -plosgff $<

_clean:
