.PHONY: FORCE
all: FORCE
	$(MAKE) -C .. bitgesell_qt test_bitgesell_qt
clean: FORCE
	$(MAKE) -C .. bitgesell_qt_clean test_bitgesell_qt_clean
check: FORCE
	$(MAKE) -C .. test_bitgesell_qt_check
bitgesell-qt bitgesell-qt.exe: FORCE
	$(MAKE) -C .. bitgesell_qt
apk: FORCE
	$(MAKE) -C .. BGL_qt_apk