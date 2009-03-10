
SHELL = /bin/sh

#### Start of system configuration section. ####
top_srcdir = ..//../.
srcdir = $(top_srcdir)/ext/gdbm
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

RUBY_EXTCONF_H = 
cflags   = $(optflags) $(debugflags) $(warnflags)
optflags = -O2
debugflags = -g
warnflags = -Wall -Wno-parentheses
CFLAGS   = -fno-common -fno-common -pipe $(cflags) -arch i386 -arch x86_64
INCFLAGS = -I. -I$(arch_hdrdir) -I$(hdrdir) -I$(srcdir)
DEFS     = 
CPPFLAGS =  $(cppflags)
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
CLEANFILES = mkmf.log
DISTCLEANFILES = 

all install static install-so install-rb: Makefile


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
