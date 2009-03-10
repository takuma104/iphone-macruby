
SHELL = /bin/sh

#### Start of system configuration section. ####
top_srcdir = ..//../.
srcdir = $(top_srcdir)/ext/openssl
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
LIBS = $(LIBRUBYARG_SHARED) -lssl -lcrypto   
SRCS = openssl_missing.c ossl.c ossl_asn1.c ossl_bio.c ossl_bn.c ossl_cipher.c ossl_config.c ossl_digest.c ossl_engine.c ossl_hmac.c ossl_ns_spki.c ossl_ocsp.c ossl_pkcs12.c ossl_pkcs5.c ossl_pkcs7.c ossl_pkey.c ossl_pkey_dh.c ossl_pkey_dsa.c ossl_pkey_ec.c ossl_pkey_rsa.c ossl_rand.c ossl_ssl.c ossl_ssl_session.c ossl_x509.c ossl_x509attr.c ossl_x509cert.c ossl_x509crl.c ossl_x509ext.c ossl_x509name.c ossl_x509req.c ossl_x509revoked.c ossl_x509store.c
OBJS = openssl_missing.o ossl.o ossl_asn1.o ossl_bio.o ossl_bn.o ossl_cipher.o ossl_config.o ossl_digest.o ossl_engine.o ossl_hmac.o ossl_ns_spki.o ossl_ocsp.o ossl_pkcs12.o ossl_pkcs5.o ossl_pkcs7.o ossl_pkey.o ossl_pkey_dh.o ossl_pkey_dsa.o ossl_pkey_ec.o ossl_pkey_rsa.o ossl_rand.o ossl_ssl.o ossl_ssl_session.o ossl_x509.o ossl_x509attr.o ossl_x509cert.o ossl_x509crl.o ossl_x509ext.o ossl_x509name.o ossl_x509req.o ossl_x509revoked.o ossl_x509store.o
TARGET = openssl
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
pre-install-rb-default: $(RUBYLIBDIR)/openssl
install-rb-default: $(RUBYLIBDIR)/openssl/bn.rb
$(RUBYLIBDIR)/openssl/bn.rb: $(srcdir)/lib/openssl/bn.rb
	$(COPY) $(srcdir)/lib/openssl/bn.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/openssl/bn.rb
install-rb-default: $(RUBYLIBDIR)/openssl/buffering.rb
$(RUBYLIBDIR)/openssl/buffering.rb: $(srcdir)/lib/openssl/buffering.rb
	$(COPY) $(srcdir)/lib/openssl/buffering.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/openssl/buffering.rb
install-rb-default: $(RUBYLIBDIR)/openssl/cipher.rb
$(RUBYLIBDIR)/openssl/cipher.rb: $(srcdir)/lib/openssl/cipher.rb
	$(COPY) $(srcdir)/lib/openssl/cipher.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/openssl/cipher.rb
install-rb-default: $(RUBYLIBDIR)/openssl/digest.rb
$(RUBYLIBDIR)/openssl/digest.rb: $(srcdir)/lib/openssl/digest.rb
	$(COPY) $(srcdir)/lib/openssl/digest.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/openssl/digest.rb
install-rb-default: $(RUBYLIBDIR)/openssl/ssl.rb
$(RUBYLIBDIR)/openssl/ssl.rb: $(srcdir)/lib/openssl/ssl.rb
	$(COPY) $(srcdir)/lib/openssl/ssl.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/openssl/ssl.rb
install-rb-default: $(RUBYLIBDIR)/openssl/x509.rb
$(RUBYLIBDIR)/openssl/x509.rb: $(srcdir)/lib/openssl/x509.rb
	$(COPY) $(srcdir)/lib/openssl/x509.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/openssl/x509.rb
pre-install-rb-default: $(RUBYLIBDIR)
install-rb-default: $(RUBYLIBDIR)/openssl.rb
$(RUBYLIBDIR)/openssl.rb: $(srcdir)/lib/openssl.rb
	$(COPY) $(srcdir)/lib/openssl.rb $(@D)
clean-rb-default::
	@-$(RM) $(RUBYLIBDIR)/openssl.rb
clean-rb-default::
	@-$(RMDIRS) $(RUBYLIBDIR)
	@-$(RMDIRS) $(RUBYLIBDIR)/openssl
$(RUBYARCHDIR):
	$(MAKEDIRS) $@
$(RUBYLIBDIR):
	$(MAKEDIRS) $@
$(RUBYLIBDIR)/openssl:
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

$(OBJS): $(hdrdir)/ruby.h $(hdrdir)/ruby/defines.h $(arch_hdrdir)/ruby/config.h $(RUBY_EXTCONF_H)
