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

for f in launch sales retirement project judgment; do
  run "example $f" 'simulate
' "$samples/$f.tm"
  want "^simulated 10000 iterations"
done

run 'example figures' 'Z1 = =B17
Z2 = =B18
simulate
Z1
Z2
' "$samples/launch.tm"
near Z1 190000 30000
near Z2 0.28 0.05

if [ "$failed" -ne 0 ]; then
  echo "smoke-test: FAILED" >&2
  exit 1
fi
echo "smoke-test: all passed"
