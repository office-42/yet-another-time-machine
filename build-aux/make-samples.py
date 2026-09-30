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
        self.sources = []
        self.iterations = iterations
        self.seed = seed
    def set(self, ref, value):
        self.cells[ref] = str(value)
    def width(self, c, w):
        self.widths[c] = w
    def source(self, kind, name, path, bounds):
        self.sources.append((kind, name, path, bounds))
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
        for kind, src_name, src_path, bounds in self.sources:
            line = f"{kind}\t{src_name}\t{src_path}"
            if bounds:
                line += "\t" + "\t".join(str(b) for b in bounds)
            lines.append(line)
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
s.set("G17", "A future's months share their surprises, as the")
s.set("G18", "smoothing carries each forward: a high month")
s.set("G19", "is followed by more, and a year's total is as")
s.set("G20", "uncertain as the model says it is.")
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

# ---- Pictures ------------------------------------------------------------
import struct, zlib

PICS = os.path.join(OUT, "pictures")

def write_png(name, w, h, pixel, alpha=False):
    """pixel(x, y) -> (r, g, b), or (r, g, b, a) with alpha, each 0-255."""
    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            row.extend(pixel(x, y))
        rows.append(bytes(row))
    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xffffffff)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))
    os.makedirs(PICS, exist_ok=True)
    with open(os.path.join(PICS, name), "wb") as f:
        f.write(png)

def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))

# ---- 9. Radar nowcast ----------------------------------------------------
RADAR = [(20, "#7fd4ff"), (25, "#3aa0ff"), (30, "#2fd05b"), (35, "#fff23c"),
         (40, "#ffa21f"), (45, "#ff3b30"), (50, "#c21fd6")]
RW, RH = 160, 120
BOUNDS = (4.0, 57.0, 14.0, 62.0)          # west south east north
# Rain west of Oslo, drifting east-north-east: dry at home now, rain due in
# about three quarters of an hour.
blobs = [(78, 60, 44, 9, 5), (70, 70, 41, 7, 7), (83, 48, 38, 6, 4), (26, 30, 47, 5, 5),
         (106, 95, 36, 8, 6), (46, 90, 33, 10, 8), (74, 56, 30, 18, 12)]

def radar_frame(shift_x, shift_y, gain):
    def dbz(x, y):
        best = 0
        for bx, by, peak, sx, sy in blobs:
            dx = (x - bx - shift_x) / sx
            dy = (y - by - shift_y) / sy
            best = max(best, peak + gain - 6 * (dx * dx + dy * dy))
        return best
    def pixel(x, y):
        z = dbz(x, y)
        colour = None
        for level, c in RADAR:
            if z >= level:
                colour = c
        # Dry: transparent, as radar composites are, so the map shows.
        return hexrgb(colour) + (255,) if colour else (238, 242, 245, 0)
    return pixel

write_png("radar-0.png", RW, RH, radar_frame(0, 0, 0), alpha=True)
write_png("radar-1.png", RW, RH, radar_frame(2, -1, 0.5), alpha=True)

s = Sheet(iterations=5000)
s.width("A", 230)
for c in range(1, 14):
    s.width(col(c), 62)
