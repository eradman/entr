Name:		entr
Version:	2.6
Release:	1%{?dist}
Summary:	A utility for running arbitrary commands when files change
Group:		Development/Tools
License:	ISC
URL:		http://entrproject.org/
# You can generate this tarball with:
#	hg clone https://bitbucket.org/eradman/entr
#	cd entr
#	hg archive -r entr-2.6 ../entr-2.6.tar.gz
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
* Sun Feb  9 2014 Jordi Funollet Pujol <funollet@fastmail.fm> - 2.6-1
- New release.


