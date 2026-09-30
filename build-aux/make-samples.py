#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Writes the example models in samples/.  Run it after changing one:
#
#     python3 build-aux/make-samples.py

import math, random, os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples")

def col(c):
    s = ""
    c += 1
    while c > 0:
        c -= 1
        s = chr(65 + c % 26) + s
        c //= 26
    return s

class Sheet:
    def __init__(self, iterations=10000, seed=1):
        self.cells = {}
        self.widths = {}
        self.formats = {}
        self.iterations = iterations
        self.seed = seed
    def set(self, ref, value):
        self.cells[ref] = str(value)
    def width(self, c, w):
        self.widths[c] = w
    def fmt(self, refs, code):
        for ref in refs.split():
            self.formats[ref] = code
    def save(self, name):
        def key(ref):
            letters = "".join(ch for ch in ref if ch.isalpha())
            digits = int("".join(ch for ch in ref if ch.isdigit()))
            n = 0
            for ch in letters:
                n = n * 26 + ord(ch) - 64
            return (digits, n)
        lines = ["timemachine 1", f"iterations\t{self.iterations}", f"seed\t{self.seed}"]
        for c, w in sorted(self.widths.items(), key=lambda kv: (len(kv[0]), kv[0])):
            lines.append(f"width\t{c}\t{w}")
        for ref in sorted(self.formats, key=key):
            lines.append(f"format\t{ref}\t{self.formats[ref]}")
        for ref in sorted(self.cells, key=key):
            v = self.cells[ref].replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")
            lines.append(f"cell\t{ref}\t{v}")
        with open(os.path.join(OUT, name), "w") as f:
            f.write("\n".join(lines) + "\n")

# ---- 1. Product launch ---------------------------------------------------
s = Sheet()
s.width("A", 230); s.width("B", 110); s.width("C", 330)
s.set("A1", "Product launch: will it make money?")
s.set("A3", "Inputs, each a range not a number"); s.set("B3", "Draw"); s.set("C3", "How it was estimated")
s.set("A4", "Market size (units a year)"); s.set("B4", "=RAND.LOGCI(80000,300000)"); s.set("C4", "90% sure it is between 80k and 300k")
s.set("A5", "Market share"); s.set("B5", "=RAND.PERT(5%,12%,25%)"); s.set("C5", "at least 5%, most likely 12%, at most 25%")
s.set("A6", "Price"); s.set("B6", "=RAND.TRIANGULAR(39,49,55)"); s.set("C6", "discounting is likelier than a premium")
s.set("A7", "Unit cost"); s.set("B7", "=RAND.NORMAL(22,2.5)"); s.set("C7", "the supplier's quote, give or take")
s.set("A8", "Fixed costs"); s.set("B8", "=RAND.PERT(250000,300000,420000)"); s.set("C8", "overruns run long")
s.set("A9", "Does a competitor launch too?"); s.set("B9", "=RAND.BERNOULLI(30%)"); s.set("C9", "1 in the futures where they do: a 30% chance")
s.set("A10", "Share lost if they do"); s.set("B10", "35%"); s.set("C10", "a plain assumption: certain, so not tinted")
s.set("A12", "Units sold"); s.set("B12", "=B4*B5*(1-B9*B10)")
s.set("A13", "Revenue"); s.set("B13", "=B12*B6")
s.set("A14", "Profit"); s.set("B14", "=B12*(B6-B7)-B8"); s.set("C14", "select this cell after pressing F5")
s.set("A16", "What the futures say (press F5)")
s.set("A17", "Expected profit"); s.set("B17", "=SIM.MEAN(B14)")
s.set("A18", "Chance of a loss"); s.set("B18", '=SIM.PROB(B14,"<0")')
s.set("A19", "Profit 9 futures in 10 beat"); s.set("B19", "=SIM.PERCENTILE(B14,0.1)")
s.set("A20", "Profit 1 future in 10 beats"); s.set("B20", "=SIM.PERCENTILE(B14,0.9)")
s.set("A21", "Average of the worst 5%"); s.set("B21", "=SIM.TAILMEAN(B14,5%)")
s.set("A22", "Profit if every input were its average"); s.set("B22", "=(SIM.MEAN(B4)*SIM.MEAN(B5)*(1-SIM.MEAN(B9)*B10))*(SIM.MEAN(B6)-SIM.MEAN(B7))-SIM.MEAN(B8)"); s.set("C22", "the plan from averages: not the average outcome")
s.set("A24", "What drives profit (rank correlation)")
for i, (label, ref) in enumerate([("Market size", "B4"), ("Market share", "B5"), ("Price", "B6"),
                                   ("Unit cost", "B7"), ("Fixed costs", "B8"), ("Competitor", "B9")]):
    r = 25 + i
    s.set(f"A{r}", label); s.set(f"B{r}", f"=SIM.CORREL({ref},$B$14)")
