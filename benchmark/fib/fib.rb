def fib(n)
  return n if n < 2
  fib(n - 1) + fib(n - 2)
end

total = 0
40.times { total += fib(30) }
puts total
