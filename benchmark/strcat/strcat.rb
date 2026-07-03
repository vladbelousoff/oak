n = 1_000_000

total = 0
i = 0
while i < n
  s = 'id-' + (i % 1000).to_s + '-x'
  total += s.size
  i += 1
end
puts total
