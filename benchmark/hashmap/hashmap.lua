local n = 300000
local keys = 20011

local counts = {}
local size = 0
for i = 0, n - 1 do
  local key = 'k' .. tostring((i % keys) * 7919 % keys)
  local current = counts[key]
  if current then
    counts[key] = current + 1
  else
    counts[key] = 1
    size = size + 1
  end
end

local total = 0
for i = 0, n - 1 do
  local key = 'k' .. tostring((i % keys) * 7919 % keys)
  total = total + counts[key]
end

print(string.format("%d", size))
print(string.format("%d", total))
