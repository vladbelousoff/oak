use strict;
use warnings;

my $n = 300000;
my $keys = 20011;
my $reps = 25;

my $size_sum = 0;
my $total = 0;
for my $rep (1 .. $reps) {
    my %counts;
    for my $i (0 .. $n - 1) {
        my $key = 'k' . (($i % $keys) * 7919 % $keys);
        if (exists $counts{$key}) {
            $counts{$key} += 1;
        } else {
            $counts{$key} = 1;
        }
    }

    for my $i (0 .. $n - 1) {
        my $key = 'k' . (($i % $keys) * 7919 % $keys);
        $total += $counts{$key};
    }

    $size_sum += scalar keys %counts;
}

print "$size_sum\n";
print "$total\n";
