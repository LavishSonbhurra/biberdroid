package BiberNative;
# Loads XS modules from nativeLibraryDir, where Android lets an app keep
# executable code. Module Foo::Bar lives there as libpl_Foo_Bar.so; perl's
# usual auto/Foo/Bar/Bar.so lookup is the fallback.
use strict;
our $DIR = $ENV{BIBER_NATIVE_DIR};

# The interpreter's built-in @INC points at the Termux prefix it was built
# for; nothing there belongs to us.
BEGIN { @INC = grep { !m{^/data/data/com\.termux/} } @INC }

require DynaLoader;
require XSLoader;

sub _native {
    my ($module, @args) = @_;
    return unless defined $DIR;
    (my $name = $module) =~ s/::/_/g;
    my $file = "$DIR/libpl_$name.so";
    return unless -f $file;
    my $flags = $module->can('dl_load_flags') ? $module->dl_load_flags : 0;
    my $libref = DynaLoader::dl_load_file($file, $flags)
        or die "Can't load '$file' for module $module: " . DynaLoader::dl_error() . "\n";
    push @DynaLoader::dl_librefs, $libref;
    push @DynaLoader::dl_modules, $module;
    push @DynaLoader::dl_shared_objects, $file;
    (my $bootname = "boot_$module") =~ s/\W/_/g;
    my $boot = DynaLoader::dl_find_symbol($libref, $bootname)
        or die "Can't find '$bootname' in $file\n";
    my $xs = DynaLoader::dl_install_xsub("${module}::bootstrap", $boot, $file);
    return [ &$xs($module, @args) ];
}

{
    no warnings 'redefine';
    my $xsloader = \&XSLoader::load;
    *XSLoader::load = sub {
        my @args = @_ ? @_ : (scalar caller);
        my $r = _native(@args);
        return $r ? $r->[-1] : $xsloader->(@args);
    };
    my $bootstrap = \&DynaLoader::bootstrap;
    *DynaLoader::bootstrap = sub {
        my $r = _native(@_);
        return $r ? $r->[-1] : $bootstrap->(@_);
    };
}
1;