s.fmt("B4 B12", "#,##0")
s.fmt("B5 B10 B18", "0.0%")
s.fmt("B6 B7", "$0.00")
s.fmt("B8 B13 B14 B17 B19 B20 B21 B22", "$#,##0")
s.fmt("B25 B26 B27 B28 B29 B30", "+0.00;-0.00")
s.save("launch.tm")

# ---- 2. Sales forecast ---------------------------------------------------
rng = random.Random(7)
s = Sheet()
s.width("A", 70); s.width("B", 90); s.width("C", 110); s.width("D", 90); s.width("E", 120); s.width("G", 250); s.width("H", 110)
s.set("A1", "Monthly sales: three years of history, one year of forecast")
s.set("A3", "Month"); s.set("B3", "Sales"); s.set("C3", "Forecast (ETS)"); s.set("D3", "± 95%"); s.set("E3", "History & a future")
for t in range(1, 37):
    r = t + 3
    y = 1000 + 15 * t + 150 * math.sin(2 * math.pi * (t - 1) / 12) + rng.gauss(0, 40)
    s.set(f"A{r}", t); s.set(f"B{r}", round(y))
    s.set(f"E{r}", f"=B{r}")
for t in range(37, 49):
    r = t + 3
    s.set(f"A{r}", t)
    s.set(f"C{r}", f"=FORECAST.ETS(A{r},$B$4:$B$39,$A$4:$A$39)")
    s.set(f"D{r}", f"=FORECAST.ETS.CONFINT(A{r},$B$4:$B$39,$A$4:$A$39)")
    s.set(f"E{r}", f"=RAND.ETS(A{r},$B$4:$B$39,$A$4:$A$39)")
s.set("G3", "What the smoothing found")
s.set("G4", "Season length (months)"); s.set("H4", "=FORECAST.ETS.SEASONALITY(B4:B39,A4:A39)")
s.set("G5", "Last year's sales"); s.set("H5", "=SUM(B28:B39)")
s.set("G6", "Next year, point forecast"); s.set("H6", "=SUM(C40:C51)")
s.set("G7", "Next year, one future"); s.set("H7", "=SUM(E40:E51)")
s.set("G8", "Growth, point forecast"); s.set("H8", "=H6/H5-1")
s.set("G10", "After F5:")
s.set("G11", "Chance next year beats 20,000"); s.set("H11", '=SIM.PROB(H7,">20000")')
s.set("G12", "Next year, P10"); s.set("H12", "=SIM.PERCENTILE(H7,0.1)")
s.set("G13", "Next year, P90"); s.set("H13", "=SIM.PERCENTILE(H7,0.9)")
s.set("G15", "Select E4:E51 for the fan chart:")
s.set("G16", "history as a line, the forecast as a fan.")
s.set("G17", "Each month is drawn on its own, so the")
s.set("G18", "annual spread is if anything too narrow.")
s.fmt(" ".join(f"C{r} D{r} E{r}" for r in range(40, 52)), "#,##0")
s.fmt("H5 H6 H7 H12 H13", "#,##0")
s.fmt("H8 H11", "0.0%")
s.save("sales.tm")

