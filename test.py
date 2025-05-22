fml = '''; 
(set-info :status unknown)
(declare-fun ari_N () Int)
(declare-fun k!60 () Int)
(assert
 (let (($x215 (= k!60 ari_N)))
(let (($x94 (<= 50 ari_N)))
(let (($x212 (or $x94 $x215)))
(let (($x239 (not $x94)))
(let (($x214 (or $x239 (= k!60 (+ 100 (* (- 1) ari_N))))))
(let (($x134 (>= ari_N 0)))
(let (($x401 (or (and $x94 (not (<= 1 k!60))) (and $x239 (not (<= (- 1) k!60))))))
(let (($x390 (or (and (<= ari_N 50) (<= ari_N 0)) (and (>= ari_N 51) (>= ari_N 101)))))
(let (($x383 (>= ari_N 51)))
(let (($x357 (or (and (<= ari_N 50) (<= ari_N (- 2))) (and $x383 (>= ari_N 103)))))
(let (($x386 (<= ari_N 50)))
(let (($x158 (not (and (>= ari_N 1) (or (and $x386 $x357) (and $x383 $x390))))))
(and $x158 $x401 $x134 $x214 $x212))))))))))))))
(check-sat)'''

atoms = '''
; 
(set-info :status unknown)
(declare-fun ari_N () Int)
(declare-fun k!60 () Int)
(assert
 (>= ari_N 1))
(assert
 (<= ari_N 50))
(assert
 (<= ari_N (- 2)))
(assert
 (>= ari_N 51))
(assert
 (>= ari_N 103))
(assert
 (<= ari_N 0))
(assert
 (>= ari_N 101))
(assert
 (<= 50 ari_N))
(assert
 (<= 1 k!60))
(assert
 (<= (- 1) k!60))
(assert
 (>= ari_N 0))
(assert
 (let ((?x93 (+ 100 (* (- 1) ari_N))))
 (<= k!60 ?x93)))
(assert
 (let ((?x93 (+ 100 (* (- 1) ari_N))))
 (>= k!60 ?x93)))
(assert
 (<= k!60 ari_N))
(assert
 (>= k!60 ari_N))
(check-sat)
'''

from z3 import *

s_fml = Solver()
s_fml.from_string(fml)

s_atoms = Solver()
s_atoms.from_string(atoms)

print(s_fml.assertions())
print('******')
print(s_atoms.assertions())