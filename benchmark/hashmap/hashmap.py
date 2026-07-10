N = 300000
KEYS = 20011
REPS = 25

size_sum = 0
total = 0
for rep in range(REPS):
    counts = {}
    for i in range(N):
        key = 'k' + str((i % KEYS) * 7919 % KEYS)
        if key in counts:
            counts[key] += 1
        else:
            counts[key] = 1

    for i in range(N):
        key = 'k' + str((i % KEYS) * 7919 % KEYS)
        total += counts[key]

    size_sum += len(counts)

print(size_sum)
print(total)
