timemachine 1
iterations	10000
seed	1
width	A	250
width	E	110
width	F	100
cell	A1	Project schedule: three-point estimates and the merge bias
cell	A3	Task
cell	B3	Optimistic
cell	C3	Likely
cell	D3	Pessimistic
cell	E3	Duration
cell	F3	PERT mean
cell	A4	Design
cell	B4	5
cell	C4	8
cell	D4	15
cell	E4	=RAND.PERT(B4,C4,D4)
cell	F4	=(B4+4*C4+D4)/6
cell	A5	Build the back end
cell	B5	10
cell	C5	15
cell	D5	30
cell	E5	=RAND.PERT(B5,C5,D5)
cell	F5	=(B5+4*C5+D5)/6
cell	A6	Build the front end
cell	B6	8
cell	C6	13
cell	D6	25
cell	E6	=RAND.PERT(B6,C6,D6)
cell	F6	=(B6+4*C6+D6)/6
cell	A7	Integration
cell	B7	3
cell	C7	5
cell	D7	12
cell	E7	=RAND.PERT(B7,C7,D7)
cell	F7	=(B7+4*C7+D7)/6
cell	A8	Testing
cell	B8	5
cell	C8	7
cell	D8	20
cell	E8	=RAND.PERT(B8,C8,D8)
cell	F8	=(B8+4*C8+D8)/6
cell	A9	Launch
cell	B9	2
cell	C9	3
cell	D9	6
cell	E9	=RAND.PERT(B9,C9,D9)
cell	F9	=(B9+4*C9+D9)/6
cell	A11	Total: back and front end side by side
cell	E11	=E4+MAX(E5,E6)+E7+E8+E9
cell	F11	=F4+MAX(F5,F6)+F7+F8+F9
cell	A13	Deadline (working days)
cell	E13	45
cell	A14	Chance of making it
cell	E14	=SIM.PROB(E11,"<="&E13)
cell	A15	Days to be 80% sure
cell	E15	=SIM.PERCENTILE(E11,0.8)
cell	A16	Expected duration
cell	E16	=SIM.MEAN(E11)
cell	A17	The plan from average task times
cell	E17	=F11
cell	A19	The average of the longer of two paths is more than the longer
cell	A20	of their averages: plans made from averages run late on average.
