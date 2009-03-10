
SHELL = /bin/sh

#### Start of system configuration section. ####
top_srcdir = ..//../.
srcdir = $(top_srcdir)/ext/json
topdir = ../..
hdrdir = $(top_srcdir)/include
arch_hdrdir = $(extout)/include/$(arch)
VPATH = $(srcdir):$(arch_hdrdir)/ruby:$(hdrdir)/ruby
RUBYLIB = -
RUBYOPT = -r$(top_srcdir)/ext/purelib.rb
exec_prefix = $(prefix)
prefix = $(DESTDIR)/Library/Frameworks/MacRuby.framework/Versions/0.4/usr
docdir = $(datarootdir)/doc/$(PACKAGE)
sitehdrdir = $(rubyhdrdir)/site_ruby
rubyhdrdir = $(includedir)/ruby-$(MAJOR).$(MINOR).$(TEENY)
datarootdir = $(prefix)/share
vendorlibdir = $(vendordir)/$(ruby_version)
sysconfdir = $(prefix)/etc
bindir = $(exec_prefix)/bin
datadir = $(datarootdir)
sitedir = $(libdir)/ruby/site_ruby
sitearchdir = $(sitelibdir)/$(sitearch)
mandir = $(datarootdir)/man
dvidir = $(docdir)
localedir = $(datarootdir)/locale
localstatedir = $(prefix)/var
htmldir = $(docdir)
sharedstatedir = $(prefix)/com
oldincludedir = $(DESTDIR)/usr/include
sbindir = $(exec_prefix)/sbin
psdir = $(docdir)
libdir = $(exec_prefix)/lib
includedir = $(prefix)/include
pdfdir = $(docdir)
libexecdir = $(exec_prefix)/libexec
infodir = $(datarootdir)/info
rubylibdir = $(libdir)/ruby/$(ruby_version)
archdir = $(rubylibdir)/$(arch)
vendordir = $(prefix)/lib/ruby/vendor_ruby
vendorhdrdir = $(rubyhdrdir)/vendor_ruby
sitelibdir = $(sitedir)/$(ruby_version)
vendorarchdir = $(vendorlibdir)/$(sitearch)

CC = gcc
CXX = g++
LIBRUBY = $(LIBRUBY_SO)
LIBRUBY_A = lib$(RUBY_SO_NAME)-static.a
LIBRUBYARG_SHARED = -l$(RUBY_SO_NAME)
LIBRUBYARG_STATIC = -l$(RUBY_SO_NAME)-static -lpthread -ldl -lxml2 -lobjc -lffi -lauto -framework Foundation
OUTFLAG = -o 
COUTFLAG = -o 

RUBY_EXTCONF_H = extconf.h
cflags   = $(optflags) $(debugflags) $(warnflags)
optflags = -O2
debugflags = -g
warnflags = -Wall -Wno-parentheses
CFLAGS   = -fno-common -fno-common -pipe $(cflags) -arch i386 -arch x86_64
INCFLAGS = -I. -I$(arch_hdrdir) -I$(hdrdir) -I$(srcdir)
DEFS     = 
CPPFLAGS = -DRUBY_EXTCONF_H=\"$(RUBY_EXTCONF_H)\" $(cppflags)
CXXFLAGS = $(CFLAGS) 
ldflags  = 
dldflags = 
archflag = -arch i386 -arch x86_64
DLDFLAGS = $(ldflags) $(dldflags) $(archflag)
LDSHARED = $(CC) -dynamic -bundle -undefined suppress -flat_namespace -arch i386 -arch x86_64
LDSHAREDXX = $(CXX) -dynamic -bundle -undefined suppress -flat_namespace
AR = ar
EXEEXT = 

RUBY_INSTALL_NAME = macruby
RUBY_SO_NAME = macruby
arch = universal-darwin9.0
sitearch = universal-darwin9.0
ruby_version = 1.9.1
ruby = $(topdir)/miniruby -I'$(topdir)' -I'$(top_srcdir)/lib' -I'$(extout)/$(arch)' -I'$(extout)/common' -I./- -I'$(top_srcdir)/ext' -rpurelib.rb
RUBY = $(ruby)
RM = rm -f
RM_RF = $(RUBY) -run -e rm -- -rf
RMDIRS = $(RUBY) -run -e rmdir -- -p
MAKEDIRS = mkdir -p
INSTALL = /usr/bin/install -c
INSTALL_PROG = $(INSTALL) -m 0755
INSTALL_DATA = $(INSTALL) -m 644
COPY = cp

#### End of system configuration section. ####

preload = 

libpath = . $(topdir)
LIBPATH =  -L. -L$(topdir)
DEFFILE = 

CLEANFILES = mkmf.log
DISTCLEANFILES = 
DISTCLEANDIRS = 

extout = $(topdir)/.ext
extout_prefix = $(extout)$(target_prefix)/
target_prefix = 
LOCAL_LIBS = 
LIBS = $(LIBRUBYARG_SHARED)   
SRCS = 
OBJS = 
TARGET = 
DLLIB = 
EXTSTATIC = 
STATIC_LIB = 

