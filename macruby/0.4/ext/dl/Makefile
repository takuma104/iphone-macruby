
SHELL = /bin/sh

#### Start of system configuration section. ####
top_srcdir = ..//../.
srcdir = $(top_srcdir)/ext/dl
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
CFLAGS   = -fno-common -fno-common -pipe $(cflags) -fno-defer-pop -fno-omit-frame-pointer -arch i386 -arch x86_64
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

CLEANFILES = mkmf.log $(HDRDIR)/dl.h
DISTCLEANFILES = callback.h
DISTCLEANDIRS = $(HDRDIR)

extout = $(topdir)/.ext
extout_prefix = $(extout)$(target_prefix)/
target_prefix = 
LOCAL_LIBS = 
LIBS = $(LIBRUBYARG_SHARED) -ldl   
SRCS = cfunc.c cptr.c dl.c handle.c
OBJS = cfunc.o cptr.o dl.o handle.o
TARGET = dl
DLLIB = $(TARGET).bundle
EXTSTATIC = 
STATIC_LIB = $(TARGET).a

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

install-so: $(RUBYARCHDIR)
install-so: $(RUBYARCHDIR)/$(DLLIB)
clean-so::
	@-$(RM) $(RUBYARCHDIR)/$(DLLIB)
	@-$(RMDIRS) $(RUBYARCHDIR)
install-rb: pre-install-rb install-rb-default
install-rb-default: pre-install-rb-default
pre-install-rb: Makefile
pre-install-rb-default: Makefile
pre-install-rb-default: $(RUBYLIBDIR)/dl
install-rb-default: $(RUBYLIBDIR)/dl/callback.rb
$(RUBYLIBDIR)/dl/callback.rb: $(srcdir)/lib/dl/callback.rb
	$(COPY) $(srcdir)/lib/dl/callback.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/dl/callback.rb
install-rb-default: $(RUBYLIBDIR)/dl/cparser.rb
$(RUBYLIBDIR)/dl/cparser.rb: $(srcdir)/lib/dl/cparser.rb
	$(COPY) $(srcdir)/lib/dl/cparser.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/dl/cparser.rb
install-rb-default: $(RUBYLIBDIR)/dl/func.rb
$(RUBYLIBDIR)/dl/func.rb: $(srcdir)/lib/dl/func.rb
	$(COPY) $(srcdir)/lib/dl/func.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/dl/func.rb
install-rb-default: $(RUBYLIBDIR)/dl/import.rb
$(RUBYLIBDIR)/dl/import.rb: $(srcdir)/lib/dl/import.rb
	$(COPY) $(srcdir)/lib/dl/import.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/dl/import.rb
install-rb-default: $(RUBYLIBDIR)/dl/pack.rb
$(RUBYLIBDIR)/dl/pack.rb: $(srcdir)/lib/dl/pack.rb
	$(COPY) $(srcdir)/lib/dl/pack.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/dl/pack.rb
install-rb-default: $(RUBYLIBDIR)/dl/stack.rb
$(RUBYLIBDIR)/dl/stack.rb: $(srcdir)/lib/dl/stack.rb
	$(COPY) $(srcdir)/lib/dl/stack.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/dl/stack.rb
install-rb-default: $(RUBYLIBDIR)/dl/struct.rb
$(RUBYLIBDIR)/dl/struct.rb: $(srcdir)/lib/dl/struct.rb
	$(COPY) $(srcdir)/lib/dl/struct.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/dl/struct.rb
install-rb-default: $(RUBYLIBDIR)/dl/types.rb
$(RUBYLIBDIR)/dl/types.rb: $(srcdir)/lib/dl/types.rb
	$(COPY) $(srcdir)/lib/dl/types.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/dl/types.rb
install-rb-default: $(RUBYLIBDIR)/dl/value.rb
$(RUBYLIBDIR)/dl/value.rb: $(srcdir)/lib/dl/value.rb
	$(COPY) $(srcdir)/lib/dl/value.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/dl/value.rb
clean-rb-default::
	@-$(RMDIRS) $(RUBYLIBDIR)/dl
pre-install-rb: $(HDRDIR)
install-rb: $(HDRDIR)/dl.h
$(HDRDIR)/dl.h: $(srcdir)/dl.h
	$(COPY) $(srcdir)/dl.h $(@D)
clean-rb::
	@-$(RM) $(HDRDIR)/dl.h
clean-rb::
	@-$(RMDIRS) $(HDRDIR)
	@-$(RMDIRS) $(RUBYLIBDIR)/dl
$(RUBYARCHDIR):
	$(MAKEDIRS) $@
$(HDRDIR):
	$(MAKEDIRS) $@
$(RUBYLIBDIR)/dl:
	$(MAKEDIRS) $@

site-install: site-install-so site-install-rb
site-install-so: install-so
site-install-rb: install-rb

.SUFFIXES: .c .m .cc .cxx .cpp .C .o

.cc.o:
	$(CXX) $(INCFLAGS) $(CPPFLAGS) $(CXXFLAGS) $(COUTFLAG)$@ -c $<

.cxx.o:
	$(CXX) $(INCFLAGS) $(CPPFLAGS) $(CXXFLAGS) $(COUTFLAG)$@ -c $<

.cpp.o:
	$(CXX) $(INCFLAGS) $(CPPFLAGS) $(CXXFLAGS) $(COUTFLAG)$@ -c $<

.C.o:
	$(CXX) $(INCFLAGS) $(CPPFLAGS) $(CXXFLAGS) $(COUTFLAG)$@ -c $<

.c.o:
	$(CC) $(INCFLAGS) $(CPPFLAGS) $(CFLAGS) $(COUTFLAG)$@ -c $<

$(RUBYARCHDIR)/$(DLLIB): $(OBJS) Makefile
	@-$(RM) $(@)
	@-$(MAKEDIRS) $(@D)
	$(LDSHARED) -o $@ $(OBJS) $(LIBPATH) $(DLDFLAGS) $(LOCAL_LIBS) $(LIBS)

$(STATIC_LIB): $(OBJS)
	@-$(RM) $(@)
	$(AR) cru $@ $(OBJS)
	@-ranlib $(DLLIB) 2> /dev/null || true

###
$(OBJS): $(RUBY_EXTCONF_H)

cfunc.o: cfunc.c dl.h $(hdrdir)/ruby/ruby.h

cptr.o: cptr.c dl.h $(hdrdir)/ruby/ruby.h $(hdrdir)/ruby/io.h

handle.o: handle.c dl.h $(hdrdir)/ruby/ruby.h

dl.o: dl.c dl.h callback.h $(hdrdir)/ruby/ruby.h $(hdrdir)/ruby/io.h

callback.h: $(srcdir)/mkcallback.rb dl.h
	@echo "generating callback.h"
	@$(RUBY) $(srcdir)/mkcallback.rb $(srcdir)/dl.h > $@
