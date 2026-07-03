const WIDTH = 256;
const MAX_ITER = 64;
const STEP = 2.0 / 256.0;

let inside = 0;
for (let py = 0; py < WIDTH; py += 1) {
  const cy = -1.0 + py * STEP;
  for (let px = 0; px < WIDTH; px += 1) {
    const cx = -1.5 + px * STEP;
    let zr = 0.0;
    let zi = 0.0;
    let iter = 0;
    while (iter < MAX_ITER && zr * zr + zi * zi <= 4.0) {
      const t = zr * zr - zi * zi + cx;
      zi = 2.0 * zr * zi + cy;
      zr = t;
      iter += 1;
    }
    if (iter === MAX_ITER) {
      inside += 1;
    }
  }
}
console.log(inside);
