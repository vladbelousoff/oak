N = 1000000

total = 0
for i in range(N):
    s = 'id-' + str(i % 1000) + '-x'
    total += len(s)
print(total)
