timemachine 1
iterations	10000
seed	1
width	A	60
width	B	100
width	C	80
width	D	130
width	E	24
width	F	330
width	G	70
image	week1	pictures/field-week1.png
image	week2	pictures/field-week2.png
image	week3	pictures/field-week3.png
image	week4	pictures/field-week4.png
image	week5	pictures/field-week5.png
image	week6	pictures/field-week6.png
format	G3	0%
format	B4	0%
format	C4	0.00
format	D4	0%
format	G4	0%
format	B5	0%
format	C5	0.00
format	D5	0%
format	G5	0%
format	B6	0%
format	C6	0.00
format	D6	0%
format	G6	0.0
format	B7	0%
format	C7	0.00
format	D7	0%
format	B8	0%
format	C8	0.00
format	D8	0%
format	B9	0%
format	C9	0.00
format	D9	0%
format	D10	0%
format	D11	0%
format	D12	0%
format	D13	0%
cell	A1	When will the canopy close? Green cover measured from weekly photographs
cell	A3	Week
cell	B3	Green cover
cell	C3	Log-odds
cell	D3	Cover, one future
cell	F3	Cover in week 8, straight-line log-odds
cell	G3	=1/(1+EXP(-FORECAST.LINEAR(8,C4:C9,A4:A9)))
cell	A4	1
cell	B4	=IMAGE.FRACTION("week1","exg",">0.1")
cell	C4	=LN(B4/(1-B4))
cell	D4	=B4
cell	F4	Chance the canopy has closed (80%) by week 8
cell	G4	=SIM.PROB(D11,">=0.8")
cell	A5	2
cell	B5	=IMAGE.FRACTION("week2","exg",">0.1")
cell	C5	=LN(B5/(1-B5))
cell	D5	=B5
cell	F5	... by week 10
cell	G5	=SIM.PROB(D13,">=0.8")
cell	A6	3
cell	B6	=IMAGE.FRACTION("week3","exg",">0.1")
cell	C6	=LN(B6/(1-B6))
cell	D6	=B6
cell	F6	Week the log-odds reach 80%
cell	G6	=(LN(0.8/0.2)-INTERCEPT(C4:C9,A4:A9))/SLOPE(C4:C9,A4:A9)
cell	A7	4
cell	B7	=IMAGE.FRACTION("week4","exg",">0.1")
cell	C7	=LN(B7/(1-B7))
cell	D7	=B7
cell	A8	5
cell	B8	=IMAGE.FRACTION("week5","exg",">0.1")
cell	C8	=LN(B8/(1-B8))
cell	D8	=B8
cell	F8	Select D4:D13 for the cover measured, then forecast, as a fan.
cell	A9	6
cell	B9	=IMAGE.FRACTION("week6","exg",">0.1")
cell	C9	=LN(B9/(1-B9))
cell	D9	=B9
cell	F9	Each week's cover is read straight off its photograph;
cell	A10	7
cell	D10	=1/(1+EXP(-RAND.LINEAR(A10,$C$4:$C$9,$A$4:$A$9)))
cell	F10	Data > Pictures and Maps lists the six of them.
cell	A11	8
cell	D11	=1/(1+EXP(-RAND.LINEAR(A11,$C$4:$C$9,$A$4:$A$9)))
cell	A12	9
cell	D12	=1/(1+EXP(-RAND.LINEAR(A12,$C$4:$C$9,$A$4:$A$9)))
cell	A13	10
cell	D13	=1/(1+EXP(-RAND.LINEAR(A13,$C$4:$C$9,$A$4:$A$9)))
