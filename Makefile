# Moon Technologies -- one VCV Rack plugin, three modules.
#
# Build:    make            (needs the Rack SDK; see docs/BUILDING.md)
# Install:  make install    (then restart Rack)
# Panels:   make panel      (regenerates all generated art and headers)
# Tests:    make test       (host compiler only, no Rack)

# --- where the Rack SDK lives -----------------------------------------------
# Set it explicitly for anything unusual:
#
#     make RACK_DIR=/path/to/Rack-SDK
#     export RACK_DIR=/path/to/Rack-SDK
#
# The default looks beside and above this repo, then in the two places the
# platform install guides put it, so a checkout next to an unpacked SDK builds
# with a bare `make` on all three operating systems.
RACK_DIR ?= $(firstword \
	$(wildcard ../Rack-SDK ../../Rack-SDK $(HOME)/Rack-SDK $(HOME)/src/Rack-SDK) \
	../Rack-SDK)

# Panels and tests need a Python 3 and a C++ compiler respectively, and nothing
# else -- no SDK, no Rack. Keeping them out of the SDK requirement is what lets
# CI regenerate the panels and run the tests on a machine that has neither.
# `clean` is not here: it comes from the SDK's plugin.mk, so without the SDK the
# clear "SDK not found" error is better than "no rule to make target".
SDK_FREE := panel test help $(filter panel-% preview-%,$(MAKECMDGOALS))
NEEDS_SDK := $(filter-out $(SDK_FREE),$(or $(MAKECMDGOALS),all))

ifneq ($(NEEDS_SDK),)
ifeq ($(wildcard $(RACK_DIR)/plugin.mk),)
$(error Rack SDK not found at "$(RACK_DIR)". Download it from \
https://vcvrack.com/downloads and either unpack it as ../Rack-SDK or pass \
RACK_DIR=/path/to/Rack-SDK. See docs/BUILDING.md)
endif
endif

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# NOTE: do not add -Isrc here. rack.hpp does `#include <plugin.hpp>`, which
# would then resolve to our own src/plugin.hpp. All internal includes are
# relative, which is why a module's C++ says "../plugin.hpp".

# Careful about linking to shared libraries, since you can't assume much about
# the user's environment and library search path. Static libraries are fine, but
# they should be added to this plugin's build system.
LDFLAGS +=

# Every module keeps its C++ in src/<Module>/. Three levels covers
# src/plugin.cpp, src/Retroactive/Retroactive.cpp and src/PatchAudit/ps/Api.cpp.
SOURCES += $(wildcard src/*.cpp src/*/*.cpp src/*/*/*.cpp)

