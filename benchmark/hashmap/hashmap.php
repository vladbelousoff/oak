<?php

$n = 300000;
$keys = 20011;
$reps = 25;

$sizeSum = 0;
$total = 0;
for ($rep = 0; $rep < $reps; $rep += 1) {
    $counts = [];
    for ($i = 0; $i < $n; $i += 1) {
        $key = 'k' . (($i % $keys) * 7919 % $keys);
        if (isset($counts[$key])) {
            $counts[$key] += 1;
        } else {
            $counts[$key] = 1;
        }
    }

    for ($i = 0; $i < $n; $i += 1) {
        $key = 'k' . (($i % $keys) * 7919 % $keys);
        $total += $counts[$key];
    }

    $sizeSum += count($counts);
}

echo $sizeSum, "\n";
echo $total, "\n";
