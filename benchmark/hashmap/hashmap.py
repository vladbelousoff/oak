N = 300000
KEYS = 20011

counts = {}
for i in range(N):
    key = 'k' + str((i % KEYS) * 7919 % KEYS)
    if key in counts:
        counts[key] += 1
    else:
        counts[key] = 1

total = 0
for i in range(N):
    key = 'k' + str((i % KEYS) * 7919 % KEYS)
    total += counts[key]

print(len(counts))
print(total)
