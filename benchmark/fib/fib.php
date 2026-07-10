<?php

function fib(int $n): int
{
    if ($n < 2) {
        return $n;
    }
    return fib($n - 1) + fib($n - 2);
}

$total = 0;
for ($rep = 0; $rep < 40; $rep += 1) {
    $total += fib(30);
}
echo $total, "\n";
