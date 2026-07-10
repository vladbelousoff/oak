use strict;
use warnings;

my $width = 256;
my $max_iter = 64;
my $step = 2.0 / 256.0;
my $reps = 20;

my $inside = 0;
for my $rep (1 .. $reps) {
    for my $py (0 .. $width - 1) {
        my $cy = -1.0 + $py * $step;
        for my $px (0 .. $width - 1) {
            my $cx = -1.5 + $px * $step;
            my $zr = 0.0;
            my $zi = 0.0;
            my $iter = 0;
            while ($iter < $max_iter && $zr * $zr + $zi * $zi <= 4.0) {
                my $t = $zr * $zr - $zi * $zi + $cx;
                $zi = 2.0 * $zr * $zi + $cy;
                $zr = $t;
                $iter += 1;
            }
            $inside += 1 if $iter == $max_iter;
        }
    }
}
print "$inside\n";
