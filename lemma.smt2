; benchmark generated from python API
(set-info :status unknown)
(assert
 (forall ((dummy1 Int) (dummy2 Int) )(=> (or (= dummy1 dummy2) (= dummy1 0)) (= (* dummy1 dummy2) (* dummy1 dummy1))))
 )
(assert
 (forall ((dummy1 Int) (dummy2 Int) )(let (($x65 (>= dummy2 0)))
 (let (($x71 (and (not (and (>= dummy2 1) (<= (- dummy1 dummy2) (- 2)))) $x65 (not (<= dummy2 dummy1)))))
 (=> $x71 (or (= dummy2 (+ dummy1 1)) (< dummy1 0))))))
 )
(assert
 (forall ((dummy1 Int) (dummy2 Int) )(let ((?x91 (* dummy1 (+ (+ 1 (* (* 2 dummy1) dummy1)) (* 3 dummy1)))))
 (= (* 6 (div ?x91 6)) ?x91)))
 )
(assert
 (forall ((dummy1 Int) (dummy2 Int) )(let ((?x17 (* dummy1 dummy1)))
 (let ((?x102 (* ?x17 (+ (+ 1 ?x17) (* 2 dummy1)))))
 (= (* 4 (div ?x102 4)) ?x102))))
 )
(assert
 (forall ((dummy1 Int) (dummy2 Int) )(let ((?x151 (* (* (* (* (* 6 dummy1) dummy1) dummy1) dummy1) dummy1)))
 (let ((?x152 (* (- 1) dummy1)))
 (let ((?x155 (+ (+ (+ ?x152 ?x151) (* (* (* 10 dummy1) dummy1) dummy1)) (* (* (* (* 15 dummy1) dummy1) dummy1) dummy1))))
 (= (* 30 (div ?x155 30)) ?x155)))))
 )
(assert
 (forall ((dummy1 Int) (dummy2 Int) )(let ((?x151 (* (* (* (* (* 6 dummy1) dummy1) dummy1) dummy1) dummy1)))
 (let ((?x193 (+ (* (* (- 1) dummy1) dummy1) (* (* (* (* (* (* 2 dummy1) dummy1) dummy1) dummy1) dummy1) dummy1))))
 (let ((?x195 (+ (+ ?x193 (* (* (* (* 5 dummy1) dummy1) dummy1) dummy1)) ?x151)))
 (= (* 12 (div ?x195 12)) ?x195)))))
 )
(assert
 (forall ((dummy1 Int) (dummy2 Int) )(let ((?x162 (* dummy1 (+ 1 dummy1))))
(= (* 2 (div ?x162 2)) ?x162)))
)