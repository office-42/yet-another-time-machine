timemachine 1
iterations	10000
seed	1
width	A	230
width	B	110
width	C	330
cell	A1	Product launch: will it make money?
cell	A3	Inputs, each a range not a number
cell	B3	Draw
cell	C3	How it was estimated
cell	A4	Market size (units a year)
cell	B4	=RAND.LOGCI(80000,300000)
cell	C4	90% sure it is between 80k and 300k
cell	A5	Market share
cell	B5	=RAND.PERT(5%,12%,25%)
cell	C5	at least 5%, most likely 12%, at most 25%
cell	A6	Price
cell	B6	=RAND.TRIANGULAR(39,49,55)
cell	C6	discounting is likelier than a premium
cell	A7	Unit cost
cell	B7	=RAND.NORMAL(22,2.5)
cell	C7	the supplier's quote, give or take
cell	A8	Fixed costs
cell	B8	=RAND.PERT(250000,300000,420000)
cell	C8	overruns run long
cell	A9	Does a competitor launch too?
cell	B9	=RAND.BERNOULLI(30%)
cell	C9	1 in the futures where they do: a 30% chance
cell	A10	Share lost if they do
cell	B10	35%
cell	C10	a plain assumption: certain, so not tinted
cell	A12	Units sold
cell	B12	=B4*B5*(1-B9*B10)
cell	A13	Revenue
cell	B13	=B12*B6
cell	A14	Profit
cell	B14	=B12*(B6-B7)-B8
cell	C14	select this cell after pressing F5
cell	A16	What the futures say (press F5)
cell	A17	Expected profit
cell	B17	=SIM.MEAN(B14)
cell	A18	Chance of a loss
cell	B18	=SIM.PROB(B14,"<0")
cell	A19	Profit 9 futures in 10 beat
cell	B19	=SIM.PERCENTILE(B14,0.1)
cell	A20	Profit 1 future in 10 beats
cell	B20	=SIM.PERCENTILE(B14,0.9)
cell	A21	Average of the worst 5%
cell	B21	=SIM.TAILMEAN(B14,5%)
cell	A22	Profit if every input were its average
cell	B22	=(SIM.MEAN(B4)*SIM.MEAN(B5)*(1-SIM.MEAN(B9)*B10))*(SIM.MEAN(B6)-SIM.MEAN(B7))-SIM.MEAN(B8)
cell	C22	the plan from averages: not the average outcome
cell	A24	What drives profit (rank correlation)
cell	A25	Market size
cell	B25	=SIM.CORREL(B4,$B$14)
cell	A26	Market share
cell	B26	=SIM.CORREL(B5,$B$14)
cell	A27	Price
cell	B27	=SIM.CORREL(B6,$B$14)
cell	A28	Unit cost
cell	B28	=SIM.CORREL(B7,$B$14)
cell	A29	Fixed costs
cell	B29	=SIM.CORREL(B8,$B$14)
cell	A30	Competitor
cell	B30	=SIM.CORREL(B9,$B$14)