# ---- 3. Retirement -------------------------------------------------------
s = Sheet()
s.width("A", 190)
s.set("A1", "Retirement savings: a random walk through 25 years of markets")
s.set("A3", "Savings today"); s.set("B3", 100000)
s.set("A4", "Saved each year"); s.set("B4", 12000)
s.set("A5", "Expected return (real)"); s.set("B5", "5%")
s.set("A6", "Volatility"); s.set("B6", "15%")
s.set("A7", "Goal"); s.set("B7", 1000000)
s.set("A9", "Year"); s.set("B9", 0)
s.set("A10", "Market return"); s.set("A11", "Savings"); s.set("B11", "=B3")
last = None
for y in range(1, 26):
    c = col(1 + y); p = col(y)
    s.set(f"{c}9", f"={p}9+1")
    # A lognormal year: geometric Brownian motion, one step a year.
    s.set(f"{c}10", "=EXP(RAND.NORMAL($B$5-$B$6^2/2,$B$6))-1")
    s.set(f"{c}11", f"={p}11*(1+{c}10)+$B$4")
    last = c
s.set("A13", "After F5 (select B11:%s11 for the fan):" % last)
s.set("A14", "Chance of reaching the goal"); s.set("B14", f'=SIM.PROB({last}11,">="&B7)')
s.set("A15", "Median outcome"); s.set("B15", f"=SIM.MEDIAN({last}11)")
s.set("A16", "Bad luck (P10)"); s.set("B16", f"=SIM.PERCENTILE({last}11,0.1)")
s.set("A17", "Good luck (P90)"); s.set("B17", f"=SIM.PERCENTILE({last}11,0.9)")
s.set("A18", "With no volatility"); s.set("B18", "=FV(B5,25,-B4,-B3)")
s.set("C18", "the straight-line plan")
s.fmt("B3 B4 B7 B15 B16 B17 B18", "$#,##0")
s.fmt(" ".join(f"{col(1 + y)}11" for y in range(0, 26)), "$#,##0")
s.fmt(" ".join(f"{col(1 + y)}10" for y in range(1, 26)), "0.0%")
s.fmt("B14", "0.0%")
s.save("retirement.tm")

# ---- 4. Project schedule -------------------------------------------------
s = Sheet()
s.width("A", 250); s.width("E", 110); s.width("F", 100)
s.set("A1", "Project schedule: three-point estimates and the merge bias")
s.set("A3", "Task"); s.set("B3", "Optimistic"); s.set("C3", "Likely"); s.set("D3", "Pessimistic"); s.set("E3", "Duration"); s.set("F3", "PERT mean")
tasks = [("Design", 5, 8, 15), ("Build the back end", 10, 15, 30), ("Build the front end", 8, 13, 25),
         ("Integration", 3, 5, 12), ("Testing", 5, 7, 20), ("Launch", 2, 3, 6)]
for i, (name, a, m, b) in enumerate(tasks):
    r = 4 + i
    s.set(f"A{r}", name); s.set(f"B{r}", a); s.set(f"C{r}", m); s.set(f"D{r}", b)
    s.set(f"E{r}", f"=RAND.PERT(B{r},C{r},D{r})"); s.set(f"F{r}", f"=(B{r}+4*C{r}+D{r})/6")
s.set("A11", "Total: back and front end side by side"); s.set("E11", "=E4+MAX(E5,E6)+E7+E8+E9"); s.set("F11", "=F4+MAX(F5,F6)+F7+F8+F9")
s.set("A13", "Deadline (working days)"); s.set("E13", 45)
s.set("A14", "Chance of making it"); s.set("E14", '=SIM.PROB(E11,"<="&E13)')
s.set("A15", "Days to be 80% sure"); s.set("E15", "=SIM.PERCENTILE(E11,0.8)")
s.set("A16", "Expected duration"); s.set("E16", "=SIM.MEAN(E11)")
s.set("A17", "The plan from average task times"); s.set("E17", "=F11")
s.set("A19", "The average of the longer of two paths is more than the longer")
s.set("A20", "of their averages: plans made from averages run late on average.")
s.fmt(" ".join(f"E{r} F{r}" for r in range(4, 12)) + " E15 E16 E17", "0.0")
s.fmt("E14", "0%")
s.save("project.tm")

