timemachine 1
iterations	10000
seed	1
width	A	240
width	B	80
width	C	80
width	D	90
image	week1	pictures/field-week1.png
image	week2	pictures/field-week2.png
image	week3	pictures/field-week3.png
image	week4	pictures/field-week4.png
image	week5	pictures/field-week5.png
image	week6	pictures/field-week6.png
format	B4	0%
format	C4	0.00
format	B5	0%
format	C5	0.00
format	B6	0%
format	C6	0.00
format	B7	0%
format	C7	0.00
format	B8	0%
format	C8	0.00
format	B9	0%
format	C9	0.00
format	B11	0%
format	B12	0%
format	B13	0%
format	B14	0.0
cell	A1	When will the canopy close? Green cover measured from weekly photographs
cell	A3	Week
cell	B3	Green cover
cell	C3	Log-odds
cell	A4	1
cell	B4	=IMAGE.FRACTION("week1","exg",">0.1")
cell	C4	=LN(B4/(1-B4))
cell	A5	2
cell	B5	=IMAGE.FRACTION("week2","exg",">0.1")
cell	C5	=LN(B5/(1-B5))
cell	A6	3
cell	B6	=IMAGE.FRACTION("week3","exg",">0.1")
cell	C6	=LN(B6/(1-B6))
cell	A7	4
cell	B7	=IMAGE.FRACTION("week4","exg",">0.1")
cell	C7	=LN(B7/(1-B7))
cell	A8	5
cell	B8	=IMAGE.FRACTION("week5","exg",">0.1")
cell	C8	=LN(B8/(1-B8))
cell	A9	6
cell	B9	=IMAGE.FRACTION("week6","exg",">0.1")
cell	C9	=LN(B9/(1-B9))
cell	A11	Cover in week 8, straight-line log-odds
cell	B11	=1/(1+EXP(-FORECAST.LINEAR(8,C4:C9,A4:A9)))
cell	A12	Cover in week 8, one future
cell	B12	=1/(1+EXP(-RAND.LINEAR(8,C4:C9,A4:A9)))
cell	A13	Chance the canopy has closed (80%) by week 8
cell	B13	=SIM.PROB(B12,">=0.8")
cell	A14	Week the log-odds reach 80%
cell	B14	=(LN(0.8/0.2)-INTERCEPT(C4:C9,A4:A9))/SLOPE(C4:C9,A4:A9)
cell	A16	Each week's cover is read straight off its photograph; the pictures are in
cell	A17	samples/pictures, and Data > Pictures and Maps lists them.