s.source("image", "radar_0", "pictures/radar-0.png", BOUNDS)
s.source("image", "radar_1", "pictures/radar-1.png", BOUNDS)
s.source("map", "world", "maps/world.geojson", None)
s.set("A1", "Rain at home in the next two hours: a radar nowcast")
s.set("A3", "Home, lat"); s.set("B3", 59.91)
s.set("A4", "Home, lon"); s.set("B4", 10.75)
s.set("A5", "Motion east, a frame (share of width)"); s.set("B5", '=IMAGE.MOTION("radar_0","radar_1","dx",0.1)')
s.set("A6", "Motion south, a frame (share of height)"); s.set("B6", '=IMAGE.MOTION("radar_0","radar_1","dy",0.1)')
s.set("A7", "Speed (km/h)"); s.set("B7", "=6*SQRT((GEO.DISTANCE(59.5,4,59.5,14)*B5)^2+(GEO.DISTANCE(57,9,62,9)*B6)^2)")
s.set("A8", "Motion east, this future"); s.set("B8", "=RAND.NORMAL(B5,0.25*ABS(B5)+0.002)")
s.set("A9", "Motion south, this future"); s.set("B9", "=RAND.NORMAL(B6,0.25*ABS(B6)+0.002)")
s.set("A10", "Home on the radar, u"); s.set("B10", '=IMAGE.XY("radar_1",B3,B4,"u")')
s.set("A11", "Home on the radar, v"); s.set("B11", '=IMAGE.XY("radar_1",B3,B4,"v")')
s.set("A13", "Minutes ahead")
s.set("A14", "Where the rain comes from, u")
s.set("A15", "Where the rain comes from, v")
s.set("A16", "Reflectivity there (dBZ)")
s.set("A17", "Grown or decayed (dBZ)")
s.set("A18", "Rain rate (mm/h)")
s.set("A19", "Raining?")
s.set("A21", "Chance of rain (F5)")
s.set("A22", "Rain rate, P90 (mm/h)")
for k in range(0, 13):
    c = col(1 + k)
    s.set(f"{c}13", k * 10)
    # Backward, semi-Lagrangian: the rain here in k frames is the rain k
    # frames upstream now.
    s.set(f"{c}14", f"=$B$10-{c}13/10*$B$8")
    s.set(f"{c}15", f"=$B$11-{c}13/10*$B$9")
    s.set(f"{c}16", f'=IFERROR(IMAGE.LEGEND("radar_1",{c}14,{c}15,$B$26:$B$32,$A$26:$A$32,0.12),0)')
    s.set(f"{c}17", f"=IF({c}16>0,{c}16+RAND.NORMAL(0,1.5*SQRT({c}13/10)),0)")
    # Marshall and Palmer: Z = 200 R^1.6.
    s.set(f"{c}18", f"=IF({c}17<18,0,(10^({c}17/10)/200)^(1/1.6))")
    s.set(f"{c}19", f"={c}18>=0.1")
    s.set(f"{c}21", f"=SIM.MEAN({c}19)")
    s.set(f"{c}22", f"=SIM.PERCENTILE({c}18,0.9)")
s.set("D3", "Chance of rain within the hour"); s.set("H3", "=SIM.MEAN(H4)")
s.set("D4", "Rain within the hour, this future"); s.set("H4", "=OR(B19:H19)")
s.set("D5", "Share of the radar showing rain now"); s.set("H5", '=IMAGE.FRACTION("radar_1","saturation",">0.3")')
s.set("D7", "Select A3:B4 to see home on the radar frame; select a row of")
s.set("D8", "rain rates for its fan; the block below maps the nowcast.")
s.set("A24", "Radar legend")
s.set("A25", "dBZ"); s.set("B25", "Colour")
for i, (level, colour) in enumerate(RADAR):
    s.set(f"A{26 + i}", level); s.set(f"B{26 + i}", colour)
s.set("A34", "The radar an hour ahead (dBZ), extrapolated over the whole frame; select B35:AG58:")
g0r, g0c = 35, 1
for i in range(24):
    for j in range(32):
        c = col(g0c + j)
        s.set(f"{c}{g0r + i}", f'=IFERROR(IMAGE.LEGEND("radar_1",(COLUMN()-{g0c + 1}+0.5)/32-6*$B$5,(ROW()-{g0r}+0.5)/24-6*$B$6,$B$26:$B$32,$A$26:$A$32,0.12),0)')
s.fmt("B5 B6 B8 B9 B10 B11", "0.0000")
s.fmt("B7", "0")
s.fmt(" ".join(f"{col(1 + k)}{r}" for k in range(13) for r in (14, 15)), "0.000")
s.fmt(" ".join(f"{col(1 + k)}{r}" for k in range(13) for r in (16, 17, 18, 22)), "0.0")
s.fmt(" ".join(f"{col(1 + k)}21" for k in range(13)) + " H3 H5", "0%")
s.fmt(" ".join(f"{col(g0c + j)}{g0r + i}" for i in range(24) for j in range(32)), "0")
s.save("nowcast.tm")

# ---- 10. Wildfire --------------------------------------------------------
LAND = [("forest", "#2e7d32", 1.0), ("grass", "#c5e17a", 0.6), ("town", "#9e9e9e", 0.25),
        ("road", "#555555", 0.05), ("water", "#4a90d9", 0.0)]
LW, LH = 80, 60
lrng = random.Random(21)
meadows = [(lrng.uniform(0, LW), lrng.uniform(0, LH), lrng.uniform(4, 9)) for _ in range(9)]