# ---- 5. Judgment ---------------------------------------------------------
s = Sheet()
s.width("A", 300); s.width("E", 120)
s.set("A1", "Keeping score: probabilities, outcomes and updating")
s.set("A3", "Question"); s.set("B3", "Alice"); s.set("C3", "Bob"); s.set("D3", "Crowd"); s.set("E3", "Crowd, bolder"); s.set("F3", "Happened?")
qs = [("Will the product ship by June?", 0.8, 0.6, 1), ("Will the rate rise this quarter?", 0.3, 0.55, 0),
      ("Will the rival raise prices?", 0.65, 0.7, 1), ("Will the pilot beat its target?", 0.4, 0.2, 0),
      ("Will the hire accept the offer?", 0.9, 0.75, 1), ("Will the vote pass?", 0.55, 0.35, 1),
      ("Will the supplier be late?", 0.25, 0.4, 0), ("Will the trial succeed?", 0.15, 0.3, 0)]
for i, (q, a, b, o) in enumerate(qs):
    r = 4 + i
    s.set(f"A{r}", q); s.set(f"B{r}", a); s.set(f"C{r}", b)
    s.set(f"D{r}", f"=AVERAGE(B{r}:C{r})"); s.set(f"E{r}", f"=EXTREMIZE(D{r})"); s.set(f"F{r}", o)
s.set("A13", "Brier score (0 is perfect, 0.25 a coin)")
s.set("A14", "Log score (lower is better)")
for c in "BCDE":
    s.set(f"{c}13", f"=BRIER({c}4:{c}11,$F$4:$F$11)")
    s.set(f"{c}14", f"=LOGSCORE({c}4:{c}11,$F$4:$F$11)")
s.set("A17", "Updating on evidence")
s.set("A18", "Prior: chance the launch slips"); s.set("B18", "30%")
s.set("A19", "Chance of a bad status report if it slips"); s.set("B19", "80%")
s.set("A20", "... and if it is on time"); s.set("B20", "20%")
s.set("A21", "After one bad report"); s.set("B21", "=BAYES(B18,B19,B20)")
s.set("A22", "After a second"); s.set("B22", "=BAYES(B21,B19,B20)")
s.set("A24", "Uncertain events, simulated")
s.set("A25", "Launch slips?"); s.set("B25", "=RAND.BERNOULLI(B18)")
s.set("A26", "Report is bad?"); s.set("B26", "=RAND.BERNOULLI(IF(B25,B19,B20))")
s.set("A27", "Slipped, among futures with a bad report (F5)"); s.set("B27", "=B25*B26")
s.set("A28", "Chance of a bad report"); s.set("B28", "=SIM.MEAN(B26)")
s.set("A29", "Chance of slip and bad report"); s.set("B29", "=SIM.MEAN(B27)")
s.set("A30", "So, slipped given a bad report"); s.set("B30", "=B29/B28")
s.set("C30", "simulation agrees with Bayes in B21")
s.fmt(" ".join(f"{c}{r}" for c in "BCDE" for r in range(4, 12)), "0%")
s.fmt("B13 C13 D13 E13 B14 C14 D14 E14", "0.000")
s.fmt("B21 B22 B28 B29 B30", "0.0%")
s.save("judgment.tm")
print("ok")

# ---- 6. Weather ----------------------------------------------------------
rng = random.Random(11)
s = Sheet()
s.width("A", 250)
for c in range(1, 16):
    s.width(col(c), 64)
