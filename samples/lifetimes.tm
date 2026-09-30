timemachine 1
iterations	10000
seed	1
width	A	60
width	B	70
width	C	60
width	E	380
width	F	90
format	F4	0.00
format	F5	#,##0
format	F6	0%
format	F7	0%
format	F10	#,##0
format	F11	0%
format	F12	#,##0
format	F13	#,##0
format	F16	0%
format	F17	0.0
format	F20	0.0
format	F21	0.0
format	F22	0.0
cell	A1	How long will the pumps last? Lifetimes, some of them not over yet
cell	A3	Pump
cell	B3	Hours
cell	C3	Failed
cell	E3	Failed 0: still running after so many hours -- a lifetime known only to be longer.
cell	A4	#1
cell	B4	4743
cell	C4	1
cell	E4	Weibull shape (above 1: they wear out)
cell	F4	=WEIBULL.FIT(B4:B23,"shape",C4:C23)
cell	A5	#2
cell	B5	4484
cell	C5	1
cell	E5	Weibull scale (hours)
cell	F5	=WEIBULL.FIT(B4:B23,"scale",C4:C23)
cell	A6	#3
cell	B6	5390
cell	C6	0
cell	E6	Share still running at 5,000 hours: Kaplan-Meier
cell	F6	=KAPLAN.MEIER(5000,B4:B23,C4:C23)
cell	A7	#4
cell	B7	3959
cell	C7	0
cell	E7	... by the fitted Weibull
cell	F7	=1-WEIBULL.DIST(5000,F4,F5,TRUE)
cell	A8	#5
cell	B8	1596
cell	C8	0
cell	A9	#6
cell	B9	2115
cell	C9	1
cell	E9	Our pump has run (hours)
cell	F9	3000
cell	A10	#7
cell	B10	3460
cell	C10	1
cell	E10	The hour it fails, one future
cell	F10	=RAND.WEIBULL(F4,F5,F9)
cell	A11	#8
cell	B11	8998
cell	C11	0
cell	E11	Chance it fails in the 2,000 hours of warranty left (F5)
cell	F11	=SIM.PROB(F10,"<"&(F9+2000))
cell	A12	#9
cell	B12	3908
cell	C12	0
cell	E12	Hours it has left, even odds
cell	F12	=SIM.MEDIAN(F10)-F9
cell	A13	#10
cell	B13	6329
cell	C13	1
cell	E13	A lifetime straight from the record, one future
cell	F13	=RAND.SURVIVAL(B4:B23,C4:C23)
cell	A14	#11
cell	B14	1760
cell	C14	0
cell	A15	#12
cell	B15	3355
cell	C15	1
cell	E15	Repairs: a failure every 40 hours, 60 hours to mend, three fitters
cell	A16	#13
cell	B16	3457
cell	C16	1
cell	E16	Chance a failed pump waits for a fitter
cell	F16	=ERLANG.C(1/40,1/60,3)
cell	A17	#14
cell	B17	3953
cell	C17	0
cell	E17	Mean wait (hours)
cell	F17	=ERLANG.C(1/40,1/60,3,"wait")
cell	A18	#15
cell	B18	4305
cell	C18	1
cell	A19	#16
cell	B19	7609
cell	C19	0
cell	E19	With nothing to go on: a firm that has lasted 12 years (Gott's rule)
cell	A20	#17
cell	B20	1071
cell	C20	1
cell	E20	Years it lasts yet, one future
cell	F20	=RAND.LINDY(12)
cell	A21	#18
cell	B21	8284
cell	C21	1
cell	E21	Even odds of lasting another (years)
cell	F21	=SIM.MEDIAN(F20)
cell	A22	#19
cell	B22	5080
cell	C22	1
cell	E22	Nine chances in ten of lasting another (years)
cell	F22	=SIM.PERCENTILE(F20,0.1)
cell	A23	#20
cell	B23	2226
cell	C23	1