def land(x, y):
    kind = "forest"
    for mx, my, r in meadows:
        if (x - mx) ** 2 + (y - my) ** 2 < r * r:
            kind = "grass"
    if ((x - 16) / 11) ** 2 + ((y - 16) / 7) ** 2 < 1:
        kind = "water"
    if abs(x - (38 + 6 * math.sin(y / 7))) < 1.3:
        kind = "water"
    if 58 <= x <= 72 and 8 <= y <= 18:
        kind = "town"
    if abs(y - 34) < 0.8:
        kind = "road"
    return hexrgb(dict((k, c) for k, c, f in LAND)[kind])

write_png("landcover.png", LW, LH, land)

s = Sheet(iterations=2000)
s.width("A", 60)
for c in range(1, 41):
    s.width(col(c), 36)
s.width("M", 44)   # room for the town's chance, "27%"
s.source("image", "land", "pictures/landcover.png", None)
s.set("A1", "Where will the fire go? A spread model over a land-cover picture")
s.set("A2", "Start row"); s.set("D2", 22)
s.set("F2", "Start col"); s.set("I2", 8)
s.set("K2", "p"); s.set("M2", 0.6)
s.set("O2", "Hours"); s.set("R2", 48)
s.set("T2", "Wind from"); s.set("W2", 225)
s.set("Y2", "m/s"); s.set("AB2", 4)
s.set("A3", "The land cover, its colour on the picture and how well it burns are in AQ3:AS8.")
s.set("AQ3", "Land"); s.set("AR3", "Colour"); s.set("AS3", "Fuel")
for i, (kind, colour, fuel) in enumerate(LAND):
    s.set(f"AQ{4 + i}", kind); s.set(f"AR{4 + i}", colour); s.set(f"AS{4 + i}", fuel)
s.set("A5", "Chance the town burns (F5)"); s.set("M5", "=SIM.MEAN(M6)")
s.set("A6", "Town burns, this future"); s.set("M6", "=MAX(AF46:AK50)")
s.set("P5", "Expected cells burnt"); s.set("Z5", "=SIM.MEAN(Z6)")
s.set("P6", "Cells burnt, this future"); s.set("Z6", "=SUM(B42:AO71)")
s.set("A7", "Fuel, read off the picture's colours (select B8:AO37 for a heatmap):")
f0 = 8
colours_range = "$AR$4:$AR$8"; fuels_range = "$AS$4:$AS$8"
for i in range(30):
    for j in range(40):
        c = col(1 + j)
        s.set(f"{c}{f0 + i}", f'=IFERROR(IMAGE.LEGEND("land",({j}+0.5)/40,({i}+0.5)/30,{colours_range},{fuels_range},0.2),0)')
s.set("A40", "Burnt in this future; F5, then select B42:AO71 for the chance each cell burns:")
b0 = 42
for i in range(30):
    for j in range(40):
        c = col(1 + j)
        s.set(f"{c}{b0 + i}", f"=RAND.SPREAD($B${f0}:$AO${f0 + 29},$D$2,$I$2,$M$2,$R$2,{i + 1},{j + 1},$W$2,$AB$2)")
s.fmt(" ".join(f"{col(1 + j)}{f0 + i}" for i in range(30) for j in range(40)), "0.0")
s.fmt("M5", "0%")
s.fmt("Z5", "0")
s.save("wildfire.tm")

# ---- 11. Storm -----------------------------------------------------------
track = [(0, 18.5, -62.0), (12, 19.2, -63.6), (24, 20.0, -65.3), (36, 20.9, -67.1), (48, 21.9, -69.0),
         (60, 22.85, -70.95), (72, 23.8, -72.9), (96, 25.6, -76.6), (120, 27.5, -79.8)]
# The National Hurricane Center's 2026 Atlantic cone radii (n mi), two
# thirds of five years' track errors; as a standard deviation each way,
# the radius divided by 1.4823.
cone = {0: 0, 12: 25, 24: 39, 36: 49, 48: 62, 60: 77, 72: 95, 96: 134, 120: 200}
s = Sheet(iterations=4000)
s.width("A", 170); s.width("B", 60)
last = col(len(track))
for c in range(1, len(track) + 1):
    s.width(col(c), 70)
