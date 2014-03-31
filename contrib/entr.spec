Name:		entr
Version:	2.8
Release:	1%{?dist}
Summary:	A utility for running arbitrary commands when files change
Group:		Development/Tools
License:	ISC
URL:		http://entrproject.org/
# You can generate this tarball with:
#	hg clone https://bitbucket.org/eradman/entr
#	cd entr
#	hg archive -r entr-2.8 ../entr-2.6.tar.gz
Source0:	%{name}-%{version}.tar.gz

%description
A utility for running arbitrary commands when files change. Uses kqueue(2) or
inotify(7) to avoid polling. entr responds to file system events by executing
command line arguments or by writing to a FIFO.

%prep
%setup -q

%build
./configure
make %{?_smp_mflags}

%install
PREFIX=%{buildroot}/usr make install

%check
make test

%files
%defattr(-,root,root)
%{_bindir}/entr
%attr(0644,-,-) %{_mandir}/man1/entr.1.gz
%doc LICENSE README.md

%changelog
* Mon Mar 31 2014 Eric Radman <ericshane@eradman.com>
- Packaged 2.8-1
* Sun Feb  9 2014 Jordi Funollet Pujol <funollet@fastmail.fm>
- Packaged 2.6-1


