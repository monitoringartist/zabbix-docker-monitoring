ACLOCAL_AMFLAGS = -I m4

SUBDIRS = \
	src

EXTRA_DIST = \
	include \
	README.md

# RPM package build target
RPMBASE = ~/rpmbuild
RPMBUILD = rpmbuild
RPMBUILD_FLAGS = -ba
RPMFILE = $(PACKAGE)-$(PACKAGE_VERSION)-*.$(build_cpu).rpm

rpm:
	sed -i 's/^Version\s*:.*/Version     : $(PACKAGE_VERSION)/' packaging/rpmbuild/$(PACKAGE).spec
	mkdir -p $(RPMBASE)/{BUILD,RPMS,SOURCES,SPECS,SRPMS} || :
	$(MAKE) dist
	cp $(PACKAGE)-$(PACKAGE_VERSION).tar.gz $(RPMBASE)/SOURCES/$(PACKAGE)-$(PACKAGE_VERSION).tar.gz
	cp packaging/rpmbuild/$(PACKAGE).spec $(RPMBASE)/SPECS/$(PACKAGE).spec
	$(RPMBUILD) $(RPMBUILD_FLAGS) packaging/rpmbuild/$(PACKAGE).spec
	cp $(RPMBASE)/RPMS/$(build_cpu)/$(RPMFILE) .

rpmclean:
	rm -f $(RPMFILE)
	rm -f $(PACKAGE)-$(PACKAGE_VERSION).tar.gz

deb:
	$(MAKE) dist
	tar xf $(PACKAGE)-$(PACKAGE_VERSION).tar.gz
	cp -rv packaging/debian $(PACKAGE)-$(PACKAGE_VERSION)/
	mv $(PACKAGE)-$(PACKAGE_VERSION).tar.gz $(PACKAGE)_$(PACKAGE_VERSION).orig.tar.gz
	cd $(PACKAGE)-$(PACKAGE_VERSION) && debuild -us -uc

debclean:
	rm -rf $(PACKAGE)-$(PACKAGE_VERSION)/
	rm -f $(PACKAGE)_$(PACKAGE_VERSION).orig.tar.gz
	rm -f $(PACKAGE)_$(PACKAGE_VERSION)-*.build
	rm -f $(PACKAGE)_$(PACKAGE_VERSION)-*.changes
	rm -f $(PACKAGE)_$(PACKAGE_VERSION)-*.debian.tar.gz
	rm -f $(PACKAGE)_$(PACKAGE_VERSION)-*.deb
	rm -f $(PACKAGE)_$(PACKAGE_VERSION)-*.dsc

.PHONY: test rpm rpmclean deb debclean 