s.source("map", "world", "maps/world.geojson", None)
s.set("A1", "Where will the hurricane go? The official track and its known errors")
s.set("A3", "Hours ahead")
s.set("A4", "Official track, lat")
s.set("A5", "Official track, lon")
s.set("A6", "Cone radius (n mi)")
s.set("A7", "Error sd each way (km)")
s.set("A8", "Error east (km)")
s.set("A9", "Error north (km)")
s.set("A10", "Storm lat")
s.set("A11", "Storm lon")
s.set("A12", "Over")
s.set("A13", "Distance to Miami (km)")
for i, (h, la, lo) in enumerate(track):
    c = col(1 + i); p = col(i)
    s.set(f"{c}3", h); s.set(f"{c}4", la); s.set(f"{c}5", lo); s.set(f"{c}6", cone[h])
    s.set(f"{c}7", f"={c}6*1.852/1.4823")
    if i == 0:
        s.set(f"{c}8", 0); s.set(f"{c}9", 0)
    else:
        # A random walk whose spread at each hour matches the cone: the
        # errors at one hour and the next are not independent.
        s.set(f"{c}8", f"={p}8+RAND.NORMAL(0,SQRT({c}7^2-{p}7^2))")
        s.set(f"{c}9", f"={p}9+RAND.NORMAL(0,SQRT({c}7^2-{p}7^2))")
    s.set(f"{c}10", f"={c}4+{c}9/111.2")
    s.set(f"{c}11", f"={c}5+{c}8/(111.2*COS({c}10*PI()/180))")
    s.set(f"{c}12", f'=IFERROR(MAP.REGION("world",{c}10,{c}11),"sea")')
    s.set(f"{c}13", f"=GEO.DISTANCE({c}10,{c}11,25.76,-80.19)")
s.set("A15", "Closest to Miami (km)"); s.set("B15", f"=MIN(B13:{last}13)")
s.set("A16", "Chance within 100 km of Miami"); s.set("B16", '=SIM.PROB(B15,"<=100")')
s.set("A18", "Chance the centre crosses (F5)")
countries = ["Puerto Rico", "Dominican Rep.", "Haiti", "Cuba", "Bahamas", "United States of America", "Jamaica"]
for i, name in enumerate(countries):
    r = 19 + i
    s.set(f"A{r}", name)
    s.set(f"C{r}", f"=COUNTIF($B$12:${last}$12,A{r})>0")
    s.set(f"B{r}", f"=SIM.MEAN(C{r})")
s.set("A27", f"Likeliest place at {track[-1][0]} h"); s.set("B27", f"=SIM.MODE({last}12)")
s.set("D15", f"Select A10:{last}11 for the tracks on the map;")
s.set("D16", f"a row of the storm's lat for its fan; {last}12 for where it ends up.")
s.fmt(" ".join(f"{col(1 + i)}{r}" for i in range(len(track)) for r in (7, 8, 9, 13)) + " B15", "0")
s.fmt(" ".join(f"{col(1 + i)}{r}" for i in range(len(track)) for r in (10, 11)), "0.00")
s.fmt(" ".join(f"B{19 + i}" for i in range(len(countries))) + " B16", "0%")
s.save("storm.tm")

# ---- 12. Rain gauges -----------------------------------------------------
grng = random.Random(8)
s = Sheet()
s.width("A", 120); s.width("B", 70); s.width("C", 70); s.width("D", 80); s.width("F", 250); s.width("G", 90)
s.source("map", "world", "maps/world.geojson", None)
s.set("A1", "Rain at the farm, between the gauges: interpolation with its error")
s.set("A3", "Station"); s.set("B3", "Lat"); s.set("C3", "Lon"); s.set("D3", "Rain (mm)")
towns = ["Abbots", "Barrow", "Coldham", "Denby", "Easton", "Fairley", "Glenmore", "Hatch",
         "Ivybridge", "Jarrow", "Kelby", "Linton", "Marsh", "Norton", "Oakley"]
for i, t in enumerate(towns):
    r = 4 + i
    la = round(grng.uniform(51.6, 52.6), 3)
    lo = round(grng.uniform(-1.6, 0.4), 3)
    # A storm that rained hardest in the north-west.
    rain = max(0, round(8 + 22 * math.exp(-((la - 52.4) ** 2 / 0.12 + (lo + 1.3) ** 2 / 0.5)) + grng.gauss(0, 2), 1))
    s.set(f"A{r}", t); s.set(f"B{r}", la); s.set(f"C{r}", lo); s.set(f"D{r}", rain)
