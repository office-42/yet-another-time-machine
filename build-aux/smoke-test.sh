#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The smoke test CI runs, and the one a person can run by hand after a
# build:
#
#     sh build-aux/smoke-test.sh builddir/src/timemachine-calc
#
# The terminal front-end needs no display, so the engine can be exercised
# end to end on a runner with no X and no Wayland.  It is a smoke test,
# not a test suite: it asks of each part only that it still answers, and
# answers about right.  Simulated figures are checked against what theory
# says, within a tolerance, so that a different libm cannot fail it.

set -eu

calc=${1:-builddir/src/timemachine-calc}
if [ ! -x "$calc" ]; then
  echo "smoke-test: no such program: $calc" >&2
  exit 2
fi
case "$calc" in
  /*) ;;
  *) calc="$PWD/$calc" ;;
esac
samples="$(cd "$(dirname "$0")/.." && pwd)/samples"

tab=$(printf '\t')
work=$(mktemp -d "${TMPDIR:-/tmp}/timemachine-smoke.XXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"

failed=0
out=out.txt

# run NAME SCRIPT [FILE]: feeds SCRIPT to the calculator, output in $out.
run () {
  echo "== $1"
  status=0
  printf '%s' "$2" | "$calc" ${3:-} >"$out" 2>err.txt || status=$?
  if [ "$status" -ne 0 ]; then
    echo "   FAIL: timemachine-calc exited $status" >&2
    cat "$out" err.txt >&2
    failed=1
    return 0
  fi
  sed 's/^/   | /' "$out"
}

# want PATTERN: the last run printed a line matching it.
want () {
  if ! grep -q "$1" "$out"; then
    echo "   FAIL: expected /$1/" >&2
    failed=1
  fi
}

# near CELL VALUE TOLERANCE: the cell's value is within tolerance.
near () {
  got=$(awk -F "$tab" -v c="$1" '$1 == c { print $2 }' "$out" | tail -n 1)
  if [ -z "$got" ] || ! awk -v g="$got" -v w="$2" -v t="$3" \
       'BEGIN { d = g - w; if (d < 0) d = -d; exit !(d <= t) }'; then
    echo "   FAIL: $1 is '$got', expected $2 +- $3" >&2
    failed=1
  fi
}

run 'formulas' 'A1 = 10
A2 = 20
B1 = =SUM(A1:A2)*2
B1
C1 = =IF(B1>50,"big","small")
C1
D1 = =2^3^2
D1
D2 = =-2^2
D2
D3 = =ROUND(2.675,2)
D3
D4 = =1/0
D4
E1 = =E2+1
E2 = =E1
E1
F1 = ="a"&1.5&TRUE
F1
G1 = =NPV(10%,100,100,100)
G1
'
want "^B1${tab}60${tab}"
want "^C1${tab}big${tab}"
want "^D1${tab}64${tab}"
want "^D2${tab}4${tab}"
want "^D3${tab}2.68${tab}"
want "^D4${tab}#DIV/0!${tab}"
want "^E1${tab}#CIRC!${tab}"
want "^F1${tab}a1.5TRUE${tab}"
near G1 248.6851991 1e-6

run 'fill and copy move references' 'A1 = 1
A2 = 2
A3 = 3
B1 = =A1*10
filldown B1:B3
B3
copy B1:B3 D1
D2
C1 = =$A$1+A1
fillright C1:E1
E1
'
want "^B3${tab}30${tab}=A3\\*10"
want "^D2${tab}0${tab}=C2\\*10"
want "^E1${tab}3${tab}=\\\$A\\\$1+C1"

# The judgment functions have exact answers.
run 'judgment' 'A1 = =BAYES(0.1,0.9,0.2)
A1
A2 = =EXTREMIZE(0.7)
A2
B1 = 0.9
B2 = 0.2
C1 = 1
C2 = 0
A3 = =BRIER(B1:B2,C1:C2)
A3
'
near A1 0.3333333333 1e-9
near A2 0.8926638565 1e-9
near A3 0.025 1e-12

# Draws from each distribution should have the mean theory says.  With
# 20,000 futures the standard error is under 1% of the spread, so these
# tolerances are several standard errors wide.
run 'distributions' 'iterations 20000
A1 = =RAND.NORMAL(100,15)
A2 = =RAND.PERT(10,20,60)
A3 = =RAND.TRIANGULAR(0,3,9)
A4 = =RAND.POISSON(4)
A5 = =RAND.POISSON(120)
A6 = =RAND.LOGNORMAL(50,20)
A7 = =RAND.BERNOULLI(0.3)
A8 = =RAND.GAMMA(0.5,2)
A9 = =RAND.CI(10,30)
A10 = =RAND.BINOMIAL(100,0.25)
B1 = =SIM.MEAN(A1)
B2 = =SIM.MEAN(A2)
B3 = =SIM.MEAN(A3)
B4 = =SIM.MEAN(A4)
B5 = =SIM.MEAN(A5)
B6 = =SIM.MEAN(A6)
B7 = =SIM.MEAN(A7)
B8 = =SIM.MEAN(A8)
B9 = =SIM.PERCENTILE(A9,0.95)
B10 = =SIM.MEAN(A10)
C1 = =SIM.STDEV(A1)
simulate
B1
B2
B3
B4
B5
B6
B7
B8
B9
B10
C1
'
want "^simulated 20000 iterations"
near B1 100 0.6
near B2 25 0.3
near B3 4 0.08
near B4 4 0.06
near B5 120 0.4
near B6 50 0.6
near B7 0.3 0.015
near B8 1 0.04
near B9 30 0.3
near B10 25 0.15
near C1 15 0.3

# Latin hypercube sampling covers each input's strata exactly once, so
# with only 1,000 futures its means and percentiles are nearly exact --
# far inside what plain Monte Carlo could promise (its standard error
# here would be 0.47 on the first).
run 'latin hypercube' 'sampling latin
iterations 1000
A1 = =RAND.NORMAL(100,15)
A2 = =RAND.PERT(10,20,60)
A3 = =RAND.GAMMA(3,2)
A4 = =RAND.POISSON(4)
B1 = =SIM.MEAN(A1)
B2 = =SIM.PERCENTILE(A1,0.95)
B3 = =SIM.MEAN(A2)
B4 = =SIM.MEAN(A3)
B5 = =SIM.MEAN(A4)
simulate
B1
B2
B3
B4
B5
'
want "Latin hypercube"
near B1 100 0.05
near B2 124.67 0.2
near B3 25 0.05
near B4 6 0.02
near B5 4 0.01

run 'inverse distributions' 'A1 = =T.INV(0.975,3)
A1
A2 = =GAMMA.INV(0.9,3,2)
A2
A3 = =BETA.DIST(BETA.INV(0.3,2,5),2,5,TRUE)
A3
A4 = =T.INV.2T(0.05,1)
A4
A5 = =BETA.INV(0.999,0.5,0.5)
A5
'
near A1 3.182446305 1e-8
near A2 10.644640676 1e-7
near A3 0.3 1e-10
near A4 12.706204736 1e-7
near A5 0.9999975326 1e-9

# The methods particular futures are forecast with, against closed forms.
run 'weather, sport, markets' 'S1 = dry
S2 = rain
T1 = 0.75
U1 = 0.25
T2 = 0.4
U2 = 0.6
A1 = =MARKOV.STEADY("rain",S1:S2,T1:U2)
A1
A2 = =MARKOV.PROB("rain","rain",S1:S2,T1:U2,2)
A2
B1 = rain
C1 = =RAND.MARKOV(B1,$S$1:$S$2,$T$1:$U$2)
D1 = =RAND.MARKOV(C1,$S$1:$S$2,$T$1:$U$2)
E1 = =D1="rain"
F1 = =SIM.MEAN(E1)
V1 = rain
V2 = rain
V3 = dry
V4 = rain
V5 = dry
V6 = dry
A3 = =MARKOV.ESTIMATE("rain","dry",V1:V6)
A3
A4 = =POISSON.MATCH(1.5,1.1,"home")+POISSON.MATCH(1.5,1.1,"draw")+POISSON.MATCH(1.5,1.1,"away")
A4
A5 = =POISSON.SCORE(1.2,0.8,0,0)
A5
A6 = =ELO.EXPECT(1600,1500)
A6
A7 = =BLACKSCHOLES(100,100,0.05,0.2,1)
A7
A8 = =BLACKSCHOLES(100,100,0.05,0.2,1,"put")
A8
A9 = =GBM.PROB(100,100,0.08,0.25,1)
A9
A10 = =BASS(5,0.03,0.38)
A10
A11 = =FORECAST.DRIFT(2,W1:W4)
W1 = 10
W2 = 12
W3 = 13
W4 = 16
A11
A12 = =FORECAST.SNAIVE(1,W1:W4,2)
A12
A13 = =DRAWDOWN(W1:W4)
A13
X1 = 100
X2 = 120
X3 = 90
X4 = 130
A14 = =DRAWDOWN(X1:X4)
A14
A15 = =BRIER.SKILL(Y1:Y4,Z1:Z4)
Y1 = 0.9
Y2 = 0.1
Y3 = 0.8
Y4 = 0.3
Z1 = 1
Z2 = 0
Z3 = 1
Z4 = 0
A15
R1 = 10
R2 = 12
R3 = 14
R4 = 16
R5 = 18
R6 = 20
R7 = 22
R8 = 24
A17 = =FORECAST.DAMPED(3,R1:R8,1)
A17
Q1 = 16
Q2 = 8
Q3 = 4
Q4 = 2
Q5 = 1
Q6 = 0.5
A18 = =FORECAST.AR1(1,Q1:Q6)
A18
A16 = =RAND.SPLITNORMAL(10,1,3)
B16 = =SIM.MEAN(A16)
simulate
F1
B16
'
near A1 0.3846153846 1e-9
near A2 0.46 1e-9
near A3 0.6666666667 1e-9
near A4 1 1e-9
near A5 0.1353352832 1e-9
near A6 0.6400649998 1e-9
near A7 10.45058357 1e-6
near A8 5.573526022 1e-6
near A9 0.5773035262 1e-6
near A10 0.3311986425 1e-6
near A11 20 1e-9
near A12 13 1e-9
near A13 0 1e-12
near A14 0.25 1e-12
near A15 0.85 1e-9
near A17 30 1e-6
near A18 0.25 1e-9
near F1 0.46 0.015
near B16 11.5958 0.06

# The same seed gives the same futures.
run 'seeds' 'A1 = =RAND.NORMAL(0,1)
B1 = =SIM.SAMPLE(A1,7)
seed 42
simulate
B1
simulate
B1
'
first=$(awk -F "$tab" '$1 == "B1" { print $2 }' "$out" | head -n 1)
second=$(awk -F "$tab" '$1 == "B1" { print $2 }' "$out" | tail -n 1)
if [ "$first" != "$second" ]; then
  echo "   FAIL: seed 42 gave $first then $second" >&2
  failed=1
fi

# Holt-Winters on a series with a known trend and season.
series=''
for t in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24; do
  case $(( (t - 1) % 4 )) in 0) s=10 ;; 1) s=-5 ;; 2) s=-10 ;; *) s=5 ;; esac
  series="${series}A$t = $t
B$t = $(( 100 + 2 * t + s ))
"
done
run 'exponential smoothing' "${series}C1 = =FORECAST.ETS(25,B1:B24,A1:A24)
C1
C2 = =FORECAST.ETS(27,B1:B24,A1:A24)
C2
C3 = =FORECAST.ETS.SEASONALITY(B1:B24,A1:A24)
C3
C4 = =FORECAST.LINEAR(25,B1:B24,A1:A24)
C4
"
near C1 160 0.5
near C2 144 0.5
near C3 4 0
near C4 150.2 1.5

run 'formats' 'A1 = 1234.5678
format A1 #,##0.00
A1
A2 = -1234.5
format A2 #,##0.0;(#,##0.0);"nil"
A2
A3 = 0
format A3 #,##0.0;(#,##0.0);"nil"
A3
A4 = 12.5%
A4
A5 = 1234567
format A5 0.00E+00
A5
'
want "^A1${tab}1,234.57${tab}"
want "^A2${tab}(1,234.5)${tab}"
want "^A3${tab}nil${tab}"
want "^A4${tab}12.5%${tab}"
want "^A5${tab}1.23E+06${tab}"

run 'undo and redo' 'A1 = 1
A2 = =A1*2
A1 = 5
A2
undo
A2
redo
A2
filldown A2:A4
A4
undo
A4
'
want "^A2${tab}10${tab}"
want "^A2${tab}2${tab}"
want "^A4${tab}40${tab}=A3\\*2"
want "^A4${tab}${tab}$"

# A cell's draws are its own: adding a cell does not change another's,
# only Draw Again does.
run 'streams' 'draws 3
A1 = =RAND.NORMAL(0,1)
A1
B1 = =RAND.NORMAL(0,1)
B2 = =RAND.PERT(1,2,3)
A1
redraw
A1
'
first=$(awk -F "$tab" '$1 == "A1" { print $2 }' "$out" | sed -n 1p)
second=$(awk -F "$tab" '$1 == "A1" { print $2 }' "$out" | sed -n 2p)
third=$(awk -F "$tab" '$1 == "A1" { print $2 }' "$out" | sed -n 3p)
if [ "$first" != "$second" ] || [ "$first" = "$third" ]; then
  echo "   FAIL: A1 drew $first, then $second after an edit, then $third" >&2
  failed=1
fi

run 'judgment, continued' 'A1 = =LAPLACE(3,10)
A1
B1 = 0.9
B2 = 0.6
B3 = 0.2
A2 = =POOL.ODDS(B1:B3)
A2
C1 = 1.1
C2 = 1.4
C3 = 1.25
C4 = 2
C5 = 0.95
A3 = =REFCLASS(C1:C5,100)
A3
A4 = =RAND.METALOG(10,20,60)
D1 = =SIM.PERCENTILE(A4,0.1)
D2 = =SIM.PERCENTILE(A4,0.5)
D3 = =SIM.PERCENTILE(A4,0.9)
simulate
D1
D2
D3
'
near A1 0.3333333333 1e-9
near A2 0.6 1e-9
near A3 152 1e-9
near D1 10 0.3
near D2 20 0.3
near D3 60 1.2

# Files: written, read back, and the examples still load and simulate.
run 'files' 'A1 = Revenue
B1 = =RAND.PERT(80,100,150)
C1 = tab	and \ backslash
D1 = 1234.5
format D1 $#,##0
iterations 5000
seed 9
save t.tm
save t.csv
'
run 'reload' 'B1
C1
D1
simulate
' t.tm
want "^B1${tab}[0-9.]*${tab}=RAND.PERT(80,100,150)"
want "^C1${tab}tab${tab}and \\\\ backslash"
want "^D1${tab}\\\$1,235${tab}"
want "^simulated 5000 iterations, seed 9"

for f in launch sales retirement project judgment weather football stocks; do
  run "example $f" 'simulate
' "$samples/$f.tm"
  want "^simulated [0-9]* iterations"
done

run 'example figures, weather' 'Z1 = =B26
Z2 = =H17
simulate
Z1
Z2
' "$samples/weather.tm"
near Z2 0.37 0.02

run 'example figures, markets' 'Z1 = =G12
Z2 = =G17
simulate
Z1
Z2
' "$samples/stocks.tm"
near Z2 0.542 0.02

run 'example figures' 'Z1 = =B17
Z2 = =B18
simulate
Z1
Z2
' "$samples/launch.tm"
near Z1 190000 30000
near Z2 0.28 0.05

# ---- Learning from a table --------------------------------------------------

# y = 2 + 3a - 2b exactly: regression must find the plane itself.
table='A1 = 1
A2 = 2
A3 = 3
A4 = 4
A5 = 5
A6 = 6
B1 = 3
B2 = 1
B3 = 4
B4 = 1
B5 = 5
B6 = 9
C1 = =2+3*A1-2*B1
C2 = =2+3*A2-2*B2
C3 = =2+3*A3-2*B3
C4 = =2+3*A4-2*B4
C5 = =2+3*A5-2*B5
C6 = =2+3*A6-2*B6
D1 = 0
D2 = 0
D3 = 1
D4 = 0
D5 = 1
D6 = 1
E1 = 10
E2 = 2
'
run 'learning from a table' "${table}F1 = =MLR.COEF(C1:C6,A1:B6,0)
F2 = =MLR.COEF(C1:C6,A1:B6,1)
F3 = =MLR.COEF(C1:C6,A1:B6,2)
F4 = =FORECAST.MLR(E1:E2,C1:C6,A1:B6)
F5 = =FORECAST.MLR(3.5,A1:A6,B1:B6)-FORECAST.LINEAR(3.5,A1:A6,B1:B6)
F6 = =FORECAST.MLR.CONFINT(3.5,A1:A6,B1:B6)-FORECAST.LINEAR.CONFINT(3.5,A1:A6,B1:B6)
F7 = =KNN.FORECAST(2.1,A1:A6,A1:A6,1)
F8 = =KNN.PERCENTILE(3.4,A1:A6,A1:A6,1,3)
F9 = =CONFORMAL.CONFINT(A1:A6,0.8)
F10 = =CONFORMAL.CONFINT(A1:A6,0.9)
F11 = =QUANTILE.REG(E1:E2,C1:C6,A1:B6,0.5)
F12 = =LOGIT.PROB(9,D1:D6,A1:A6)
F13 = =POISSON.REG(3.5,A1:A6,B1:B6)>0
F1
F2
F3
F4
F5
F6
F7
F8
F9
F10
F11
F12
F13
"
near F1 2 1e-9
near F2 3 1e-9
near F3 -2 1e-9
near F4 28 1e-9
near F5 0 1e-9
near F6 0 1e-9
near F7 2 1e-12
near F8 4 1e-12
near F9 6 1e-12
want "^F10${tab}#NUM!"
near F11 28 0.001
want "^F13${tab}TRUE"

run 'scores and small formulas' 'A1 = 9
A2 = 11
A3 = 12
A4 = 13
A5 = 14
A6 = 15
A7 = 16
A8 = 18
A9 = 20
A10 = 22
B1 = =CRPS(A1:A10,15)
B2 = =CRPS.NORMAL(15,15,4)
B3 = =ERLANG.C(10,1,12)
B4 = =KAPLAN.MEIER(14,A1:A10)
B5 = =WEIBULL.DIST(10,2,10,TRUE)
B6 = =PINBALL(A1:A5,A6:A10,0.9)
B7 = =COVERAGE(A1:A5,A6:A10,A3:A7)
C1 = 1
C2 = 1
C3 = 1
C4 = 1
C5 = 5
C6 = 5
C7 = 5
C8 = 5
B8 = =CHANGEPOINT(C1:C8)
B9 = =SHRINK(20,5,A1:A10,C1:C10)
B10 = =TEXT(2.5,"0")
B11 = =POISSON.DIST(1000,1000,TRUE)
B12 = =BINOM.DIST(5,10,0.5,TRUE)
B1
B2
B3
B4
B5
B6
B7
B8
B9
B10
B11
B12
'
near B1 1 1e-12
near B2 0.9347799 1e-6
near B3 0.4493882 1e-6
near B4 0.5 1e-12
near B5 0.6321206 1e-6
near B6 5.76 1e-9
near B7 1 1e-12
near B8 5 0
near B11 0.5084094 1e-6
near B12 0.6230469 1e-6
want "^B10${tab}3${tab}"

run 'updating, copulas and decisions' 'A1 = =RAND.PROPORTION(7,10)
A2 = =RAND()
A3 = =RAND()
B1 = 1
B2 = 0.6
C1 = 0.6
C2 = 1
D1 = =RAND.COPULA(B1:C2,1)
D2 = =RAND.COPULA(B1:C2,2)
D3 = =RAND.NORMAL(100,10)
simulate 20000
E1 = =SIM.MEAN(A1)
E2 = =SIM.EVPI(A2,A3)
E3 = =SIM.PBEST(1,A2,A3)
E4 = =SIM.CE(D3,20)
E5 = =SIM.CORREL(D1,D2)
E6 = =SIM.PIT(D3,110)
E7 = =SIM.CRPS(D3,100)
E1
E2
E3
E4
E5
E6
E7
'
near E1 0.6667 0.005
near E2 0.1667 0.006
near E3 0.5 0.015
near E4 97.5 0.3
near E5 0.585 0.02
near E6 0.841 0.01
near E7 2.337 0.05

# ---- Deep chains, text futures, correlated months -------------------------

# A chain four thousand cells long, each naming the one below, used to be a
# false #CIRC!: cells are now worked out in the order they depend on each
# other, not by recursion.
awk 'BEGIN { for (i = 1; i < 4000; i++) printf "A%d = =A%d+1\n", i, i + 1;
             print "A4000 = 0"; print "A1" }' >chain.txt
run 'a long chain' "$(cat chain.txt)
"
near A1 3999 0

run 'text futures' 'D1 = dry
D2 = wet
E1 = 0.7
F1 = 0.3
E2 = 0.4
F2 = 0.6
A1 = dry
A2 = =RAND.MARKOV(A1,D1:D2,E1:F2)
A3 = =RAND.MARKOV(A2,D1:D2,E1:F2)
A4 = =RAND.MARKOV(A3,D1:D2,E1:F2)
A5 = =RAND.MARKOV(A4,D1:D2,E1:F2)
A6 = =RAND.MARKOV(A5,D1:D2,E1:F2)
A7 = =RAND.MARKOV(A6,D1:D2,E1:F2)
A8 = =RAND.MARKOV(A7,D1:D2,E1:F2)
A9 = =RAND.MARKOV(A8,D1:D2,E1:F2)
A10 = =RAND.MARKOV(A9,D1:D2,E1:F2)
simulate 20000
B1 = =SIM.PROB(A10,"dry")
B2 = =SIM.MODE(A10)
B1
B2
stats A10
'
near B1 0.5714 0.012
want "^B2${tab}dry"
want "^A10${tab}wet${tab}"

# The months of an exponential-smoothing forecast run high or low
# together: a year's total is as uncertain as the model says.
run 'correlated forecast months' 'simulate
stats H7
' "$samples/sales.tm"
want "sd=[89][0-9][0-9]\."

run 'moving cells, typed numbers' 'A1 = 5
A2 = =A1*2
C1 = =SUM(A1:A2)
move A1:A2 B1
B2
C1
D1 = 1,000
D2 = $5
D3 = =D1+D2
D3
'
want "^B2${tab}10${tab}=B1\*2"
want "^C1${tab}15${tab}=SUM(B1:B2)"
near D3 1005 0

# ---- Pictures, maps and places ------------------------------------------------

run 'pictures' "image $samples/pictures/field-week1.png week1
image $samples/pictures/radar-0.png r0 4 57 14 62
image $samples/pictures/radar-1.png r1 4 57 14 62
A1 = =IMAGE.WIDTH(\"week1\")
A2 = =IMAGE.FRACTION(\"week1\",\"exg\",\">0.1\")
A3 = =IMAGE.MOTION(\"r0\",\"r1\",\"dx\",0.1)*160
A4 = =IMAGE.MOTION(\"r0\",\"r1\",\"dy\",0.1)*120
A5 = =IMAGE.XY(\"r1\",59.5,9,\"u\")
A6 = =IMAGE.GEO(\"r1\",59.5,9,\"gray\")>0
A1
A2
A3
A4
A5
A6
"
near A1 120 0
near A2 0.04 0.01
near A3 2 0.3
near A4 -1 0.3
near A5 0.5 1e-9
want "^A6${tab}TRUE"

run 'maps and places' "map $samples/maps/world.geojson world
A1 = =MAP.REGION(\"world\",59.91,10.75)
A2 = =MAP.AREA(\"world\",\"Norway\")
A3 = =MAP.CONTAINS(\"world\",\"France\",48.86,2.35)
A4 = =MAP.NEAREST(\"world\",0,-30)
A5 = =GEO.DISTANCE(59.91,10.75,48.86,2.35)
A6 = =GEO.DESTINATION(59.91,10.75,500,90,\"lat\")
A7 = =MAP.PROPERTY(\"world\",\"Norway\",\"continent\")
B1 = 60
B2 = 61
B3 = 60.5
C1 = 10
C2 = 11
C3 = 10.2
D1 = 5
D2 = 9
D3 = 7
A8 = =GEO.KRIGE(61,11,B1:B3,C1:C3,D1:D3)
A9 = =GEO.IDW(60,10,B1:B3,C1:C3,D1:D3)
A1
A2
A3
A4
A5
A6
A7
A8
A9
"
want "^A1${tab}Norway"
near A2 394620 8000
want "^A3${tab}TRUE"
want "^A4${tab}Brazil"
near A5 1341 3
near A6 59.8 0.2
want "^A7${tab}Europe"
near A8 9 1e-6
near A9 5 1e-6

# ---- The examples, end to end ----------------------------------------------------

run 'examples, pictures and maps' 'Z1 = =H3
simulate
Z1
' "$samples/nowcast.tm"
near Z1 0.69 0.06

run 'examples, wildfire' 'Z1 = =M5
simulate 1000
Z1
' "$samples/wildfire.tm"
near Z1 0.27 0.07

run 'examples, crops and gauges' 'Z1 = =B13
simulate
Z1
' "$samples/crops.tm"
near Z1 0.51 0.05

run 'examples, storm' 'Z1 = =B24
simulate
Z1
B27
' "$samples/storm.tm"
near Z1 0.23 0.05
want "^B27${tab}sea"

run 'examples, learning' 'Z1 = =K24
Z2 = =K6
simulate
Z1
Z2
' "$samples/analogues.tm"
near Z1 0.29 0.05
near Z2 1.25 0.02

run 'examples, updating' 'Z1 = =B8
Z2 = =E22
simulate
Z1
Z2
' "$samples/abtest.tm"
near Z1 0.95 0.02
near Z2 0.068 0.005

run 'examples, lifetimes' 'Z1 = =F4
Z2 = =F16
Z1
Z2
' "$samples/lifetimes.tm"
near Z1 2.05 0.02
near Z2 0.24 0.01

if [ "$failed" -ne 0 ]; then
  echo "smoke-test: FAILED" >&2
  exit 1
fi
echo "smoke-test: all passed"
