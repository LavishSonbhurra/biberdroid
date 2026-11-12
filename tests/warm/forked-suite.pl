#!/usr/bin/perl
# Runs biber's own test files the way a warm process runs biber: this process
# compiles every module bin/biber loads, then each test file runs in a child
# forked from it (scripts/suite-forked-approot.sh). The launcher is not
# involved; what is checked is that biber's modules, compiled before the fork,
# work the same in the forked child.
use strict;
use warnings;
use TAP::Parser;

my ($biber, $jobs, @modules);

# In a BEGIN block, as bin/biber's own "use" lines are: modules compiled then
# may still queue INIT blocks.
BEGIN {
    ($biber, $jobs) = @ARGV;
    open my $script, '<', $biber or die "cannot read $biber: $!\n";
    @modules = map { /^use\s+([A-Z][\w:]*)/ ? $1 : () } <$script>;
    close $script;
    for my $module (@modules) {
        eval "require $module; 1" or die "cannot load $module: $@";
    }
}
printf "%d modules compiled (%d named by bin/biber) before the first fork\n", scalar(keys %INC), scalar @modules;

my @files = sort glob 't/*.t';
my (%out, %name, $running);
my ($tests, $failed, @bad) = (0, 0);

sub collect {
    my $pid = wait;
    return if $pid < 0;
    my $status = $?;
    $running--;
    my $file = delete $name{$pid};
    open my $in, '<', $out{$file} or die "no output of $file\n";
    my $tap = do { local $/; <$in> };
    close $in;
    unlink $out{$file};
    my $parser = TAP::Parser->new({ tap => $tap });
    $parser->run;
    $tests += $parser->tests_run;
    my $wrong = $parser->failed + ($parser->is_good_plan ? 0 : 1) + ($status ? 1 : 0) + $parser->parse_errors;
    if ($wrong) {
        $failed += $parser->failed || 1;
        push @bad, sprintf('%s (failed %d of %d, exit %d)', $file, scalar($parser->failed), $parser->tests_run, $status >> 8);
    }
}

for my $file (@files) {
    collect() while ($running // 0) >= ($jobs || 4);
    (my $log = $file) =~ s{/}{_}g;
    $out{$file} = "forked-$log.tap";
    my $pid = fork // die "fork: $!\n";
    if (!$pid) {
        open STDOUT, '>', $out{$file} or die;
        open STDERR, '>', '/dev/null' or die;
        @ARGV = ();
        $0 = $file;
        my $done = do "./$file";
        print "not ok - $file died: $@\n" if !defined $done && $@;
        exit;
    }
    $name{$pid} = $file;
    $running++;
}
collect() while $running;
print "Failed: $_\n" for @bad;
printf "Files=%d, Tests=%d, Failed=%d\n", scalar @files, $tests, $failed;
print $failed || @bad ? "Result: FAIL\n" : "Result: PASS\n";
exit($failed || @bad ? 1 : 0);