s.set("F3", "The farm")
s.set("F4", "Lat"); s.set("G4", 52.15)
s.set("F5", "Lon"); s.set("G5", -0.9)
s.set("F7", "Nearest gauge (mm)"); s.set("G7", "=GEO.NEAREST(G4,G5,B4:B18,C4:C18,D4:D18)")
s.set("F8", "Nearest gauge is (km) away"); s.set("G8", "=GEO.NEAREST(G4,G5,B4:B18,C4:C18)")
s.set("F9", "Inverse-distance estimate (mm)"); s.set("G9", "=GEO.IDW(G4,G5,B4:B18,C4:C18,D4:D18)")
s.set("F10", "Kriged estimate (mm)"); s.set("G10", "=GEO.KRIGE(G4,G5,B4:B18,C4:C18,D4:D18)")
s.set("F11", "... give or take (sd, mm)"); s.set("G11", "=GEO.KRIGE.SD(G4,G5,B4:B18,C4:C18,D4:D18)")
s.set("F12", "Rain at the farm, one future"); s.set("G12", "=MAX(0,RAND.KRIGE(G4,G5,B4:B18,C4:C18,D4:D18))")
s.set("F13", "Chance of more than 20 mm (F5)"); s.set("G13", '=SIM.PROB(G12,">20")')
s.set("F14", "Gauges within 25 km"); s.set("G14", "=GEO.WITHIN(G4,G5,B4:B18,C4:C18,25)")
s.set("F16", "Select A3:D18 for the gauges on a map, coloured by rain.")
s.fmt("G7 G9 G10 G11 G12", "0.0")
s.fmt("G8", "0")
s.fmt("G13", "0%")
s.save("gauges.tm")

