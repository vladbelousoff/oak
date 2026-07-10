width = 256
max_iter = 64
step = 2.0 / 256.0
reps = 20

inside = 0
rep = 0
while rep < reps
  py = 0
  while py < width
    cy = -1.0 + py * step
    px = 0
    while px < width
      cx = -1.5 + px * step
      zr = 0.0
      zi = 0.0
      iter = 0
      while iter < max_iter && zr * zr + zi * zi <= 4.0
        t = zr * zr - zi * zi + cx
        zi = 2.0 * zr * zi + cy
        zr = t
        iter += 1
      end
      inside += 1 if iter == max_iter
      px += 1
    end
    py += 1
  end
  rep += 1
end
puts inside
