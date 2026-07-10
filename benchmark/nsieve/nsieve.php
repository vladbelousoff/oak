<?php

function nsieve(int $limit): int
{
    $flags = array_fill(0, $limit + 1, 1);

    $p = 2;
    while ($p * $p <= $limit) {
        if ($flags[$p] == 1) {
            $m = $p * $p;
            while ($m <= $limit) {
                $flags[$m] = 0;
                $m += $p;
            }
        }
        $p += 1;
    }

    $count = 0;
    for ($n = 2; $n <= $limit; $n += 1) {
        if ($flags[$n] == 1) {
            $count += 1;
        }
    }
    return $count;
}

$total = 0;
for ($rep = 0; $rep < 60; $rep += 1) {
    $total += nsieve(500000);
}
echo $total, "\n";
