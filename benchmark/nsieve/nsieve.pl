use strict;
use warnings;

sub nsieve {
    my ($limit) = @_;
    my @flags = (1) x ($limit + 1);

    my $p = 2;
    while ($p * $p <= $limit) {
        if ($flags[$p] == 1) {
            my $m = $p * $p;
            while ($m <= $limit) {
                $flags[$m] = 0;
                $m += $p;
            }
        }
        $p += 1;
    }

    my $count = 0;
    for my $n (2 .. $limit) {
        $count += 1 if $flags[$n] == 1;
    }
    return $count;
}

my $total = 0;
for my $rep (1 .. 60) {
    $total += nsieve(500000);
}
print "$total\n";