# --- Syphon, the fast video path (macOS only) --------------------------------
# Transmittal publishes a GL texture to a compositor rather than encoding a
# stream, which is what makes it usable to perform with rather than only to
# capture with. Syphon is vendored in vendor/Syphon (3-clause BSD) and compiled
# straight into plugin.dylib -- there is no framework bundle to ship and nothing
# to code-sign, and Rack's own NANOVG_GL2 context is the legacy GL one Syphon's
# OpenGL server wants.
#
# Its sources are built by Xcode normally, which supplies two things the SDK's
# plain clang invocation does not: a prefix header (Cocoa, and the SYPHONLOG
# macro) and framework-style <Syphon/...> include resolution. -include and a
# -Ivendor that makes the directory's own name the framework name cover both. ARC
# is on for Syphon's own sources only; the rest of this plugin is not ARC.
# ARCH_OS comes from the SDK's arch.mk, which plugin.mk pulls in at the bottom
# of this file -- long after this line. So the platform test is uname's, the
# same one the preview target already uses.
ifeq ($(shell uname -s), Darwin)
SYPHON_DIR := vendor/Syphon
SYPHON_SRC := $(filter-out %Metal.m %MetalClient.m %MetalServer.m, $(wildcard $(SYPHON_DIR)/*.m)) $(wildcard $(SYPHON_DIR)/*.c)
SOURCES += $(SYPHON_SRC)
SOURCES += src/Transmittal/Syphon.mm
FLAGS += -Ivendor -I$(SYPHON_DIR) -include $(SYPHON_DIR)/Syphon_Prefix.pch
LDFLAGS += -framework Cocoa -framework OpenGL -framework IOSurface -framework CoreVideo
# Only Syphon's own translation units are ARC; the rest of this plugin is not,
# and the .mm wrapper owns nothing ARC would need to see. Target-specific, and
# deliberately not indented: a tab here would make it a recipe line.
# Syphon uses NSKeyedUnarchiver APIs introduced in 10.13, while the SDK's own
# flags say 10.9. Left alone that is not a warning, it is a crash on an old Mac
# -- so its translation units are told the truth about what they need. Rack 2
# itself does not run on anything older.
$(patsubst %, build/%.o, $(SYPHON_SRC)): FLAGS += -fobjc-arc -mmacosx-version-min=10.13
endif

# Added to the .vcvplugin package by `make dist`. The compiled library and
# plugin.json are added automatically.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

ifneq ($(wildcard $(RACK_DIR)/plugin.mk),)
include $(RACK_DIR)/plugin.mk
endif


# --- panels -----------------------------------------------------------------
# Panels are generated, never hand-edited. tools/panels/<Module>.py is the only
# file to edit for layout; it is the single source for res/*.svg,
# src/PanelTheme.hpp, src/<Module>/Panel.hpp and the previews.
# See panelkit/README.md.
#
#   make panel                 regenerate every panel
#   make panel-Retroactive     just that one
#   make preview-Retroactive   ... and open the browser mock
#   make vcv-preview           render every panel through VCV Rack itself,
#                              which is the only preview that cannot lie
#   make vcv-<Module>          render just that one -- what you want while you
#                              are working on it
PYTHON ?= python3
PANEL_SPECS := $(wildcard tools/panels/*.py)

.PHONY: panel vcv-preview test help

panel:
	@for spec in $(PANEL_SPECS); do \
		$(PYTHON) $$spec || exit 1; \
	done

panel-%:
	$(PYTHON) tools/panels/$*.py

preview-%: panel-%
	$(OPEN) tools/previews/$*.html

# Rack renders every model of the plugin in one pass, but each spec drives its
# own run, so this is one Rack launch per panel. Slow and thorough, on purpose:
# it is the last check before a panel is called done.
# Regenerate, THEN build, THEN render. The order matters and used to be wrong:
# `vcv-preview: all` built the plugin from whatever headers were on disk and only
# then re-ran the specs, so a spec change showed up in the artwork and not in the
# widget positions -- a panel whose jacks sat beside their own wells, blamed on
# the solver for as long as it took to notice.
vcv-preview: panel
	@$(MAKE) all
	@for spec in $(PANEL_SPECS); do \
		$(PYTHON) $$spec --vcv || exit 1; \
	done

# One panel, which is what you want while you are working on one. Rendering all
# twenty-four takes minutes and puts a Rack window up for each; `make vcv-Toll`
# regenerates that spec, rebuilds the plugin so the widget positions match the
# artwork, and renders only that panel.
vcv-%: panel-%
	@$(MAKE) all
	$(PYTHON) tools/panels/$*.py --vcv

# Derived from uname rather than from the SDK's ARCH_MAC, so that opening a
# preview does not require the SDK.
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
OPEN := open
else ifneq (,$(findstring MINGW,$(UNAME)))
OPEN := cmd //c start ""
else
OPEN := xdg-open
endif


# --- tests ------------------------------------------------------------------
# Host-compiled unit tests for the DSP and the movement statistic. They do not
# link against Rack, so they run anywhere a C++ compiler does -- including CI.
TEST_DIRS := $(wildcard tests/*/)

test:
	@for d in $(TEST_DIRS); do \
		echo "== $$d"; \
		$(MAKE) -C $$d test || exit 1; \
	done


help:
	@echo "Moon Technologies -- VCV Rack plugin"
	@echo
	@echo "  make                 build the plugin (needs the Rack SDK)"
	@echo "  make install         package it and copy into Rack, then RESTART Rack"
	@echo "  make dist            package only, into dist/"
	@echo "  make clean           remove build output"
	@echo
	@echo "  make panel           regenerate every panel from tools/panels/*.py"
	@echo "  make panel-<Module>  regenerate one"
	@echo "  make vcv-preview     render every panel through VCV Rack itself"
	@echo "  make vcv-NAME        render just one panel (e.g. make vcv-Toll)"
	@echo
	@echo "  make test            run the host-side unit tests"
	@echo
	@echo "See docs/BUILDING.md."
