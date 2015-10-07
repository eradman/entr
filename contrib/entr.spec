Name:		entr
Version:	3.3
Release:	1%{?dist}
Summary:	A utility for running arbitrary commands when files change
Group:		Development/Tools
License:	ISC
URL:		http://entrproject.org/
Source0:	%{name}-%{version}.tar.gz

%description
A utility for running arbitrary commands when files change. Uses kqueue(2) or
inotify(7) to avoid polling. entr responds to file system events by executing
command line arguments.

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
* Tue Nov 06 2015 Eric Radman <ericshane@eradman.com>
- Modified description to match features of 3.2
* Sun Feb  9 2014 Jordi Funollet Pujol <funollet@fastmail.fm>
- Packaged 2.6-1

