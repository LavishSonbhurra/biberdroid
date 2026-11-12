#!/usr/bin/perl
# Stands in for bin/biber in the warm process checks (scripts/warm-approot.sh):
# reports the state a run starts in, then does what its arguments ask. The
# same report must come from a run on its own and from one a warm process did.
use strict;
use warnings;
use Cwd ();
# As Biber/CodePage.pm does while biber compiles.
use sigtrap qw(die untrapped normal-signals);

my %do = map { /^(\w+)=(.*)$/s ? ($1 => $2) : ($_ => 1) } @ARGV;
END { print STDERR "END block ran\n" if $do{end} }

sub slurp { open my $f, '<', $_[0] or return (); my @l = <$f>; close $f; @l }

if ($do{state}) {
    my @l;
    push @l, 'args: ' . join('|', @ARGV);
    push @l, 'cwd: ' . Cwd::getcwd();
    push @l, "env: $_=$ENV{$_}" for sort keys %ENV;
    push @l, sprintf('umask: %04o', umask);
    push @l, 'nice: ' . getpriority(0, 0);
    push @l, 'terminal: ' . join(' ', map { -t $_ ? 1 : 0 } \*STDIN, \*STDOUT, \*STDERR);
    push @l, 'started: ' . (time - $^T <= 2 ? 'now' : 'long ago');
    push @l, "script: $0";
    push @l, "sig $_: " . (ref $SIG{$_} ? 'CODE' : $SIG{$_}) for grep { defined $SIG{$_} } sort keys %SIG;
    push @l, map { s/\s+$//r } grep { /^Sig(Blk|Ign|Cgt)/ } slurp('/proc/self/status');
    push @l, map { s/\s+$//r } slurp('/proc/self/limits');
    # Open descriptors above 2, by what they are: nothing of a warm process may show here.
    if (opendir my $d, '/proc/self/fd') {
        my $own = fileno($d) // -1;
        for my $fd (sort { $a <=> $b } grep { /^\d+$/ && $_ > 2 && $_ != $own } readdir $d) {
            my $to = readlink("/proc/self/fd/$fd") // '?';
            $to =~ s/\[\d+\]/[N]/;
            push @l, "open: $to";
        }
    }
    push @l, 'modules: ' . scalar(keys %INC);
    print "$_\n" for @l;
}
if ($do{order}) {
    # Through one pipe or file: a block-buffered STDOUT comes after STDERR, a line-buffered one before.
    print STDOUT "first on stdout\n";
    print STDERR "then on stderr\n";
}
if ($do{stdin}) {
    my $n = 0;
    $n += length while <STDIN>;
    print "stdin: $n bytes\n";
}
if (defined $do{write}) {
    open my $f, '>>', $do{write} or die "cannot write $do{write}: $!\n";
    print $f "written by $ARGV[0]\n" or die "cannot write $do{write}: $!\n";
    close $f or die "cannot write $do{write}: $!\n";
    printf "mode: %04o\n", (stat $do{write})[2] & 07777;
}
if (defined $do{fill}) {
    # Until the disk is full.
    open my $f, '>', $do{fill} or die "cannot write $do{fill}: $!\n";
    my $block = 'x' x 4096;
    for (1 .. 100000) { print $f $block or die "write failed: $!\n" }
    close $f or die "close failed: $!\n";
}
if ($do{lines}) { print "line $_\n" for 1 .. $do{lines} }
if ($do{flood}) { print 'y' x 4096, "\n" while 1 }
if ($do{sleep}) { select undef, undef, undef, $do{sleep} }
if ($do{kill}) { kill $do{kill}, $$; select undef, undef, undef, 1 }
if ($do{rand}) { print 'rand: ', rand(), "\n" }
exit($do{exit} // 0);
