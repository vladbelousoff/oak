local function nsieve(limit)
  local flags = {}
  for i = 0, limit do
    flags[i] = 1
  end

  local p = 2
  while p * p <= limit do
    if flags[p] == 1 then
      local m = p * p
      while m <= limit do
        flags[m] = 0
        m = m + p
      end
    end
    p = p + 1
  end

  local count = 0
  for n = 2, limit do
    if flags[n] == 1 then
      count = count + 1
    end
  end
  return count
end

local total = 0
for rep = 1, 1 do
  total = total + nsieve(500000)
end
print(string.format("%d", total))
