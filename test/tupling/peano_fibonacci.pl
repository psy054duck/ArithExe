fib(0,0).
fib(s(0),1).
fib(s(s(N)),F) :- fib(s(N),A), fib(N,B), F is A+B.
