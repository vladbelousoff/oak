local function fib(n)
  if n < 2 then
    return n
  end
  return fib(n - 1) + fib(n - 2)
end

local total = 0
for rep = 1, 40 do
  total = total + fib(30)
end
print(string.format("%d", total))
