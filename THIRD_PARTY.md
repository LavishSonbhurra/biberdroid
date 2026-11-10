# Third-party components

The payload (the files in `jniLibs/arm64-v8a` and the Perl tree) is made of the components
below, generated from the build's `COMPONENTS.tsv`. Each release also carries:

- `NOTICES.txt`: this list with the full licence texts, for an app's licences screen.
  The AAR holds the same file as `assets/biberdroid/NOTICES.txt`.
- `biberdroid-<version>-sources.tar`: the complete source of everything listed here.
- `COMPONENTS.tsv` and, inside the payload archive, `FILES.tsv`, which maps every shipped
  file to its component.

Where a component lets the recipient choose a licence (Perl and most CPAN modules: the
Artistic License or the GPL), it is distributed here under the Artistic License. Perl's
Unicode data (`lib/unicore` and the collation table in `libpl_Unicode_Collate.so`) is under
the Unicode License v3. The licence texts are in [licenses/](licenses).

| Component | Version | Licence | Source | Files |
| --- | --- | --- | --- | ---: |
| biber | 2.21 | Artistic-2.0 | <https://github.com/plk/biber/archive/refs/tags/v2.21.tar.gz> | 37 |
| biberdroid | - | Artistic-2.0 | <https://github.com/LavishSonbhurra/biberdroid> (src/biber-launcher.c, src/BiberNative.pm) | 2 |
| libiconv | 1.19.1 (Termux libiconv 1.19.1) | LGPL-2.1-or-later | <https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.19.1.tar.gz><br><https://github.com/termux/termux-packages/tree/master/packages/libiconv> | 1 |
| libxml2 | 2.15.4 (Termux libxml2 2.15.4-1) | MIT | <https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.4.tar.xz><br><https://github.com/termux/termux-packages/tree/master/packages/libxml2> | 1 |
| perl | 5.44.0 (Termux perl 5.44.0-1) | (Artistic-1.0-Perl OR GPL-1.0-or-later) AND Unicode-3.0 | <https://www.cpan.org/src/5.0/perl-5.44.0.tar.gz><br><https://github.com/termux/termux-packages/tree/master/packages/perl> | 263 |
| zlib | 1.4 (Termux zlib 1.4) | Zlib | <https://zlib.net/><br><https://github.com/termux/termux-packages/tree/master/packages/zlib> | 1 |
| autovivification | 0.18 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/V/VP/VPIT/autovivification-0.18.tar.gz> | 2 |
| B-Hooks-EndOfScope | 0.28 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/E/ET/ETHER/B-Hooks-EndOfScope-0.28.tar.gz> | 5 |
| Business-ISBN | 3.014 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/B/BR/BRIANDFOY/Business-ISBN-3.014.tar.gz> | 3 |
| Business-ISBN-Data | 20261103.001 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/B/BR/BRIANDFOY/Business-ISBN-Data-20261103.001.tar.gz> | 3 |
| Business-ISMN | 1.205 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/B/BR/BRIANDFOY/Business-ISMN-1.205.tar.gz> | 2 |
| Business-ISSN | 1.008 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/B/BR/BRIANDFOY/Business-ISSN-1.008.tar.gz> | 1 |
| Capture-Tiny | 0.50 | Apache-2.0 | <https://cpan.metacpan.org/authors/id/D/DA/DAGOLDEN/Capture-Tiny-0.50.tar.gz> | 1 |
| Class-Accessor | 0.51 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/K/KA/KASEI/Class-Accessor-0.51.tar.gz> | 3 |
| Class-Data-Inheritable | 0.10 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/R/RS/RSHERER/Class-Data-Inheritable-0.10.tar.gz> | 1 |
| Class-Inspector | 1.36 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/P/PL/PLICEASE/Class-Inspector-1.36.tar.gz> | 2 |
| Class-Singleton | 1.6 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/S/SH/SHAY/Class-Singleton-1.6.tar.gz> | 1 |
| Clone | 0.51 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/A/AT/ATOOMIC/Clone-0.51.tar.gz> | 2 |
| Data-Compare | 1.29 | Artistic-1.0-Perl OR GPL-2.0-only | <https://cpan.metacpan.org/authors/id/D/DC/DCANTRELL/Data-Compare-1.29.tar.gz> | 2 |
| Data-Dump | 1.25 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/G/GA/GARU/Data-Dump-1.25.tar.gz> | 4 |
| Data-Uniqid | 0.12 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/M/MW/MWX/Data-Uniqid-0.12.tar.gz> | 2 |
| DateTime | 1.68 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/DateTime-1.68.tar.gz> | 10 |
| DateTime-Calendar-Julian | 0.108 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/W/WY/WYANT/DateTime-Calendar-Julian-0.108.tar.gz> | 1 |
| DateTime-Format-Builder | 0.83 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/DateTime-Format-Builder-0.83.tar.gz> | 7 |
| DateTime-Format-Strptime | 1.80 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/DateTime-Format-Strptime-1.80.tar.gz> | 2 |
| DateTime-Locale | 1.47 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/DateTime-Locale-1.47.tar.gz> | 7 |
| DateTime-TimeZone | 2.72 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/DateTime-TimeZone-2.72.tar.gz> | 14 |
| Devel-StackTrace | 2.05 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/Devel-StackTrace-2.05.tar.gz> | 2 |
| Dist-CheckConflicts | 0.11 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/D/DO/DOY/Dist-CheckConflicts-0.11.tar.gz> | 1 |
| Encode-Locale | 1.05 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/G/GA/GAAS/Encode-Locale-1.05.tar.gz> | 1 |
| Eval-Closure | 0.14 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/D/DO/DOY/Eval-Closure-0.14.tar.gz> | 1 |
| Exception-Class | 1.46 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/Exception-Class-1.46.tar.gz> | 2 |
| Exporter-Tiny | 1.006003 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/T/TO/TOBYINK/Exporter-Tiny-1.006003.tar.gz> | 2 |
| File-Find-Rule | 0.35 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/R/RC/RCLAMP/File-Find-Rule-0.35.tar.gz> | 1 |
| File-ShareDir | 1.118 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/R/RE/REHSACK/File-ShareDir-1.118.tar.gz> | 4 |
| File-Slurper | 0.014 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/L/LE/LEONT/File-Slurper-0.014.tar.gz> | 1 |
| File-Which | 1.27 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/P/PL/PLICEASE/File-Which-1.27.tar.gz> | 1 |
| IO-String | 1.08 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/G/GA/GAAS/IO-String-1.08.tar.gz> | 1 |
| IPC-Run3 | 0.049 | BSD-3-Clause OR Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/R/RJ/RJBS/IPC-Run3-0.049.tar.gz> | 6 |
| Lingua-Translit | 0.29 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/A/AL/ALINKE/Lingua-Translit-0.29.tar.gz> | 2 |
| List-AllUtils | 0.19 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/List-AllUtils-0.19.tar.gz> | 1 |
| List-MoreUtils | 0.430 | Apache-2.0 | <https://cpan.metacpan.org/authors/id/R/RE/REHSACK/List-MoreUtils-0.430.tar.gz> | 2 |
| List-MoreUtils-XS | 0.430 | Apache-2.0 | <https://cpan.metacpan.org/authors/id/R/RE/REHSACK/List-MoreUtils-XS-0.430.tar.gz> | 2 |
| List-SomeUtils | 0.59 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/List-SomeUtils-0.59.tar.gz> | 2 |
| List-SomeUtils-XS | 0.59 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/List-SomeUtils-XS-0.59.tar.gz> | 2 |
| List-UtilsBy | 0.12 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/P/PE/PEVANS/List-UtilsBy-0.12.tar.gz> | 1 |
| Log-Log4perl | 1.58 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/M/MS/MSCHILLI/Log-Log4perl-1.58.tar.gz> | 51 |
| MIME-Charset | 1.013.1 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/N/NE/NEZUMI/MIME-Charset-1.013.1.tar.gz> | 4 |
| Module-Implementation | 0.09 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/Module-Implementation-0.09.tar.gz> | 1 |
| Module-Runtime | 0.018 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/H/HA/HAARG/Module-Runtime-0.018.tar.gz> | 1 |
| MRO-Compat | 0.15 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/H/HA/HAARG/MRO-Compat-0.15.tar.gz> | 1 |
| namespace-autoclean | 0.31 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/E/ET/ETHER/namespace-autoclean-0.31.tar.gz> | 1 |
| namespace-clean | 0.27 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/R/RI/RIBASUSHI/namespace-clean-0.27.tar.gz> | 2 |
| Number-Compare | 0.03 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/R/RC/RCLAMP/Number-Compare-0.03.tar.gz> | 1 |
| Package-Stash | 0.40 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/E/ET/ETHER/Package-Stash-0.40.tar.gz> | 3 |
| Package-Stash-XS | 0.32 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/E/ET/ETHER/Package-Stash-XS-0.32.tar.gz> | 2 |
| Params-Validate | 1.31 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/Params-Validate-1.31.tar.gz> | 7 |
| Params-ValidationCompiler | 0.31 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/Params-ValidationCompiler-0.31.tar.gz> | 3 |
| Parse-RecDescent | 1.967015 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/J/JT/JTBRAUN/Parse-RecDescent-1.967015.tar.gz> | 1 |
| PerlIO-utf8_strict | 0.010 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/L/LE/LEONT/PerlIO-utf8_strict-0.010.tar.gz> | 2 |
| Regexp-Common | 2024080801 | MIT OR BSD-3-Clause OR Artistic-1.0-Perl OR Artistic-2.0 | <https://cpan.metacpan.org/authors/id/A/AB/ABIGAIL/Regexp-Common-2024080801.tar.gz> | 33 |
| Role-Tiny | 2.002005 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/H/HA/HAARG/Role-Tiny-2.002005.tar.gz> | 2 |
| Sort-Key | 1.33 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/S/SA/SALVA/Sort-Key-1.33.tar.gz> | 7 |
| Specio | 0.53 | Artistic-2.0 | <https://cpan.metacpan.org/authors/id/D/DR/DROLSKY/Specio-0.53.tar.gz> | 41 |
| Sub-Exporter-Progressive | 0.001013 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/F/FR/FREW/Sub-Exporter-Progressive-0.001013.tar.gz> | 1 |
| Sub-Quote | 2.006009 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/H/HA/HAARG/Sub-Quote-2.006009.tar.gz> | 2 |
| Text-BibTeX | 1.00 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/A/AM/AMBS/Text-BibTeX-1.00.tar.gz> | 12 |
| Text-CSV | 2.06 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/I/IS/ISHIGAKI/Text-CSV-2.06.tar.gz> | 2 |
| Text-CSV_XS | 1.65 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/H/HM/HMBRAND/Text-CSV_XS-1.65.tgz> | 2 |
| Text-Glob | 0.11 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/R/RC/RCLAMP/Text-Glob-0.11.tar.gz> | 1 |
| Text-Roman | 3.5 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/S/SY/SYP/Text-Roman-3.5.tar.gz> | 1 |
| Try-Tiny | 0.32 | MIT | <https://cpan.metacpan.org/authors/id/E/ET/ETHER/Try-Tiny-0.32.tar.gz> | 1 |
| Unicode-LineBreak | 2019.001 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/N/NE/NEZUMI/Unicode-LineBreak-2019.001.tar.gz> | 6 |
| URI | 5.40 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/O/OA/OALDERS/URI-5.40.tar.gz> | 68 |
| Variable-Magic | 0.65 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/V/VP/VPIT/Variable-Magic-0.65.tar.gz> | 2 |
| XML-LibXML | 2.0213 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/T/TO/TODDR/XML-LibXML-2.0213.tar.gz> | 17 |
| XML-LibXML-Simple | 1.01 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/M/MA/MARKOV/XML-LibXML-Simple-1.01.tar.gz> | 1 |
| XML-NamespaceSupport | 1.12 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/P/PE/PERIGRIN/XML-NamespaceSupport-1.12.tar.gz> | 1 |
| XML-SAX | 1.02 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/G/GR/GRANTM/XML-SAX-1.02.tar.gz> | 20 |
| XML-SAX-Base | 1.09 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/G/GR/GRANTM/XML-SAX-Base-1.09.tar.gz> | 3 |
| XML-Writer | 0.900 | LicenseRef-XML-Writer (use and redistribution permitted under any circumstances) | <https://cpan.metacpan.org/authors/id/J/JO/JOSEPHW/XML-Writer-0.900.tar.gz> | 1 |
| XString | 0.005 | Artistic-1.0-Perl OR GPL-1.0-or-later | <https://cpan.metacpan.org/authors/id/A/AT/ATOOMIC/XString-0.005.tar.gz> | 2 |