s.set("A1", "The next two weeks: will it rain, and how warm will it be?")
s.set("A3", "Today's temperature (°C)"); s.set("B3", 16.5)
s.set("A4", "Today's weather"); s.set("B4", "rain")
s.set("A5", "Day of the year today"); s.set("B5", 120)
s.set("A6", "Anomaly carried over a day"); s.set("B6", 0.75); s.set("C6", "AR(1) phi: a warm spell fades by a quarter a day")
s.set("A7", "Day-to-day shock (°C)"); s.set("B7", 1.8)
s.set("A9", "Day")
s.set("A10", "Seasonal normal (°C)")
s.set("A11", "Anomaly (°C)")
s.set("A12", "Temperature (°C)")
s.set("A13", "Weather")
s.set("A14", "Rain?")
s.set("A16", "After F5:")
s.set("A17", "Chance of rain")
s.set("A18", "Temperature, P10")
s.set("A19", "Temperature, P90")
for d in range(0, 15):
    c = col(1 + d); p = col(d)
    s.set(f"{c}9", d)
    # Climatology: a year-long sine wave, warmest late July.
    s.set(f"{c}10", f"=11+7*SIN(2*PI()*($B$5+{c}9-110)/365)")
    if d == 0:
        s.set(f"{c}11", "=B3-B10")
        s.set(f"{c}13", "=B4")
    else:
        s.set(f"{c}11", f"=RAND.AR1({p}11,0,$B$6,$B$7)")
        s.set(f"{c}13", f"=RAND.MARKOV({p}13,$A$23:$A$24,$B$23:$C$24)")
        s.set(f"{c}17", f"=SIM.MEAN({c}14)")
        s.set(f"{c}18", f"=SIM.PERCENTILE({c}12,0.1)")
        s.set(f"{c}19", f"=SIM.PERCENTILE({c}12,0.9)")
    s.set(f"{c}12", f"={c}10+{c}11")
    s.set(f"{c}14", f'={c}13="rain"')
s.set("A21", "Rain follows rain: a Markov chain, counted from the last 60 days")
s.set("B22", "dry"); s.set("C22", "rain")
s.set("A23", "dry"); s.set("A24", "rain")
for r, frm in ((23, "dry"), (24, "rain")):
    for c, to in (("B", "dry"), ("C", "rain")):
        s.set(f"{c}{r}", f'=MARKOV.ESTIMATE("{frm}","{to}",$B$36:$B$95,1)')
s.set("A25", "Long-run share of rainy days"); s.set("B25", '=MARKOV.STEADY("rain",A23:A24,B23:C24)')
s.set("A26", "Rain a week from now, exactly"); s.set("B26", '=MARKOV.PROB(B4,"rain",A23:A24,B23:C24,7)')
s.set("A27", "... and as simulated"); s.set("B27", "=H17")
s.set("A28", "Rainy days in the next fortnight"); s.set("B28", '=COUNTIF(C13:P13,"rain")')
s.set("A29", "Expected rainy days (F5)"); s.set("B29", "=SIM.MEAN(B28)")
s.set("A30", "Chance of a day above 20°C"); s.set("B30", '=SIM.PROB(B31,">20")')
s.set("A31", "Warmest day of the fortnight"); s.set("B31", "=MAX(C12:P12)")
s.set("A32", "Select C12:P12 for the temperature fan; C14 or any Rain? cell for its odds.")
s.set("A34", "Observed, last 60 days")
s.set("A35", "Day"); s.set("B35", "Weather")
state = "dry"
for i in range(60):
    r = 36 + i
    s.set(f"A{r}", i - 60)
    p_rain = 0.6 if state == "rain" else 0.25
    state = "rain" if rng.random() < p_rain else "dry"
    s.set(f"B{r}", state)
s.fmt(" ".join(f"{col(1+d)}{r}" for d in range(15) for r in (10, 11, 12)) + " B3 B7", "0.0")
s.fmt(" ".join(f"{col(1+d)}{r}" for d in range(1, 15) for r in (18, 19)), "0.0")
s.fmt(" ".join(f"{col(1+d)}17" for d in range(1, 15)) + " B23 C23 B24 C24 B25 B26 B27 B30", "0%")
s.fmt("B29 B31", "0.0")
s.save("weather.tm")

