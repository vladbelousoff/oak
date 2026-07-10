def nsieve(limit)
  flags = Array.new(limit + 1, 1)

  p = 2
  while p * p <= limit
    if flags[p] == 1
      m = p * p
      while m <= limit
        flags[m] = 0
        m += p
      end
    end
    p += 1
  end

  count = 0
  n = 2
  while n <= limit
    count += 1 if flags[n] == 1
    n += 1
  end
  count
end

total = 0
60.times { total += nsieve(500_000) }
puts total
