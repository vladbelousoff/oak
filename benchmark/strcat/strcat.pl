use strict;
use warnings;

my $n = 20000000;

my $total = 0;
for my $i (0 .. $n - 1) {
    my $s = 'id-' . ($i % 1000) . '-x';
    $total += length $s;
}
print "$total\n";
