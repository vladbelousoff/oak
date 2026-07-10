function nsieve(limit) {
  const flags = new Array(limit + 1).fill(1);

  let p = 2;
  while (p * p <= limit) {
    if (flags[p] === 1) {
      let m = p * p;
      while (m <= limit) {
        flags[m] = 0;
        m += p;
      }
    }
    p += 1;
  }

  let count = 0;
  for (let n = 2; n <= limit; n += 1) {
    if (flags[n] === 1) {
      count += 1;
    }
  }
  return count;
}

let total = 0;
for (let rep = 0; rep < 60; rep += 1) {
  total += nsieve(500000);
}
console.log(total);
