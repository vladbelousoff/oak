n = 300_000
keys = 20_011
reps = 25

size_sum = 0
total = 0
rep = 0
while rep < reps
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

  i = 0
  while i < n
    key = 'k' + ((i % keys) * 7919 % keys).to_s
    total += counts[key]
    i += 1
  end

  size_sum += counts.size
  rep += 1
end

puts size_sum
puts total
