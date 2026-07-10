def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


total = 0
for rep in range(40):
    total += fib(30)
print(total)
