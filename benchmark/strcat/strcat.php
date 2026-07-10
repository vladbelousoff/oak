<?php

$n = 20000000;

$total = 0;
for ($i = 0; $i < $n; $i += 1) {
    $s = 'id-' . ($i % 1000) . '-x';
    $total += strlen($s);
}
echo $total, "\n";
