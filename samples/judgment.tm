timemachine 1
iterations	10000
seed	1
width	A	300
width	E	120
format	B4	0%
format	C4	0%
format	D4	0%
format	E4	0%
format	B5	0%
format	C5	0%
format	D5	0%
format	E5	0%
format	B6	0%
format	C6	0%
format	D6	0%
format	E6	0%
format	B7	0%
format	C7	0%
format	D7	0%
format	E7	0%
format	B8	0%
format	C8	0%
format	D8	0%
format	E8	0%
format	B9	0%
format	C9	0%
format	D9	0%
format	E9	0%
format	B10	0%
format	C10	0%
format	D10	0%
format	E10	0%
format	B11	0%
format	C11	0%
format	D11	0%
format	E11	0%
format	B13	0.000
format	C13	0.000
format	D13	0.000
format	E13	0.000
format	B14	0.000
format	C14	0.000
format	D14	0.000
format	E14	0.000
format	B21	0.0%
format	B22	0.0%
format	B28	0.0%
format	B29	0.0%
format	B30	0.0%
cell	A1	Keeping score: probabilities, outcomes and updating
cell	A3	Question
cell	B3	Alice
cell	C3	Bob
cell	D3	Crowd
cell	E3	Crowd, bolder
cell	F3	Happened?
cell	A4	Will the product ship by June?
cell	B4	0.8
cell	C4	0.6
cell	D4	=AVERAGE(B4:C4)
cell	E4	=EXTREMIZE(D4)
cell	F4	1
cell	A5	Will the rate rise this quarter?
cell	B5	0.3
cell	C5	0.55
cell	D5	=AVERAGE(B5:C5)
cell	E5	=EXTREMIZE(D5)
cell	F5	0
cell	A6	Will the rival raise prices?
cell	B6	0.65
cell	C6	0.7
cell	D6	=AVERAGE(B6:C6)
cell	E6	=EXTREMIZE(D6)
cell	F6	1
cell	A7	Will the pilot beat its target?
cell	B7	0.4
cell	C7	0.2
cell	D7	=AVERAGE(B7:C7)
cell	E7	=EXTREMIZE(D7)
cell	F7	0
cell	A8	Will the hire accept the offer?
cell	B8	0.9
cell	C8	0.75
cell	D8	=AVERAGE(B8:C8)
cell	E8	=EXTREMIZE(D8)
cell	F8	1
cell	A9	Will the vote pass?
cell	B9	0.55
cell	C9	0.35
cell	D9	=AVERAGE(B9:C9)
cell	E9	=EXTREMIZE(D9)
cell	F9	1
cell	A10	Will the supplier be late?
cell	B10	0.25
cell	C10	0.4
cell	D10	=AVERAGE(B10:C10)
cell	E10	=EXTREMIZE(D10)
cell	F10	0
cell	A11	Will the trial succeed?
cell	B11	0.15
cell	C11	0.3
cell	D11	=AVERAGE(B11:C11)
cell	E11	=EXTREMIZE(D11)
cell	F11	0
cell	A13	Brier score (0 is perfect, 0.25 a coin)
cell	B13	=BRIER(B4:B11,$F$4:$F$11)
cell	C13	=BRIER(C4:C11,$F$4:$F$11)
cell	D13	=BRIER(D4:D11,$F$4:$F$11)
cell	E13	=BRIER(E4:E11,$F$4:$F$11)
cell	A14	Log score (lower is better)
cell	B14	=LOGSCORE(B4:B11,$F$4:$F$11)
cell	C14	=LOGSCORE(C4:C11,$F$4:$F$11)
cell	D14	=LOGSCORE(D4:D11,$F$4:$F$11)
cell	E14	=LOGSCORE(E4:E11,$F$4:$F$11)
cell	A17	Updating on evidence
cell	A18	Prior: chance the launch slips
cell	B18	30%
cell	A19	Chance of a bad status report if it slips
cell	B19	80%
cell	A20	... and if it is on time
cell	B20	20%
cell	A21	After one bad report
cell	B21	=BAYES(B18,B19,B20)
cell	A22	After a second
cell	B22	=BAYES(B21,B19,B20)
cell	A24	Uncertain events, simulated
cell	A25	Launch slips?
cell	B25	=RAND.BERNOULLI(B18)
cell	A26	Report is bad?
cell	B26	=RAND.BERNOULLI(IF(B25,B19,B20))
cell	A27	Slipped, among futures with a bad report (F5)
cell	B27	=B25*B26
cell	A28	Chance of a bad report
cell	B28	=SIM.MEAN(B26)
cell	A29	Chance of slip and bad report
cell	B29	=SIM.MEAN(B27)
cell	A30	So, slipped given a bad report
cell	B30	=B29/B28
cell	C30	simulation agrees with Bayes in B21
