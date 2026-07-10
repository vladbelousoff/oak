use strict;
use warnings;

sub fib {
    my ($n) = @_;
    return $n if $n < 2;
    return fib($n - 1) + fib($n - 2);
}

my $total = 0;
for my $rep (1 .. 40) {
    $total += fib(30);
}
print "$total\n";