# ---- 13. Crops -----------------------------------------------------------
crng = random.Random(4)
FW, FH = 120, 80
plants = [(crng.uniform(0, FW), crng.uniform(0, FH)) for _ in range(140)]
# The cover each week, a slow spell in week 4 and all: the leaves' radius
# that gives it, for plants scattered at random.
cover = [0.04, 0.08, 0.17, 0.23, 0.40, 0.52]
for week in range(1, 7):
    radius = math.sqrt(-math.log(1 - cover[week - 1]) / (len(plants) / (FW * FH) * math.pi))
    def field(x, y, radius=radius):
        for px, py in plants:
            if (x + 0.5 - px) ** 2 + (y + 0.5 - py) ** 2 < radius * radius:
                shade = 40 + int(20 * math.sin(px + py))
                return (40 + shade // 3, 120 + shade, 40)
        return (140 + (x * 7 + y * 3) % 17, 110 + (x * 5 + y) % 13, 80)
    write_png(f"field-week{week}.png", FW, FH, field)

s = Sheet()
s.width("A", 60); s.width("B", 100); s.width("C", 80); s.width("D", 130); s.width("E", 24)
s.width("F", 330); s.width("G", 70)
for week in range(1, 7):
    s.source("image", f"week{week}", f"pictures/field-week{week}.png", None)
s.set("A1", "When will the canopy close? Green cover measured from weekly photographs")
s.set("A3", "Week"); s.set("B3", "Green cover"); s.set("C3", "Log-odds"); s.set("D3", "Cover, one future")
for week in range(1, 11):
    r = 3 + week
    s.set(f"A{r}", week)
    if week <= 6:
        # Excess green, 2g - r - b of the chromatic coordinates, above 0.1: plant.
        s.set(f"B{r}", f'=IMAGE.FRACTION("week{week}","exg",">0.1")')
        s.set(f"C{r}", f"=LN(B{r}/(1-B{r}))")
        s.set(f"D{r}", f"=B{r}")
    else:
        # A straight line in the log-odds is a logistic curve in the cover;
        # the line's own uncertainty is shared by the weeks of one future.
        s.set(f"D{r}", f"=1/(1+EXP(-RAND.LINEAR(A{r},$C$4:$C$9,$A$4:$A$9)))")
s.set("F3", "Cover in week 8, straight-line log-odds"); s.set("G3", "=1/(1+EXP(-FORECAST.LINEAR(8,C4:C9,A4:A9)))")
s.set("F4", "Chance the canopy has closed (80%) by week 8"); s.set("G4", '=SIM.PROB(D11,">=0.8")')
s.set("F5", "... by week 10"); s.set("G5", '=SIM.PROB(D13,">=0.8")')
s.set("F6", "Week the log-odds reach 80%"); s.set("G6", "=(LN(0.8/0.2)-INTERCEPT(C4:C9,A4:A9))/SLOPE(C4:C9,A4:A9)")
s.set("F8", "Select D4:D13 for the cover measured, then forecast, as a fan.")
s.set("F9", "Each week's cover is read straight off its photograph;")
s.set("F10", "Data > Pictures and Maps lists the six of them.")
s.fmt(" ".join(f"B{r}" for r in range(4, 10)) + " " + " ".join(f"D{r}" for r in range(4, 14)) + " G3 G4 G5", "0%")
s.fmt(" ".join(f"C{r}" for r in range(4, 10)), "0.00")
s.fmt("G6", "0.0")
s.save("crops.tm")
print("ok, pictures and maps")

# ---- 14. Analogues: learning from past projects ------------------------------
arng = random.Random(14)
s = Sheet(iterations=10000)
s.width("A", 50)
for c in "BCDE":
    s.width(c, 64)
s.width("F", 70); s.width("G", 44); s.width("H", 330)
for c in "IJKL":
    s.width(c, 80)
s.width("M", 70)
s.set("A1", "How far over will this project run? Learning from sixty past projects")
s.set("A3", "Project"); s.set("B3", "Team"); s.set("C3", "Months"); s.set("D3", "Novelty")
s.set("E3", "Links"); s.set("F3", "Overrun"); s.set("G3", ">50%")
for i in range(60):
    r = 4 + i
    team = arng.randint(3, 25); months = arng.randint(3, 24); novelty = arng.randint(1, 5)
    links = arng.randint(0, 12)
    # Overruns grow with novelty and links, and scatter more when novel.
    ratio = math.exp(0.02 + 0.07 * novelty + 0.025 * links + 0.004 * team - 0.006 * months
                     + arng.gauss(0, 0.05 + 0.035 * novelty))
    s.set(f"A{r}", f"P{i + 1:02d}"); s.set(f"B{r}", team); s.set(f"C{r}", months)
    s.set(f"D{r}", novelty); s.set(f"E{r}", links); s.set(f"F{r}", round(ratio, 3))
    s.set(f"G{r}", f"=F{r}>1.5")
    if i >= 40:
        # How far a model fitted to the first forty missed each of the rest:
        # honest, out-of-sample errors for the conformal interval.
        s.set(f"M{r}", f"=F{r}-FORECAST.MLR(B{r}:E{r},$F$4:$F$43,$B$4:$E$43)")
s.set("M3", "Miss")
X = "$B$4:$E$63"; Y = "$F$4:$F$63"
s.set("H3", "The new project: team, months, novelty (1-5), links to other work")
s.set("I4", 10); s.set("J4", 12); s.set("K4", 2); s.set("L4", 4)
s.set("I3", "Team"); s.set("J3", "Months"); s.set("K3", "Novelty"); s.set("L3", "Links")
x0 = "$I$4:$L$4"
rows = [
    ("Analogues: mean overrun of the 8 most similar", f"=KNN.FORECAST({x0},{Y},{X},8,1)"),
    ("... P10 and P90 of them", f"=KNN.PERCENTILE({x0},{Y},{X},0.1,8,1)", f"=KNN.PERCENTILE({x0},{Y},{X},0.9,8,1)"),
    ("Multiple regression", f"=FORECAST.MLR({x0},{Y},{X})"),
    ("... give or take (90%)", f"=FORECAST.MLR.CONFINT({x0},{Y},{X},0.9)"),
    ("... or by the last 20 projects' own misses (conformal)", "=CONFORMAL.CONFINT(M44:M63,0.9)"),
    ("P90 by quantile regression: the spread grows with novelty", f"=QUANTILE.REG({x0},{Y},{X},0.9)"),
    ("Chance of running more than 50% over (logistic)", f"=LOGIT.PROB({x0},$G$4:$G$63,{X})"),
    ("Each extra point of novelty adds (regression)", f"=MLR.COEF({Y},{X},3)"),
]
for i, row in enumerate(rows):
    r = 6 + i
    s.set(f"H{r}", row[0]); s.set(f"K{r}", row[1])
    if len(row) > 2:
        s.set(f"L{r}", row[2])
s.set("H15", "Overrun, one future: an analogue's (F5 simulates)"); s.set("K15", f"=RAND.KNN({x0},{Y},{X},8,1)")
s.set("H16", "Overrun, one future: the regression's"); s.set("K16", f"=RAND.MLR({x0},{Y},{X})")
s.set("H18", "The bid, on the regression's overruns: fixed price, or cost plus 12%?")
s.set("H19", "Cost if it runs to plan"); s.set("K19", 1200000)
s.set("H20", "Fixed price asked"); s.set("K20", 1650000)
s.set("H21", "Profit at a fixed price, this future"); s.set("K21", "=K20-K19*K16")
s.set("H22", "Profit at cost plus 12%, this future"); s.set("K22", "=K19*K16*0.12")
s.set("H23", "Expected profit: fixed, cost plus"); s.set("K23", "=SIM.MEAN(K21)"); s.set("L23", "=SIM.MEAN(K22)")
s.set("H24", "Chance the fixed price turns out the better deal"); s.set("K24", "=SIM.PBEST(1,K21,K22)")
s.set("H25", "Worth, for sure, to a firm with 400k of risk tolerance"); s.set("K25", "=SIM.CE(K21,400000)"); s.set("L25", "=SIM.CE(K22,400000)")
s.set("H26", "Most it is worth knowing the overrun before choosing"); s.set("K26", "=SIM.EVPI(K21,K22)")
s.set("H28", "Every function here works on any table: a row per past case, a column")
s.set("H29", "per thing known about it, and a column of how it turned out.")
s.fmt(" ".join(f"F{r}" for r in range(4, 64)) + " K6 L7 K8 K11 L11 K12 K13 K15 K16", "0.00")
s.fmt("K9 K10 " + " ".join(f"M{r}" for r in range(44, 64)), "0.00")
s.fmt("K14", "0.000")
s.fmt("K12", "0%")
s.fmt("K7 L7 K6 K8 K11", "0.00")
s.fmt("K19 K20 K21 K22 K23 L23 K25 L25 K26", "#,##0")
s.fmt("K24", "0%")
s.save("analogues.tm")

# ---- 15. A/B test: updating on evidence ---------------------------------------
s = Sheet(iterations=20000)
s.width("A", 290); s.width("B", 80); s.width("C", 80); s.width("D", 80); s.width("E", 90)
s.set("A1", "Is the new page better? Rates learnt from counts, by Bayes' rule")
s.set("A3", "Page"); s.set("B3", "Visitors"); s.set("C3", "Sign-ups"); s.set("D3", "Rate"); s.set("E3", "Rate, a future")
s.set("A4", "A, the old page"); s.set("B4", 2400); s.set("C4", 120)
s.set("A5", "B, the new page"); s.set("B5", 2380); s.set("C5", 145)
for r in (4, 5):
    s.set(f"D{r}", f"=C{r}/B{r}"); s.set(f"E{r}", f"=RAND.PROPORTION(C{r},B{r})")
s.set("A7", "Lift of B over A, this future"); s.set("B7", "=E5/E4-1")
s.set("A8", "Chance B is really the better page (F5)"); s.set("B8", '=SIM.PROB(B7,">0")')
s.set("A9", "Lift, 90% interval"); s.set("B9", "=SIM.PERCENTILE(B7,0.05)"); s.set("C9", "=SIM.PERCENTILE(B7,0.95)")
s.set("A10", "Sign-ups from the next 10,000 visitors: A, B"); s.set("B10", "=RAND.PROPORTION(C4,B4,10000)"); s.set("C10", "=RAND.PROPORTION(C5,B5,10000)")
s.set("A11", "Expected extra sign-ups from choosing B"); s.set("B11", "=SIM.MEAN(C10)-SIM.MEAN(B10)")
s.set("A12", "Most that testing longer could be worth (sign-ups)"); s.set("B12", "=SIM.EVPI(B10,C10)")
s.set("A14", "Eight landing pages: which is really best? Small samples, pulled to the group")
s.set("A15", "Page"); s.set("B15", "Visitors"); s.set("C15", "Sign-ups"); s.set("D15", "Raw rate"); s.set("E15", "Pooled rate")
pages = [("Spring sale", 1200, 66), ("Free trial", 950, 95), ("Webinar", 40, 5), ("Pricing", 2100, 63),
         ("Case study", 310, 12), ("Newsletter", 1800, 144), ("Demo video", 25, 4), ("Checklist", 640, 45)]
for i, (name, v, k) in enumerate(pages):
    r = 16 + i
    s.set(f"A{r}", name); s.set(f"B{r}", v); s.set(f"C{r}", k)
    s.set(f"D{r}", f"=C{r}/B{r}"); s.set(f"E{r}", f"=SHRINK.RATE(C{r},B{r},$C$16:$C$23,$B$16:$B$23)")
s.set("A25", "The demo video's 16% is four sign-ups in twenty-five: the pooled rate")
s.set("A26", "trusts it only as far as twenty-five visitors deserve.")
s.set("A28", "Support tickets: 37 in the last 12 weeks. How many in the next 4?")
s.set("A29", "Tickets in the next four weeks, one future"); s.set("B29", "=RAND.RATE(37,12,4)")
s.set("A30", "Enough staff for nine futures in ten: plan for"); s.set("B30", "=SIM.PERCENTILE(B29,0.9)")
s.fmt("D4 D5 E4 E5 " + " ".join(f"D{r} E{r}" for r in range(16, 24)), "0.0%")
s.fmt("B7 B9 C9", "0.0%")
s.fmt("B8", "0%")
s.fmt("B11 B12", "0")
s.save("abtest.tm")

# ---- 16. Lifetimes -------------------------------------------------------------
lrng2 = random.Random(16)
s = Sheet(iterations=10000)
s.width("A", 60); s.width("B", 70); s.width("C", 60); s.width("E", 380); s.width("F", 90)
s.set("A1", "How long will the pumps last? Lifetimes, some of them not over yet")
s.set("A3", "Pump"); s.set("B3", "Hours"); s.set("C3", "Failed")
for i in range(20):
    r = 4 + i
    age = lrng2.uniform(1500, 11000)
    life = 6000 * (-math.log(1 - lrng2.random())) ** (1 / 1.8)
    s.set(f"A{r}", f"#{i + 1}"); s.set(f"B{r}", round(min(age, life))); s.set(f"C{r}", 1 if life <= age else 0)
s.set("E3", "Failed 0: still running after so many hours -- a lifetime known only to be longer.")
s.set("E4", "Weibull shape (above 1: they wear out)"); s.set("F4", '=WEIBULL.FIT(B4:B23,"shape",C4:C23)')
s.set("E5", "Weibull scale (hours)"); s.set("F5", '=WEIBULL.FIT(B4:B23,"scale",C4:C23)')
s.set("E6", "Share still running at 5,000 hours: Kaplan-Meier"); s.set("F6", "=KAPLAN.MEIER(5000,B4:B23,C4:C23)")
s.set("E7", "... by the fitted Weibull"); s.set("F7", "=1-WEIBULL.DIST(5000,F4,F5,TRUE)")
s.set("E9", "Our pump has run (hours)"); s.set("F9", 3000)
s.set("E10", "The hour it fails, one future"); s.set("F10", "=RAND.WEIBULL(F4,F5,F9)")
s.set("E11", "Chance it fails in the 2,000 hours of warranty left (F5)"); s.set("F11", '=SIM.PROB(F10,"<"&(F9+2000))')
s.set("E12", "Hours it has left, even odds"); s.set("F12", "=SIM.MEDIAN(F10)-F9")
s.set("E13", "A lifetime straight from the record, one future"); s.set("F13", "=RAND.SURVIVAL(B4:B23,C4:C23)")
s.set("E15", "Repairs: a failure every 40 hours, 60 hours to mend, three fitters")
s.set("E16", "Chance a failed pump waits for a fitter"); s.set("F16", "=ERLANG.C(1/40,1/60,3)")
s.set("E17", "Mean wait (hours)"); s.set("F17", '=ERLANG.C(1/40,1/60,3,"wait")')
s.set("E19", "With nothing to go on: a firm that has lasted 12 years (Gott's rule)")
s.set("E20", "Years it lasts yet, one future"); s.set("F20", "=RAND.LINDY(12)")
s.set("E21", "Even odds of lasting another (years)"); s.set("F21", "=SIM.MEDIAN(F20)")
s.set("E22", "Nine chances in ten of lasting another (years)"); s.set("F22", "=SIM.PERCENTILE(F20,0.1)")
s.fmt("F4", "0.00"); s.fmt("F5 F10 F12 F13", "#,##0"); s.fmt("F6 F7 F11 F16", "0%"); s.fmt("F17", "0.0")
s.fmt("F20 F21 F22", "0.0")
s.save("lifetimes.tm")
print("ok, learning")