# ---- 7. Football ---------------------------------------------------------
rng = random.Random(5)
teams = ["Rovers", "United", "City", "Athletic", "Wanderers", "Albion"]
strength = {"Rovers": (1.35, 0.8), "United": (1.25, 0.85), "City": (1.1, 1.0),
            "Athletic": (0.95, 1.05), "Wanderers": (0.85, 1.15), "Albion": (0.75, 1.2)}

def poisson(lam):
    L, k, p = math.exp(-lam), 0, 1.0
    while True:
        p *= rng.random()
        if p <= L:
            return k
        k += 1

def play(h, a):
    lam = 1.5 * strength[h][0] * strength[a][1]
    mu = 1.15 * strength[a][0] * strength[h][1]
    return poisson(lam), poisson(mu)

last_season = [(h, a) for h in teams for a in teams if h != a]
fixtures = [(h, a) for h in teams for a in teams if h != a]
rng.shuffle(fixtures)
played, remaining = fixtures[:18], fixtures[18:]
history = [("last", h, a) + play(h, a) for h, a in last_season] + \
          [("this", h, a) + play(h, a) for h, a in played]

s = Sheet(iterations=20000)
s.width("A", 110); s.width("B", 110); s.width("C", 120)
for c in "DEFGHIJK":
    s.width(c, 90)
s.set("A1", "Who wins the league? Poisson goals, twelve matches to go")
s.set("A3", "Team"); s.set("B3", "Points now"); s.set("C3", "Final (one future)"); s.set("D3", "Champion?")
s.set("E3", "Title chance"); s.set("F3", "Top two"); s.set("G3", "Expected pts")
s.set("H3", "Tie-break"); s.set("I3", "In top two?")
first_r, last_r = 22, 22 + len(history) - 1
fr = last_r + 4
lr = fr + len(remaining) - 1
for i, t in enumerate(teams):
    r = 4 + i
    s.set(f"A{r}", t)
    s.set(f"B{r}", f"=SUMIF($B${first_r}:$B${last_r},A{r},$F${first_r}:$F${last_r})+SUMIF($C${first_r}:$C${last_r},A{r},$G${first_r}:$G${last_r})")
    s.set(f"C{r}", f"=B{r}+SUMIF($A${fr}:$A${lr},A{r},$J${fr}:$J${lr})+SUMIF($B${fr}:$B${lr},A{r},$K${fr}:$K${lr})")
    # Level on points, a coin decides, as goal difference would.
    s.set(f"H{r}", f"=C{r}+RAND()/10")
    s.set(f"D{r}", f"=H{r}=MAX($H$4:$H$9)")
    s.set(f"I{r}", f"=H{r}>=LARGE($H$4:$H$9,2)")
    s.set(f"E{r}", f"=SIM.MEAN(D{r})")
    s.set(f"F{r}", f"=SIM.MEAN(I{r})")
    s.set(f"G{r}", f"=SIM.MEAN(C{r})")
s.set("A11", "Elo, for comparison")
s.set("A12", "Rovers' rating"); s.set("B12", 1640)
s.set("A13", "United's rating"); s.set("B13", 1610)
s.set("A14", "Rovers at home: expected score"); s.set("B14", "=ELO.EXPECT(B12,B13,65)")
s.set("A15", "... after beating United"); s.set("B15", "=ELO.UPDATE(B12,B14,1,20)")
s.set("A17", "Press F5, then read columns E to G. Select D4 for the leaders' odds.")
s.set("A18", "Expected goals come from each side's attack and the other's defence")
s.set("A19", "in last season's results and this season's so far.")
s.set("A21", "Season"); s.set("B21", "Home"); s.set("C21", "Away"); s.set("D21", "Home goals"); s.set("E21", "Away goals")
s.set("F21", "Home pts"); s.set("G21", "Away pts")
for i, (season, h, a, hg, ag) in enumerate(history):
    r = first_r + i
    s.set(f"A{r}", season); s.set(f"B{r}", h); s.set(f"C{r}", a)
    s.set(f"D{r}", hg); s.set(f"E{r}", ag)
    # Only this season's matches earn points.
    s.set(f"F{r}", f'=IF(A{r}="this",IF(D{r}>E{r},3,IF(D{r}=E{r},1,0)),0)')
    s.set(f"G{r}", f'=IF(A{r}="this",IF(E{r}>D{r},3,IF(D{r}=E{r},1,0)),0)')
