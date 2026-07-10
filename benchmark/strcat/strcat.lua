local n = 20000000

local total = 0
for i = 0, n - 1 do
  local s = 'id-' .. tostring(i % 1000) .. '-x'
  total = total + #s
end
print(string.format("%d", total))
