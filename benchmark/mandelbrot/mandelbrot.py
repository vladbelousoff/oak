WIDTH = 256
MAX_ITER = 64
STEP = 2.0 / 256.0

inside = 0
for py in range(WIDTH):
    cy = -1.0 + py * STEP
    for px in range(WIDTH):
        cx = -1.5 + px * STEP
        zr = 0.0
        zi = 0.0
        it = 0
        while it < MAX_ITER and zr * zr + zi * zi <= 4.0:
            zr, zi = zr * zr - zi * zi + cx, 2.0 * zr * zi + cy
            it += 1
        if it == MAX_ITER:
            inside += 1
print(inside)