s.set(f"A{fr - 2}", "Still to play")
s.set(f"A{fr - 1}", "Home"); s.set(f"B{fr - 1}", "Away"); s.set(f"C{fr - 1}", "xG home"); s.set(f"D{fr - 1}", "xG away")
s.set(f"E{fr - 1}", "Home win"); s.set(f"F{fr - 1}", "Draw"); s.set(f"G{fr - 1}", "Away win")
s.set(f"H{fr - 1}", "Goals (h)"); s.set(f"I{fr - 1}", "Goals (a)"); s.set(f"J{fr - 1}", "Home pts"); s.set(f"K{fr - 1}", "Away pts")
res = f"$B${first_r}:$B${last_r},$C${first_r}:$C${last_r},$D${first_r}:$D${last_r},$E${first_r}:$E${last_r}"
for i, (h, a) in enumerate(remaining):
    r = fr + i
    s.set(f"A{r}", h); s.set(f"B{r}", a)
    s.set(f"C{r}", f"=MATCH.XG(A{r},B{r},{res},1)")
    s.set(f"D{r}", f"=MATCH.XG(A{r},B{r},{res},2)")
    s.set(f"E{r}", f'=POISSON.MATCH(C{r},D{r},"home",-0.05)')
    s.set(f"F{r}", f'=POISSON.MATCH(C{r},D{r},"draw",-0.05)')
    s.set(f"G{r}", f'=POISSON.MATCH(C{r},D{r},"away",-0.05)')
    s.set(f"H{r}", f"=RAND.POISSON(C{r})")
    s.set(f"I{r}", f"=RAND.POISSON(D{r})")
    s.set(f"J{r}", f"=IF(H{r}>I{r},3,IF(H{r}=I{r},1,0))")
    s.set(f"K{r}", f"=IF(I{r}>H{r},3,IF(H{r}=I{r},1,0))")
s.fmt(" ".join(f"{c}{r}" for c in "EF" for r in range(4, 10)) + " B14", "0%")
s.fmt(" ".join(f"G{r}" for r in range(4, 10)), "0.0")
s.fmt(" ".join(f"{c}{r}" for c in "CD" for r in range(fr, lr + 1)), "0.00")
s.fmt(" ".join(f"{c}{r}" for c in "EFG" for r in range(fr, lr + 1)), "0%")
s.fmt(" ".join(f"H{r}" for r in range(4, 10)), "0.00")
s.fmt("B15", "0")
s.save("football.tm")

# ---- 8. Stocks -----------------------------------------------------------
# A history whose fitted drift lands near the 9% it was drawn with: two
# years pin a drift down only to about 20% either way, a lesson the sheet
# states rather than one its example should stumble into.
for seed in range(1, 1000):
    rng = random.Random(seed)
    p, logs = 50.0, []
    for w in range(104):
        r = (0.09 - 0.28 ** 2 / 2) / 52 + 0.28 / math.sqrt(52) * rng.gauss(0, 1)
        logs.append(r)
    m = sum(logs) / len(logs)
    sd = math.sqrt(sum((x - m) ** 2 for x in logs) / (len(logs) - 1))
    if abs(m * 52 + sd * sd * 52 / 2 - 0.09) < 0.02:
        break
