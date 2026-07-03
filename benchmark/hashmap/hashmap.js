const N = 300000;
const KEYS = 20011;

const counts = new Map();
for (let i = 0; i < N; i += 1) {
  const key = 'k' + (((i % KEYS) * 7919) % KEYS);
  if (counts.has(key)) {
    counts.set(key, counts.get(key) + 1);
  } else {
    counts.set(key, 1);
  }
}

let total = 0;
for (let i = 0; i < N; i += 1) {
  const key = 'k' + (((i % KEYS) * 7919) % KEYS);
  total += counts.get(key);
}

console.log(counts.size);
console.log(total);
