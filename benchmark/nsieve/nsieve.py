def nsieve(limit):
    flags = [1] * (limit + 1)

    p = 2
    while p * p <= limit:
        if flags[p] == 1:
            m = p * p
            while m <= limit:
                flags[m] = 0
                m += p
        p += 1

    count = 0
    for n in range(2, limit + 1):
        if flags[n] == 1:
            count += 1
    return count


total = 0
for rep in range(1):
    total += nsieve(500000)
print(total)
