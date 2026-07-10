local width = 256
local max_iter = 64
local step = 2.0 / 256.0
local reps = 20

local inside = 0
for rep = 1, reps do
  for py = 0, width - 1 do
    local cy = -1.0 + py * step
    for px = 0, width - 1 do
      local cx = -1.5 + px * step
      local zr = 0.0
      local zi = 0.0
      local iter = 0
      while iter < max_iter and zr * zr + zi * zi <= 4.0 do
        local t = zr * zr - zi * zi + cx
        zi = 2.0 * zr * zi + cy
        zr = t
        iter = iter + 1
      end
      if iter == max_iter then
        inside = inside + 1
      end
    end
  end
end
print(string.format("%d", inside))
