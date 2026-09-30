timemachine 1
iterations	4000
seed	1
width	A	170
width	B	70
width	C	70
width	D	70
width	E	70
width	F	70
width	G	70
width	H	70
width	I	70
width	J	70
map	world	maps/world.geojson
format	B7	0
format	C7	0
format	D7	0
format	E7	0
format	F7	0
format	G7	0
format	H7	0
format	I7	0
format	J7	0
format	B8	0
format	C8	0
format	D8	0
format	E8	0
format	F8	0
format	G8	0
format	H8	0
format	I8	0
format	J8	0
format	B9	0
format	C9	0
format	D9	0
format	E9	0
format	F9	0
format	G9	0
format	H9	0
format	I9	0
format	J9	0
format	B10	0.00
format	C10	0.00
format	D10	0.00
format	E10	0.00
format	F10	0.00
format	G10	0.00
format	H10	0.00
format	I10	0.00
format	J10	0.00
format	B11	0.00
format	C11	0.00
format	D11	0.00
format	E11	0.00
format	F11	0.00
format	G11	0.00
format	H11	0.00
format	I11	0.00
format	J11	0.00
format	B13	0
format	C13	0
format	D13	0
format	E13	0
format	F13	0
format	G13	0
format	H13	0
format	I13	0
format	J13	0
format	B15	0
format	B16	0%
format	B19	0%
format	B20	0%
format	B21	0%
format	B22	0%
format	B23	0%
format	B24	0%
format	B25	0%
cell	A1	Where will the hurricane go? The official track and its known errors
cell	A3	Hours ahead
cell	B3	0
cell	C3	12
cell	D3	24
cell	E3	36
cell	F3	48
cell	G3	60
cell	H3	72
cell	I3	96
cell	J3	120
cell	A4	Official track, lat
cell	B4	18.5
cell	C4	19.2
cell	D4	20.0
cell	E4	20.9
cell	F4	21.9
cell	G4	22.85
cell	H4	23.8
cell	I4	25.6
cell	J4	27.5
cell	A5	Official track, lon
cell	B5	-62.0
cell	C5	-63.6
cell	D5	-65.3
cell	E5	-67.1
cell	F5	-69.0
cell	G5	-70.95
cell	H5	-72.9
cell	I5	-76.6
cell	J5	-79.8
cell	A6	Cone radius (n mi)
cell	B6	0
cell	C6	25
cell	D6	39
cell	E6	49
cell	F6	62
cell	G6	77
cell	H6	95
cell	I6	134
cell	J6	200
cell	A7	Error sd each way (km)
cell	B7	=B6*1.852/1.4823
cell	C7	=C6*1.852/1.4823
cell	D7	=D6*1.852/1.4823
cell	E7	=E6*1.852/1.4823
cell	F7	=F6*1.852/1.4823
cell	G7	=G6*1.852/1.4823
cell	H7	=H6*1.852/1.4823
cell	I7	=I6*1.852/1.4823
cell	J7	=J6*1.852/1.4823
cell	A8	Error east (km)
cell	B8	0
cell	C8	=B8+RAND.NORMAL(0,SQRT(C7^2-B7^2))
cell	D8	=C8+RAND.NORMAL(0,SQRT(D7^2-C7^2))
cell	E8	=D8+RAND.NORMAL(0,SQRT(E7^2-D7^2))
cell	F8	=E8+RAND.NORMAL(0,SQRT(F7^2-E7^2))
cell	G8	=F8+RAND.NORMAL(0,SQRT(G7^2-F7^2))
cell	H8	=G8+RAND.NORMAL(0,SQRT(H7^2-G7^2))
cell	I8	=H8+RAND.NORMAL(0,SQRT(I7^2-H7^2))
cell	J8	=I8+RAND.NORMAL(0,SQRT(J7^2-I7^2))
cell	A9	Error north (km)
cell	B9	0
cell	C9	=B9+RAND.NORMAL(0,SQRT(C7^2-B7^2))
cell	D9	=C9+RAND.NORMAL(0,SQRT(D7^2-C7^2))
cell	E9	=D9+RAND.NORMAL(0,SQRT(E7^2-D7^2))
cell	F9	=E9+RAND.NORMAL(0,SQRT(F7^2-E7^2))
cell	G9	=F9+RAND.NORMAL(0,SQRT(G7^2-F7^2))
cell	H9	=G9+RAND.NORMAL(0,SQRT(H7^2-G7^2))
cell	I9	=H9+RAND.NORMAL(0,SQRT(I7^2-H7^2))
cell	J9	=I9+RAND.NORMAL(0,SQRT(J7^2-I7^2))
cell	A10	Storm lat
cell	B10	=B4+B9/111.2
cell	C10	=C4+C9/111.2
cell	D10	=D4+D9/111.2
cell	E10	=E4+E9/111.2
cell	F10	=F4+F9/111.2
cell	G10	=G4+G9/111.2
cell	H10	=H4+H9/111.2
cell	I10	=I4+I9/111.2
cell	J10	=J4+J9/111.2
cell	A11	Storm lon
cell	B11	=B5+B8/(111.2*COS(B10*PI()/180))
cell	C11	=C5+C8/(111.2*COS(C10*PI()/180))
cell	D11	=D5+D8/(111.2*COS(D10*PI()/180))
cell	E11	=E5+E8/(111.2*COS(E10*PI()/180))
cell	F11	=F5+F8/(111.2*COS(F10*PI()/180))
cell	G11	=G5+G8/(111.2*COS(G10*PI()/180))
cell	H11	=H5+H8/(111.2*COS(H10*PI()/180))
cell	I11	=I5+I8/(111.2*COS(I10*PI()/180))
cell	J11	=J5+J8/(111.2*COS(J10*PI()/180))
cell	A12	Over
cell	B12	=IFERROR(MAP.REGION("world",B10,B11),"sea")
cell	C12	=IFERROR(MAP.REGION("world",C10,C11),"sea")
cell	D12	=IFERROR(MAP.REGION("world",D10,D11),"sea")
cell	E12	=IFERROR(MAP.REGION("world",E10,E11),"sea")
cell	F12	=IFERROR(MAP.REGION("world",F10,F11),"sea")
cell	G12	=IFERROR(MAP.REGION("world",G10,G11),"sea")
cell	H12	=IFERROR(MAP.REGION("world",H10,H11),"sea")
cell	I12	=IFERROR(MAP.REGION("world",I10,I11),"sea")
cell	J12	=IFERROR(MAP.REGION("world",J10,J11),"sea")
cell	A13	Distance to Miami (km)
cell	B13	=GEO.DISTANCE(B10,B11,25.76,-80.19)
cell	C13	=GEO.DISTANCE(C10,C11,25.76,-80.19)
cell	D13	=GEO.DISTANCE(D10,D11,25.76,-80.19)
cell	E13	=GEO.DISTANCE(E10,E11,25.76,-80.19)
cell	F13	=GEO.DISTANCE(F10,F11,25.76,-80.19)
cell	G13	=GEO.DISTANCE(G10,G11,25.76,-80.19)
cell	H13	=GEO.DISTANCE(H10,H11,25.76,-80.19)
cell	I13	=GEO.DISTANCE(I10,I11,25.76,-80.19)
cell	J13	=GEO.DISTANCE(J10,J11,25.76,-80.19)
cell	A15	Closest to Miami (km)
cell	B15	=MIN(B13:J13)
cell	D15	Select A10:J11 for the tracks on the map;
cell	A16	Chance within 100 km of Miami
cell	B16	=SIM.PROB(B15,"<=100")
cell	D16	a row of the storm's lat for its fan; J12 for where it ends up.
cell	A18	Chance the centre crosses (F5)
cell	A19	Puerto Rico
cell	B19	=SIM.MEAN(C19)
cell	C19	=COUNTIF($B$12:$J$12,A19)>0
cell	A20	Dominican Rep.
cell	B20	=SIM.MEAN(C20)
cell	C20	=COUNTIF($B$12:$J$12,A20)>0
cell	A21	Haiti
cell	B21	=SIM.MEAN(C21)
cell	C21	=COUNTIF($B$12:$J$12,A21)>0
cell	A22	Cuba
cell	B22	=SIM.MEAN(C22)
cell	C22	=COUNTIF($B$12:$J$12,A22)>0
cell	A23	Bahamas
cell	B23	=SIM.MEAN(C23)
cell	C23	=COUNTIF($B$12:$J$12,A23)>0
cell	A24	United States of America
cell	B24	=SIM.MEAN(C24)
cell	C24	=COUNTIF($B$12:$J$12,A24)>0
cell	A25	Jamaica
cell	B25	=SIM.MEAN(C25)
cell	C25	=COUNTIF($B$12:$J$12,A25)>0
cell	A27	Likeliest place at 120 h
cell	B27	=SIM.MODE(J12)