rng = random.Random(seed)
s = Sheet()
s.width("A", 60); s.width("B", 90); s.width("C", 90); s.width("D", 110); s.width("F", 300); s.width("G", 110)
s.set("A1", "A share price, a year ahead: two random walks fitted to two years of history")
s.set("A3", "Week"); s.set("B3", "Price (GBM)"); s.set("C3", "Log return"); s.set("D3", "Price (bootstrap)")
price = 50.0
hist_last = 3 + 104
for w in range(0, 105):
    r = 4 + w
    if w > 0:
        price *= math.exp((0.09 - 0.28 ** 2 / 2) / 52 + 0.28 / math.sqrt(52) * rng.gauss(0, 1))
    s.set(f"A{r}", w - 104)
    s.set(f"B{r}", round(price, 2))
    if w > 0:
        s.set(f"C{r}", f"=LN(B{r}/B{r-1})")
    s.set(f"D{r}", f"=B{r}")
last = 4 + 104
for w in range(1, 53):
    r = last + w
    s.set(f"A{r}", w)
    # Geometric Brownian motion, a week a step, with the fitted drift and volatility.
    s.set(f"B{r}", f"=B{r-1}*EXP(($G$4-$G$5^2/2)/52+$G$5/SQRT(52)*RAND.NORMAL(0,1))")
    # History resampled: each future week is one of the past weeks' returns.
    s.set(f"D{r}", f"=D{r-1}*EXP(RAND.BOOTSTRAP($C$5:$C${last}))")
end = last + 52
s.set("F3", "Fitted to the history")
s.set("F4", "Drift, a year"); s.set("G4", f"=DRIFT(B4:B{last},52)")
s.set("F5", "Volatility, a year"); s.set("G5", f"=VOLATILITY(B4:B{last},52)")
s.set("F6", "Price today"); s.set("G6", f"=B{last}")
s.set("F8", "A year ahead, in theory (GBM)")
s.set("F9", "Median"); s.set("G9", "=GBM.PERCENTILE(G6,0.5,G4,G5,1)")
s.set("F10", "P10"); s.set("G10", "=GBM.PERCENTILE(G6,0.1,G4,G5,1)")
s.set("F11", "P90"); s.set("G11", "=GBM.PERCENTILE(G6,0.9,G4,G5,1)")
s.set("F12", "Chance of ending higher"); s.set("G12", "=GBM.PROB(G6,G6,G4,G5,1)")
s.set("F14", "A year ahead, simulated (F5)")
s.set("F15", "Median, GBM"); s.set("G15", f"=SIM.MEDIAN(B{end})")
s.set("F16", "Median, bootstrap"); s.set("G16", f"=SIM.MEDIAN(D{end})")
s.set("F17", "Chance of ending higher"); s.set("G17", f'=SIM.PROB(B{end},">"&G6)')
s.set("F18", "Value at risk, 95%"); s.set("G18", f"=G6-SIM.PERCENTILE(B{end},0.05)")
s.set("F19", "Worst fall on the way (median)"); s.set("G19", "=SIM.MEDIAN(G20)")
s.set("F20", "Worst fall on the way (one future)"); s.set("G20", f"=DRAWDOWN(B{last}:B{end})")
s.set("F21", "Chance of a 30% fall at some point"); s.set("G21", '=SIM.PROB(G20,">=0.3")')
s.set("F23", "What the market charges")
s.set("F24", "A call at +10%, a year, 3% rates"); s.set("G24", "=BLACKSCHOLES(G6,G6*1.1,3%,G5,1)")
s.set("F26", f"Select B4:B{end} or D4:D{end} for the fans.")
s.set("F27", "Two years pin the drift down only to about 20% either way;")
s.set("F28", "the volatility is known far better. And this is not advice:")
s.set("F29", "a random walk fitted to the past knows nothing of the news.")
s.fmt(" ".join(f"{c}{r}" for c in "BD" for r in range(4, end + 1)) + " G6 G9 G10 G11 G15 G16 G18 G24", "0.00")
s.fmt(" ".join(f"C{r}" for r in range(5, last + 1)), "0.0%")
s.fmt("G4 G5 G12 G17 G19 G20 G21", "0.0%")
s.save("stocks.tm")
print("ok, more")
