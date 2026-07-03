n = 300_000
keys = 20_011

counts = {}
i = 0
while i < n
  key = 'k' + ((i % keys) * 7919 % keys).to_s
  if counts.key?(key)
    counts[key] += 1
  else
    counts[key] = 1
  end
  i += 1
end

total = 0
i = 0
while i < n
  key = 'k' + ((i % keys) * 7919 % keys).to_s
  total += counts[key]
  i += 1
end

puts counts.size
puts total
