timemachine 1
iterations	20000
seed	1
width	A	290
width	B	80
width	C	80
width	D	80
width	E	90
format	D4	0.0%
format	E4	0.0%
format	D5	0.0%
format	E5	0.0%
format	B7	0.0%
format	B8	0%
format	B9	0.0%
format	C9	0.0%
format	B11	0
format	B12	0
format	D16	0.0%
format	E16	0.0%
format	D17	0.0%
format	E17	0.0%
format	D18	0.0%
format	E18	0.0%
format	D19	0.0%
format	E19	0.0%
format	D20	0.0%
format	E20	0.0%
format	D21	0.0%
format	E21	0.0%
format	D22	0.0%
format	E22	0.0%
format	D23	0.0%
format	E23	0.0%
cell	A1	Is the new page better? Rates learnt from counts, by Bayes' rule
cell	A3	Page
cell	B3	Visitors
cell	C3	Sign-ups
cell	D3	Rate
cell	E3	Rate, a future
cell	A4	A, the old page
cell	B4	2400
cell	C4	120
cell	D4	=C4/B4
cell	E4	=RAND.PROPORTION(C4,B4)
cell	A5	B, the new page
cell	B5	2380
cell	C5	145
cell	D5	=C5/B5
cell	E5	=RAND.PROPORTION(C5,B5)
cell	A7	Lift of B over A, this future
cell	B7	=E5/E4-1
cell	A8	Chance B is really the better page (F5)
cell	B8	=SIM.PROB(B7,">0")
cell	A9	Lift, 90% interval
cell	B9	=SIM.PERCENTILE(B7,0.05)
cell	C9	=SIM.PERCENTILE(B7,0.95)
cell	A10	Sign-ups from the next 10,000 visitors: A, B
cell	B10	=RAND.PROPORTION(C4,B4,10000)
cell	C10	=RAND.PROPORTION(C5,B5,10000)
cell	A11	Expected extra sign-ups from choosing B
cell	B11	=SIM.MEAN(C10)-SIM.MEAN(B10)
cell	A12	Most that testing longer could be worth (sign-ups)
cell	B12	=SIM.EVPI(B10,C10)
cell	A14	Eight landing pages: which is really best? Small samples, pulled to the group
cell	A15	Page
cell	B15	Visitors
cell	C15	Sign-ups
cell	D15	Raw rate
cell	E15	Pooled rate
cell	A16	Spring sale
cell	B16	1200
cell	C16	66
cell	D16	=C16/B16
cell	E16	=SHRINK.RATE(C16,B16,$C$16:$C$23,$B$16:$B$23)
cell	A17	Free trial
cell	B17	950
cell	C17	95
cell	D17	=C17/B17
cell	E17	=SHRINK.RATE(C17,B17,$C$16:$C$23,$B$16:$B$23)
cell	A18	Webinar
cell	B18	40
cell	C18	5
cell	D18	=C18/B18
cell	E18	=SHRINK.RATE(C18,B18,$C$16:$C$23,$B$16:$B$23)
cell	A19	Pricing
cell	B19	2100
cell	C19	63
cell	D19	=C19/B19
cell	E19	=SHRINK.RATE(C19,B19,$C$16:$C$23,$B$16:$B$23)
cell	A20	Case study
cell	B20	310
cell	C20	12
cell	D20	=C20/B20
cell	E20	=SHRINK.RATE(C20,B20,$C$16:$C$23,$B$16:$B$23)
cell	A21	Newsletter
cell	B21	1800
cell	C21	144
cell	D21	=C21/B21
cell	E21	=SHRINK.RATE(C21,B21,$C$16:$C$23,$B$16:$B$23)
cell	A22	Demo video
cell	B22	25
cell	C22	4
cell	D22	=C22/B22
cell	E22	=SHRINK.RATE(C22,B22,$C$16:$C$23,$B$16:$B$23)
cell	A23	Checklist
cell	B23	640
cell	C23	45
cell	D23	=C23/B23
cell	E23	=SHRINK.RATE(C23,B23,$C$16:$C$23,$B$16:$B$23)
cell	A25	The demo video's 16% is four sign-ups in twenty-five: the pooled rate
cell	A26	trusts it only as far as twenty-five visitors deserve.
cell	A28	Support tickets: 37 in the last 12 weeks. How many in the next 4?
cell	A29	Tickets in the next four weeks, one future
cell	B29	=RAND.RATE(37,12,4)
cell	A30	Enough staff for nine futures in ten: plan for
cell	B30	=SIM.PERCENTILE(B29,0.9)
