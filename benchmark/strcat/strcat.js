const N = 1000000;

let total = 0;
for (let i = 0; i < N; i += 1) {
  const s = 'id-' + (i % 1000) + '-x';
  total += s.length;
}
console.log(total);
