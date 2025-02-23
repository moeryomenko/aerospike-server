# Aerospike Server
# Makefile
#
# Main Build Targets:
#
#   make {all|server} - Build the Aerospike Server.
#   make clean        - Remove build products, excluding built packages.
#   make cleanpkg     - Remove built packages.
#   make cleanall     - Remove all build products, including built packages.
#   make cleangit     - Remove all files untracked by Git.  (Use with caution!)
#   make strip        - Build stripped versions of the server executables.
#
# Packaging Targets:
#
#   make deb     - Package server for Debian / Ubuntu platforms as a ".deb" file.
#   make rpm     - Package server for the Red Hat Package Manager (RPM.)
#   make tar     - Package server as a compressed tarball for every Linux platform.
#   make source  - Package the server source code as a compressed "tar" archive.
#
# Building a distribution release is a two step process:
#
#   1). The initial "make" builds the server itself.
#
#   2). The second step packages up the server using "make" with one of the following targets:
#
#       rpm:  Suitable for building and installing on Red Hat-derived systems.
#       deb:  Suitable for building and installing on Debian-derived systems.
#       tar:  Makes an "Every Linux" distribution, packaged as a compressed "tar" archive.
#
# Targets for running the Aerospike Server in the source tree:
#
#   make init    - Initialize the server run-time directories.
#   make start   - Start the server.
#   make stop    - Stop the server.
#

OS = $(shell build/os_version)
UNAME=$(shell uname)

# Common variable definitions:
include make_in/Makefile.vars

.PHONY: all server
all server: aslibs
	$(MAKE) -C as

.PHONY: lib
lib: aslibs
	$(MAKE) -C as $@ STATIC_LIB=1

.PHONY: aslibs
aslibs: targetdirs version $(JANSSON)/Makefile $(JEMALLOC)/Makefile $(LUAJIT)/src/luaconf.h
ifeq ($(USE_LUAJIT),1)
	$(MAKE) -C $(LUAJIT) Q= TARGET_SONAME=libluajit.so CCDEBUG=-g
endif
	$(MAKE) -C $(JEMALLOC)
	$(MAKE) -C $(JANSSON)
ifeq ($(ARCH), aarch64)
	$(MAKE) -C $(TSO)
endif
	$(MAKE) -C $(COMMON) CF=$(CF) EXT_CFLAGS="$(EXT_CFLAGS)" OS=$(UNAME)
	$(MAKE) -C $(CF)
	$(MAKE) -C $(MOD_LUA) CF=$(CF) COMMON=$(COMMON) EXT_CFLAGS="$(EXT_CFLAGS)" USE_LUAJIT=$(USE_LUAJIT) LUAJIT=$(LUAJIT) TARGET_SERVER=1 OS=$(UNAME)
	$(MAKE) -C $(S2)

.PHONY: targetdirs
targetdirs:
	mkdir -p $(GEN_DIR) $(LIBRARY_DIR) $(BIN_DIR)
	mkdir -p $(OBJECT_DIR)/base $(OBJECT_DIR)/fabric \
		$(OBJECT_DIR)/geospatial $(OBJECT_DIR)/query \
		$(OBJECT_DIR)/sindex $(OBJECT_DIR)/storage \
		$(OBJECT_DIR)/transaction $(OBJECT_DIR)/xdr

strip:	server
	$(MAKE) -C as strip

.PHONY: init start stop
init:
	@echo "Creating and initializing working directories..."
	mkdir -p run/log run/work/smd run/work/usr/udf/lua

start:
	@echo "Running the Aerospike Server locally..."
	@PIDFILE=run/asd.pid ; if [ -f $$PIDFILE ]; then echo "Aerospike already running?  Please do \"make stop\" first."; exit -1; fi
	@nohup ./modules/telemetry/telemetry.py as/etc/telemetry_dev.conf > /dev/null 2>&1 &
	$(BIN_DIR)/asd --config-file as/etc/aerospike_dev.conf

