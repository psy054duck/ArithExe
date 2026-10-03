; Exact path-expression certificate for true_unsigned_large_threshold.c.
; a: x := (x + 1) mod 2^32, enabled when x < 10,000,000.
; b: x := (x + 2) mod 2^32, enabled when 10,000,000 <= x < 100,000,000.
; Candidate trace: a^10000000 b^45000000.
; Each check seeks a counterexample to one certificate obligation.
; All seven checks must return unsat. No quantified first-break search is used.
(set-logic QF_LIA)
(set-option :timeout 3000)
(define-fun modulus () Int 4294967296)
(define-fun ka () Int 10000000)
(define-fun kb () Int 45000000)
(define-fun a ((t Int)) Int t)
(define-fun b ((t Int)) Int (+ ka (* 2 t)))
(declare-const t Int)

; Phase a starts at the program's initial value.
(push)
(assert (not (= (a 0) 0)))
(check-sat)
(pop)

; Phase a's closed form agrees with the exact unsigned update at every step.
(push)
(assert (and (<= 0 t) (< t ka)
             (not (= (a (+ t 1)) (mod (+ (a t) 1) modulus)))))
(check-sat)
(pop)

; Every a iteration enters the loop and selects the first branch.
(push)
(assert (and (<= 0 t) (< t ka)
             (not (and (<= 0 (a t)) (< (a t) modulus)
                       (< (a t) 100000000) (< (a t) 10000000)))))
(check-sat)
(pop)

; The first divergence of a enters phase b, with matching initial state.
(push)
(assert (not (and (= (a ka) (b 0))
                  (<= 10000000 (a ka)) (< (a ka) 100000000))))
(check-sat)
(pop)

; Phase b's closed form agrees with the exact unsigned update at every step.
(push)
(assert (and (<= 0 t) (< t kb)
             (not (= (b (+ t 1)) (mod (+ (b t) 2) modulus)))))
(check-sat)
(pop)

; Every b iteration remains inside the loop and selects the second branch.
(push)
(assert (and (<= 0 t) (< t kb)
             (not (and (<= 0 (b t)) (< (b t) modulus)
                       (< (b t) 100000000) (<= 10000000 (b t))))))
(check-sat)
(pop)

; At b's first divergence the loop exits and the assertion holds.
(push)
(assert (not (and (= (b kb) 100000000)
                  (<= 100000000 (b kb)) (< (b kb) modulus))))
(check-sat)
(pop)