BINDIR        = $(extout)/bin
RUBYCOMMONDIR = $(extout)/common
RUBYLIBDIR    = $(RUBYCOMMONDIR)$(target_prefix)
RUBYARCHDIR   = $(extout)/$(arch)$(target_prefix)
HDRDIR        = $(extout)/include/ruby$(target_prefix)
ARCHHDRDIR    = $(extout)/include/$(arch)/ruby$(target_prefix)

TARGET_SO     = $(RUBYARCHDIR)/$(DLLIB)
CLEANLIBS     = $(RUBYARCHDIR)/$(TARGET).bundle 
CLEANOBJS     = *.o  *.bak

all:    install
static: $(STATIC_LIB) install-rb

clean-rb-default::
clean-rb::
clean-so::
clean: clean-so clean-rb-default clean-rb
		@-$(RM) $(CLEANLIBS) $(CLEANOBJS) $(CLEANFILES)

distclean-rb-default::
distclean-rb::
distclean-so::
distclean: clean distclean-so distclean-rb-default distclean-rb
		@-$(RM) Makefile $(RUBY_EXTCONF_H) conftest.* mkmf.log
		@-$(RM) core ruby$(EXEEXT) *~ $(DISTCLEANFILES)
		@-$(RMDIRS) $(DISTCLEANDIRS)

realclean: distclean
install: install-so install-rb

install-so: Makefile
install-rb: pre-install-rb install-rb-default
install-rb-default: pre-install-rb-default
pre-install-rb: Makefile
pre-install-rb-default: Makefile
pre-install-rb-default: $(RUBYLIBDIR)/json/add
install-rb-default: $(RUBYLIBDIR)/json/add/core.rb
$(RUBYLIBDIR)/json/add/core.rb: $(srcdir)/lib/json/add/core.rb
	$(COPY) $(srcdir)/lib/json/add/core.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json/add/core.rb
install-rb-default: $(RUBYLIBDIR)/json/add/rails.rb
$(RUBYLIBDIR)/json/add/rails.rb: $(srcdir)/lib/json/add/rails.rb
	$(COPY) $(srcdir)/lib/json/add/rails.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json/add/rails.rb
pre-install-rb-default: $(RUBYLIBDIR)/json
install-rb-default: $(RUBYLIBDIR)/json/common.rb
$(RUBYLIBDIR)/json/common.rb: $(srcdir)/lib/json/common.rb
	$(COPY) $(srcdir)/lib/json/common.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json/common.rb
install-rb-default: $(RUBYLIBDIR)/json/editor.rb
$(RUBYLIBDIR)/json/editor.rb: $(srcdir)/lib/json/editor.rb
	$(COPY) $(srcdir)/lib/json/editor.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json/editor.rb
install-rb-default: $(RUBYLIBDIR)/json/ext.rb
$(RUBYLIBDIR)/json/ext.rb: $(srcdir)/lib/json/ext.rb
	$(COPY) $(srcdir)/lib/json/ext.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json/ext.rb
install-rb-default: $(RUBYLIBDIR)/json/pure.rb
$(RUBYLIBDIR)/json/pure.rb: $(srcdir)/lib/json/pure.rb
	$(COPY) $(srcdir)/lib/json/pure.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json/pure.rb
install-rb-default: $(RUBYLIBDIR)/json/version.rb
$(RUBYLIBDIR)/json/version.rb: $(srcdir)/lib/json/version.rb
	$(COPY) $(srcdir)/lib/json/version.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json/version.rb
pre-install-rb-default: $(RUBYLIBDIR)/json/pure
install-rb-default: $(RUBYLIBDIR)/json/pure/generator.rb
$(RUBYLIBDIR)/json/pure/generator.rb: $(srcdir)/lib/json/pure/generator.rb
	$(COPY) $(srcdir)/lib/json/pure/generator.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json/pure/generator.rb
install-rb-default: $(RUBYLIBDIR)/json/pure/parser.rb
$(RUBYLIBDIR)/json/pure/parser.rb: $(srcdir)/lib/json/pure/parser.rb
	$(COPY) $(srcdir)/lib/json/pure/parser.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json/pure/parser.rb
pre-install-rb-default: $(RUBYLIBDIR)
install-rb-default: $(RUBYLIBDIR)/json.rb
$(RUBYLIBDIR)/json.rb: $(srcdir)/lib/json.rb
	$(COPY) $(srcdir)/lib/json.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/json.rb
clean-rb-default::
	@-$(RMDIRS) $(RUBYLIBDIR)
	@-$(RMDIRS) $(RUBYLIBDIR)/json/pure
	@-$(RMDIRS) $(RUBYLIBDIR)/json
	@-$(RMDIRS) $(RUBYLIBDIR)/json/add
$(RUBYLIBDIR):
	$(MAKEDIRS) $@
$(RUBYLIBDIR)/json/pure:
	$(MAKEDIRS) $@
$(RUBYLIBDIR)/json:
	$(MAKEDIRS) $@
$(RUBYLIBDIR)/json/add:
	$(MAKEDIRS) $@

site-install: site-install-so site-install-rb
site-install-so: install-so
site-install-rb: install-rb

