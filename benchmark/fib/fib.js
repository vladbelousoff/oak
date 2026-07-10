function fib(n) {
  if (n < 2) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

let total = 0;
for (let rep = 0; rep < 40; rep += 1) {
  total += fib(30);
}
console.log(total);