stop:
	@echo "Stopping the local Aerospike Server..."
	@PIDFILE=run/asd.pid ; if [ -f $$PIDFILE ]; then kill `cat $$PIDFILE`; rm $$PIDFILE; fi
	@PID=`pgrep telemetry.py | grep -v grep`; if [ -n "$$PID" ]; then kill $$PID; fi

.PHONY: clean
clean:	cleanbasic cleanmodules cleandist

.PHONY: cleanbasic
cleanbasic:
	$(RM) $(VERSION_SRC) $(VERSION_OBJ)
	$(RM) -rf $(TARGET_DIR)
	$(MAKE) -C $(TSO) clean

.PHONY: cleanmodules
cleanmodules:
	$(MAKE) -C $(COMMON) clean
	if [ -e "$(JANSSON)/Makefile" ]; then \
		$(MAKE) -C $(JANSSON) clean; \
		$(MAKE) -C $(JANSSON) distclean; \
	fi
	if [ -e "$(JEMALLOC)/Makefile" ]; then \
		$(MAKE) -C $(JEMALLOC) clean; \
		$(MAKE) -C $(JEMALLOC) distclean; \
	fi
ifeq ($(ARCH), x86_64)
	if [ -e "$(LUAJIT)/Makefile" ]; then \
		if [ ! -e "$(LUAJIT)/src/luaconf.h" ]; then \
			ln -s "$(LUAJIT)/src/luaconf.h.orig" "$(LUAJIT)/src/luaconf.h"; \
		fi; \
		$(MAKE) -C $(LUAJIT) clean; \
	fi
endif
	$(MAKE) -C $(MOD_LUA) COMMON=$(COMMON) USE_LUAJIT=$(USE_LUAJIT) LUAJIT=$(LUAJIT) clean
	$(MAKE) -C $(S2) clean

.PHONY: cleandist
cleandist:
	$(RM) -r pkg/dist/*

.PHONY: cleanall
cleanall: clean cleanpkg

.PHONY: cleanpkg
cleanpkg:
	$(RM) pkg/packages/*

GIT_CLEAN = git clean -fdx

.PHONY: cleangit
cleangit:
	cd $(COMMON); $(GIT_CLEAN)
	cd $(JANSSON); $(GIT_CLEAN)
	cd $(JEMALLOC); $(GIT_CLEAN)
	cd $(LUAJIT); $(GIT_CLEAN)
	cd $(MOD_LUA); $(GIT_CLEAN)
	cd $(S2); $(GIT_CLEAN)
	$(GIT_CLEAN)

.PHONY: rpm deb tar
rpm deb tar src:
	$(MAKE) -C pkg/$@ EDITION=$(EDITION)

$(VERSION_SRC):	targetdirs
	build/gen_version $(EDITION) $(OS) > $(VERSION_SRC)

$(VERSION_OBJ):	$(VERSION_SRC)
	$(CC) -o $@ -c $<

.PHONY: version
version:	$(VERSION_OBJ)

$(JANSSON)/configure:
	cd $(JANSSON) && autoreconf -i

$(JANSSON)/Makefile: $(JANSSON)/configure
	cd $(JANSSON) && ./configure $(JANSSON_CONFIG_OPT)

$(JEMALLOC)/configure:
	cd $(JEMALLOC) && autoconf

$(JEMALLOC)/Makefile: $(JEMALLOC)/configure
	cd $(JEMALLOC) && ./configure $(JEM_CONFIG_OPT)

$(LUAJIT)/src/luaconf.h: $(LUAJIT)/src/luaconf.h.orig
	ln -s $(notdir $<) $@

.PHONY: source
source: src

tags etags:
	etags `find as cf modules $(EEREPO) -name "*.[ch]" -o -name "*.cc" | egrep -v '(target/Linux|m4)'` `find /usr/include -name "*.h"`

# Common target definitions:
ifneq ($(EEREPO),)
  include $(EEREPO)/make_in/Makefile.targets
endif
