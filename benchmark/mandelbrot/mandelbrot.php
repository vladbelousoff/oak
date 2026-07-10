<?php

$width = 256;
$maxIter = 64;
$step = 2.0 / 256.0;
$reps = 20;

$inside = 0;
for ($rep = 0; $rep < $reps; $rep += 1) {
    for ($py = 0; $py < $width; $py += 1) {
        $cy = -1.0 + $py * $step;
        for ($px = 0; $px < $width; $px += 1) {
            $cx = -1.5 + $px * $step;
            $zr = 0.0;
            $zi = 0.0;
            $iter = 0;
            while ($iter < $maxIter && $zr * $zr + $zi * $zi <= 4.0) {
                $t = $zr * $zr - $zi * $zi + $cx;
                $zi = 2.0 * $zr * $zi + $cy;
                $zr = $t;
                $iter += 1;
            }
            if ($iter == $maxIter) {
                $inside += 1;
            }
        }
    }
}
echo $inside, "\n";
