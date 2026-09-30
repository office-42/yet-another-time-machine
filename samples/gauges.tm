timemachine 1
iterations	10000
seed	1
width	A	120
width	B	70
width	C	70
width	D	80
width	F	250
width	G	90
map	world	maps/world.geojson
format	G7	0.0
format	G8	0
format	G9	0.0
format	G10	0.0
format	G11	0.0
format	G12	0.0
format	G13	0%
cell	A1	Rain at the farm, between the gauges: interpolation with its error
cell	A3	Station
cell	B3	Lat
cell	C3	Lon
cell	D3	Rain (mm)
cell	F3	The farm
cell	A4	Abbots
cell	B4	51.827
cell	C4	0.325
cell	D4	10.2
cell	F4	Lat
cell	G4	52.15
cell	A5	Barrow
cell	B5	51.685
cell	C5	-1.105
cell	D5	10.5
cell	F5	Lon
cell	G5	-0.9
cell	A6	Coldham
cell	B6	52.599
cell	C6	-1.181
cell	D6	22.0
cell	A7	Denby
cell	B7	52.053
cell	C7	-0.61
cell	D7	9.4
cell	F7	Nearest gauge (mm)
cell	G7	=GEO.NEAREST(G4,G5,B4:B18,C4:C18,D4:D18)
cell	A8	Easton
cell	B8	51.792
cell	C8	0.061
cell	D8	9.3
cell	F8	Nearest gauge is (km) away
cell	G8	=GEO.NEAREST(G4,G5,B4:B18,C4:C18)
cell	A9	Fairley
cell	B9	51.62
cell	C9	-1.066
cell	D9	8.9
cell	F9	Inverse-distance estimate (mm)
cell	G9	=GEO.IDW(G4,G5,B4:B18,C4:C18,D4:D18)
cell	A10	Glenmore
cell	B10	52.008
cell	C10	0.204
cell	D10	7.4
cell	F10	Kriged estimate (mm)
cell	G10	=GEO.KRIGE(G4,G5,B4:B18,C4:C18,D4:D18)
cell	A11	Hatch
cell	B11	51.858
cell	C11	0.383
cell	D11	8.7
cell	F11	... give or take (sd, mm)
cell	G11	=GEO.KRIGE.SD(G4,G5,B4:B18,C4:C18,D4:D18)
cell	A12	Ivybridge
cell	B12	51.663
cell	C12	-0.36
cell	D12	5.9
cell	F12	Rain at the farm, one future
cell	G12	=MAX(0,RAND.KRIGE(G4,G5,B4:B18,C4:C18,D4:D18))
cell	A13	Jarrow
cell	B13	51.938
cell	C13	-0.217
cell	D13	10.4
cell	F13	Chance of more than 20 mm (F5)
cell	G13	=SIM.PROB(G12,">20")
cell	A14	Kelby
cell	B14	52.098
cell	C14	-0.301
cell	D14	11.5
cell	F14	Gauges within 25 km
cell	G14	=GEO.WITHIN(G4,G5,B4:B18,C4:C18,25)
cell	A15	Linton
cell	B15	51.742
cell	C15	-1.471
cell	D15	7.0
cell	A16	Marsh
cell	B16	52.546
cell	C16	-0.623
cell	D16	17.0
cell	F16	Select A3:D18 for the gauges on a map, coloured by rain.
cell	A17	Norton
cell	B17	52.179
cell	C17	-0.142
cell	D17	13.5
cell	A18	Oakley
cell	B18	52.481
cell	C18	-1.029
cell	D18	23.4
