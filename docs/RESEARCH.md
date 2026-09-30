# Research notes: methods for predicting the future

Scope: quantitative and judgmental methods for predicting the future that can be built as spreadsheet
functions or engine features in a C/GTK4 spreadsheet. For each method the notes cover what it is,
when to use it, the formulas you need to implement it, and its pitfalls. Sources are cited as [n] and
listed at the end.

Notation: `y_t` is the observation at time t (t = 1..T), `h` is the forecast horizon, `m` is the
seasonal period, `ŷ_{T+h|T}` is the forecast made at T, `Φ`/`φ` are the standard normal CDF and PDF,
`Φ⁻¹` is the normal quantile function, `U ~ U(0,1)` and `Z ~ N(0,1)`, and `logit(p) = ln(p/(1-p))`.

These notes were gathered while designing Time Machine and are the reference its functions are
written against; [METHODS.md](METHODS.md) is the user-facing account of what is implemented, and
[ROADMAP.md](ROADMAP.md) of what is not yet. Formulas from otexts.com, Wikipedia, Palisade and Oracle
were checked against excerpts of the cited pages; the random-number and variate code against
reference C code (NumPy, R, the xoshiro ports). Everything else is standard textbook material,
flagged where it matters.

## 1. Forecast distributions and benchmark methods

Every forecast function should return a distribution: a point value plus prediction-interval (PI)
quantiles, or a vector of simulated paths. A point value alone invites the "flaw of averages"
(section 6.8). The four benchmarks below are cheap and often hard to beat. Implement them first and
use them as baselines for accuracy measures such as MASE [4][6].

```
Mean:           ŷ_{T+h} = ȳ                          σ_h = σ̂·sqrt(1 + 1/T)
Naive (RW):     ŷ_{T+h} = y_T                        σ_h = σ̂·sqrt(h)
Seasonal naive: ŷ_{T+h} = y_{T+h-m(k+1)}             σ_h = σ̂·sqrt(k+1),  k = floor((h-1)/m)
Drift:          ŷ_{T+h} = y_T + h·(y_T - y_1)/(T-1)  σ_h = σ̂·sqrt(h·(1 + h/T))
PI:             ŷ ± c·σ_h,  c = Φ⁻¹(1-α/2): 80%→1.2816, 90%→1.6449, 95%→1.9600, 99%→2.5758
```
Here σ̂ is the residual standard deviation. These formulas assume uncorrelated, normal residuals
[4]. When residuals are not normal, use bootstrapped residuals instead (section 3.4). fpp3 5.5
builds future paths this way and reads the PIs off their percentiles [4].

## 2. Time-series extrapolation methods

### 2.1 Simple exponential smoothing (SES) [1]
Use for series with no clear trend or seasonality. The forecast is flat.
```
Weighted-average form:  ŷ_{T+1|T} = α·y_T + (1-α)·ŷ_{T|T-1},   0 ≤ α ≤ 1
Component form:         ℓ_t = α·y_t + (1-α)·ℓ_{t-1};   ŷ_{T+h|T} = ℓ_T
Forecast variance (ETS(A,N,N)): σ_h² = σ²·[1 + α²(h-1)]            [5]
```
Estimate α and ℓ_0 together by minimising the SSE of one-step errors `e_t = y_t - ℓ_{t-1}` [1].
A simple initialisation is `ℓ_0 = y_1`. Use Brent/golden-section for one parameter and Nelder–Mead
(parameters clamped to (0,1)) for several.

### 2.2 Holt's linear trend and damped trend [2]
```
Holt:   ŷ_{t+h|t} = ℓ_t + h·b_t
        ℓ_t = α·y_t + (1-α)(ℓ_{t-1} + b_{t-1})
        b_t = β*·(ℓ_t - ℓ_{t-1}) + (1-β*)·b_{t-1}          0 ≤ α, β* ≤ 1
Damped: ŷ_{t+h|t} = ℓ_t + (φ + φ² + … + φ^h)·b_t   = ℓ_t + b_t·φ(1-φ^h)/(1-φ)
        ℓ_t = α·y_t + (1-α)(ℓ_{t-1} + φ·b_{t-1})
        b_t = β*·(ℓ_t - ℓ_{t-1}) + (1-β*)·φ·b_{t-1}         0 < φ < 1 (in practice 0.8 ≤ φ ≤ 0.98)
        As h→∞ the forecast tends to ℓ_T + φ·b_T/(1-φ).
Initialisation (heuristic): ℓ_0 = y_1,  b_0 = y_2 - y_1  (or the OLS slope of the first few points).
Variance, ETS(A,A,N), with β = α·β*:
        σ_h² = σ²·[1 + (h-1)·{α² + αβh + β²h(2h-1)/6}]      [5]
```
Undamped Holt tends to over-forecast at long horizons [2]. Damped trend is one of the most successful
methods for automatic forecasting of many series, so offer it as the default trend method [2].

### 2.3 Holt–Winters seasonal (additive and multiplicative) [3]
Use the additive form when seasonal swings are roughly constant in size, and the multiplicative form
when they grow with the level. Define `k = floor((h-1)/m)` so the forecast always uses the seasonal
index from the last observed season.
```
Additive:
  ŷ_{t+h|t} = ℓ_t + h·b_t + s_{t+h-m(k+1)}
  ℓ_t = α(y_t - s_{t-m}) + (1-α)(ℓ_{t-1} + b_{t-1})
  b_t = β*(ℓ_t - ℓ_{t-1}) + (1-β*)·b_{t-1}
  s_t = γ(y_t - ℓ_{t-1} - b_{t-1}) + (1-γ)·s_{t-m}                      0 ≤ γ ≤ 1-α
Multiplicative:
  ŷ_{t+h|t} = (ℓ_t + h·b_t)·s_{t+h-m(k+1)}
  ℓ_t = α(y_t / s_{t-m}) + (1-α)(ℓ_{t-1} + b_{t-1})
  b_t = β*(ℓ_t - ℓ_{t-1}) + (1-β*)·b_{t-1}
  s_t = γ·y_t/(ℓ_{t-1} + b_{t-1}) + (1-γ)·s_{t-m}
Damped HW (multiplicative): replace h·b_t with (φ+…+φ^h)·b_t and b_{t-1} with φ·b_{t-1}, as in 2.2.
Initialisation (heuristic): ℓ_0 = mean(y_1..y_m);  b_0 = [mean(y_{m+1..2m}) - mean(y_1..y_m)]/m;
  s_{j-m} = y_j - ℓ_0 (additive; normalise to sum 0) or y_j/ℓ_0 (multiplicative; normalise to mean 1).
Variance, ETS(A,A,A), with β = αβ* and k = floor((h-1)/m):
  σ_h² = σ²·[1 + (h-1){α² + αβh + β²h(2h-1)/6} + γk{2α + γ + βm(k+1)}]    [5]
```
Fit all parameters, plus optionally the initial states, by minimising the one-step SSE. At least two
full seasons of data are needed. Excel's `FORECAST.ETS` uses the AAA variant (additive error, trend
and season), detects seasonality automatically, and falls back to a linear trend when it cannot find
any. Its interval companion is `FORECAST.ETS.CONFINT`. Match these if you want Excel compatibility
[10]. Seasonality detection is typically the lag of the largest positive ACF peak.

### 2.4 Theta method [9]
Theta did very well in the M3 forecasting competition. Hyndman & Billah (2003) showed it is
equivalent to SES with drift equal to half the OLS slope `b` of y on t [9]. The closed form, as in R
`forecast::thetaf` (recalled from the R source, not checked against it here), is:
```
ŷ_{T+h} = ℓ_T(SES) + (b/2)·[(h-1) + (1 - (1-α)^T)/α]
```
For seasonal data, deseasonalise first (classical multiplicative decomposition), then reseasonalise.

### 2.5 Linear-regression trend with a prediction interval [11]
Use for a stable linear trend or a causal driver. Be cautious about extrapolating far ahead.
```
b1 = Sxy/Sxx,  b0 = ȳ - b1·x̄,  Sxx = Σ(x_i-x̄)²,  Sxy = Σ(x_i-x̄)(y_i-ȳ)
s² = SSE/(n-2)
Mean CI at x0:    ŷ0 ± t_{1-α/2, n-2}·s·sqrt(1/n + (x0-x̄)²/Sxx)
Prediction PI:    ŷ0 ± t_{1-α/2, n-2}·s·sqrt(1 + 1/n + (x0-x̄)²/Sxx)
Multiple regression: Var(pred) = s²·(1 + x0ᵀ(XᵀX)⁻¹x0), df = n - p
```
Excel equivalents are `LINEST`, `TREND` and `FORECAST.LINEAR`. You need a Student-t quantile, which
you can get from the inverse regularised incomplete beta function.

Pitfalls: time-series residuals are usually autocorrelated, so these intervals come out too narrow
(check lag-1 residual autocorrelation or Durbin–Watson). Regressing one trending series on another can
produce spurious correlation. The interval widens only through `(x0-x̄)²`, understating long-horizon risk.

### 2.6 AR(1) and mean reversion [12]
Use for quantities that revert to a mean, such as rates, spreads and utilisation.
```
y_t = c + φ·y_{t-1} + ε_t,  |φ|<1,  μ = c/(1-φ)
Estimate: OLS of y_t on y_{t-1}  (φ̂ = slope, ĉ = intercept, σ̂² = SSE/(n-3))
h-step forecast: ŷ_{T+h} = μ + φ^h·(y_T - μ)
Forecast variance: σ_h² = σ²·(1 - φ^{2h})/(1 - φ²)   → σ²/(1-φ²) as h→∞
Half-life of a shock: ln(0.5)/ln(φ)
Continuous analogue (Ornstein–Uhlenbeck), exact discretisation for simulation:
  x_{t+Δ} = θ + (x_t - θ)·e^{-κΔ} + σ·sqrt((1 - e^{-2κΔ})/(2κ))·Z
```
Pitfall: φ close to 1 is hard to tell apart from a random walk (unit root), and the OLS estimate of
φ is biased downward in small samples.

### 2.7 Growth curves: exponential, logistic, Gompertz, Bass
```
CAGR:            g = (y_T/y_0)^{1/T} - 1;   y_{T+h} = y_T·(1+g)^h;   doubling time = ln2/ln(1+g)
Log-linear fit:  ln y = a + b·t (OLS)  →  g = e^b - 1.  Back-transform bias: E[y] = exp(ŷ_ln + s²/2);
                 PI = exp(PI endpoints on the log scale).  Excel: GROWTH, LOGEST.
Logistic:        y(t) = K / (1 + e^{-r(t - t0)})    (equivalently K/(1 + ((K-y0)/y0)e^{-rt}))  [14]
                 The inflection is at t0, where y = K/2.  Given K, linearise: ln(K/y - 1) = -r·t + r·t0.
                 Practical fit: grid- or golden-search K > max(y) on SSE, with OLS for (r, t0) at
                 each K; or Levenberg–Marquardt on all three.
Gompertz:        y(t) = K·exp(-b·e^{-c t})   (asymmetric S-curve; inflection at y = K/e)
```
**Bass diffusion** models adoption of a new product by innovators and imitators [13]. `p` is the
coefficient of innovation (external influence), `q` the coefficient of imitation (word of mouth),
and `m` the market potential.
```
F(t) = (1 - e^{-(p+q)t}) / (1 + (q/p)·e^{-(p+q)t})         cumulative fraction adopted
f(t) = ((p+q)²/p)·e^{-(p+q)t} / (1 + (q/p)e^{-(p+q)t})²   adoption rate;  N(t) = m·F(t), n(t) = m·f(t)
Peak time t* = ln(q/p)/(p+q)  (when q > p);  F(t*) = (q-p)/(2q);  f(t*) = (p+q)²/(4q)
Typical values: mean p ≈ 0.03 (range 0.01–0.03), mean q ≈ 0.38 (range 0.3–0.5)  [13]
Discrete-time OLS (Bass 1969): n_t = a + b·N_{t-1} + c·N_{t-1}²  with a = pm, b = q-p, c = -q/m
  →  m = (-b - sqrt(b² - 4ac))/(2c),  p = a/m,  q = -c·m
```
Pitfalls: S-curves fitted before the inflection leave K and m almost unidentified (many ceilings fit
the same early data); report a range of K or put a reference-class prior on it. Exponential
extrapolation of early growth is the classic forecasting failure.

## 3. Stochastic processes for simulation

### 3.1 Random walk with drift
`y_{t+1} = y_t + d + σ·Z`. Estimate `d` as the mean of the first differences and `σ` as their
standard deviation. This is the stochastic version of the drift benchmark in section 1.

### 3.2 Geometric Brownian motion (GBM) [15]
Use for quantities that must stay positive and compound multiplicatively, such as prices, revenue or
population under constant growth plus noise.
```
dS = μS dt + σS dW.  Exact solution; no discretisation error for any Δt:
  S_{t+Δt} = S_t·exp((μ - σ²/2)Δt + σ·sqrt(Δt)·Z)
Estimate from log returns r_i = ln(S_i/S_{i-1}) sampled every Δt:
  σ̂ = sd(r)/sqrt(Δt),   μ̂ = mean(r)/Δt + σ̂²/2
Moments: E[S_T] = S_0·e^{μT};  median S_T = S_0·e^{(μ-σ²/2)T}
         P(S_T > K) = Φ((ln(S_0/K) + (μ - σ²/2)T)/(σ·sqrt T))
```
Pitfalls: the Euler scheme `S(1 + μΔt + σ√Δt·Z)` can go negative and is biased; use the exact form.
The drift estimate is very noisy: SE(μ̂) ≈ σ/sqrt(total years of data). Real returns have fat tails
and volatility clustering: offer unit-variance Student-t shocks `Z = T_ν·sqrt((ν-2)/ν)` or
bootstrapped historical returns (section 3.4).

### 3.3 Markov chains [16]
Use for discrete regimes such as recession/expansion, customer states or credit ratings.
```
P[i][j] = P(state j at t+1 | state i at t);  rows sum to 1.  Estimate: P̂_ij = n_ij / Σ_j n_ij
  (add-one smoothing for sparse counts, as in Laplace's rule in section 4.1).
Distribution after n steps: π_{t+n} = π_t · P^n   (row vector; use repeated squaring for P^n)
Stationary π: π = πP with Σπ = 1. Solve (Pᵀ - I)π = 0, replacing one equation with Σπ_i = 1.
Absorbing chains: order P as [[Q, R],[0, I]].
  Fundamental matrix N = (I - Q)⁻¹;  expected steps to absorption t = N·1;  absorption probs B = N·R
Simulation: next state = smallest j with U < cumsum(P[i][0..j]) (or the alias method for speed).
```
Pitfall: a first-order chain assumes the future depends only on the current state and that P does
not change over time. Check this against sojourn-time distributions, which should be geometric.

### 3.4 Bootstrap resampling [4][17][18]
Use when you don't want to assume a distribution: resample history to generate future scenarios.
- **IID bootstrap.** Draw n indices uniformly with replacement. This is valid only for independent
  data, such as i.i.d. residuals or cross-sectional samples.
- **Residual bootstrap for forecast paths** [4]. Fit a model and keep its residuals e_t. Simulate
  `y*_{T+1} = ŷ_{T+1|T} + e*`, where e* is a random residual. Feed y* back into the model state and
  repeat for h steps. Take PIs as percentiles across paths.
- **Moving-block bootstrap (Künsch 1989).** Choose a block length L. Draw ceil(n/L) block starts
  uniformly from [0, n-L], concatenate the blocks and truncate to n. The circular variant wraps
  indices mod n so that end points are not under-sampled.
- **Stationary bootstrap (Politis–Romano 1994).** Draw i_1 uniformly. At each step, with probability
  `p = 1/L` jump to a new uniform index; otherwise use `i_{k+1} = (i_k + 1) mod n`. Block lengths
  are geometric with mean L, and the resampled series stays stationary [17].
- **Block length.** A rule-of-thumb order is `L ∝ n^{1/3}`, for example `L ≈ round(n^{1/3})` for
  variance-type statistics. An automatic data-driven choice is Politis & White (2004) [18].

Pitfalls: a bootstrap never produces values more extreme than the history, so tails are too thin
with short histories. It assumes stationarity: detrend or difference first, bootstrap the stationary
part, then re-integrate. Never resample raw levels of a trending series.

## 4. Judgmental and probabilistic-reasoning methods

### 4.1 Base rates, reference-class forecasting, Laplace's rule
**Outside view first.** Tetlock's superforecasters start from "How often do things of this sort
happen in situations of this sort?" and only then adjust for specifics [22].

**Reference-class forecasting (Flyvbjerg, after Kahneman & Tversky)** [19]:
1. Pick a class of similar past cases, typically 20–30 or more.
2. Build the empirical distribution of the outcome, for example the overrun ratio `r = actual/estimate`.
3. Position the current case in that distribution and set an uplift.
```
Required uplift at certainty level P:  uplift(P) = Q_r(P) - 1   (Q_r = empirical quantile of r)
Adjusted estimate = inside-view estimate × Q_r(P)      e.g. Q_r(0.8) = 1.5 → +50% for P80
```
Spreadsheet function idea: `RCF(ratios_range, estimate, P)`.

**Small-sample base rates** [20]:
```
Laplace rule of succession: P(next success) = (s+1)/(n+2)     (uniform Beta(1,1) prior)
Beta-binomial generally: prior Beta(a,b) → posterior Beta(a+s, b+n-s), mean (a+s)/(a+b+n)
  Jeffreys prior a=b=0.5.  With 0 events in n periods, Laplace gives 1/(n+2) per period.
Rate for "event in the next k periods" at per-period rate λ̂: 1 - (1-λ̂)^k
```
Pitfalls: choosing the reference class is the hard part (too broad is uninformative, too narrow
leaves n too small). Report how sensitive the result is to the class chosen.

### 4.2 Bayesian updating in odds form [21]
```
O(H) = p/(1-p);   O(H|E) = O(H) × LR,   LR = P(E|H)/P(E|¬H)   (the Bayes factor)
Log-odds form:    logit p_post = logit p_prior + Σ_i ln LR_i    (assumes evidence items are
                  conditionally independent given H)
p_post = O_post/(1 + O_post)
Example: prior 20% (odds 0.25), evidence 3× likelier if H is true → odds 0.75 → p = 42.9%
```
This suits a "belief ledger" feature: a prior cell plus a column of evidence items with LRs gives a
running posterior. Pitfalls: correlated evidence items get double-counted, so merge them into one LR.
People over-update on vivid evidence and under-weight base rates; superforecasters make many small,
frequent updates, for example 0.40 → 0.35 [22].

### 4.3 Superforecasting practices (Good Judgment Project) [22][23][24]
Good Judgment Project (GJP) practices that improve accuracy: decompose ("Fermi-ize") a question
into estimable parts; anchor on base rates, then adjust with inside-view specifics; use fine-grained
probabilities; update often in small steps; balance under- and over-reaction; keep score with the
Brier score [22][23]. Probability training, teaming, and tracking the top performers
("superforecasters") each improved accuracy in the IARPA tournaments [23][24]. Features that follow:
every probability cell keeps a timestamped history; questions can be resolved TRUE/FALSE and are then
scored automatically; each forecaster or model gets a calibration dashboard (section 5).

**Fermi decomposition with distributions.** Estimate each factor as a distribution and let Monte
Carlo propagate the uncertainty. For independent lognormal factors there is a closed form:
`Π LN(μ_i, σ_i²) = LN(Σμ_i, Σσ_i²)`.

### 4.4 Aggregating multiple forecasts; extremizing [25][26][27]
```
Linear pool:        p̄ = Σ w_i p_i                                   (Σw = 1)
Median:             robust to outliers
Geometric mean of odds:  O* = Π O_i^{w_i},  p* = O*/(1+O*)   ≡ logit p* = Σ w_i logit p_i
Extremized logit aggregator (Satopää et al. 2014) [26]:
    logit p* = a · Σ w_i logit(p_i),   a ≥ 1
    p* = (Π O_i^{w_i})^a / (1 + (Π O_i^{w_i})^a)
Karmarkar recalibration of an average [25]:  p* = p̄^a / (p̄^a + (1-p̄)^a)
```
The geometric mean of odds is a good default aggregate [27]. Averaging pulls the aggregate toward
0.5: it compresses the scale near the ends and fails to combine information that different
forecasters hold separately, and the optimal a in GJP data was greater than 1 [25][26]. Fit `a` by
minimising the Brier or log score on resolved questions rather than hard-coding it. More independent,
diverse forecasters justify a larger a; a single forecaster, or a crowd sharing the same information,
justifies a ≈ 1. Weight forecasters by recency and past accuracy. (Metaculus's community prediction
is a recency-weighted median [28].)

### 4.5 Expert elicitation of a distribution (three-point, P10/P90, metalog)
**PERT (beta-PERT)** takes a minimum a, most-likely value (mode) b and maximum c [32]. The
`RiskPert` function in @RISK and the "modified PERT" in Vose's software follow this form.
```
α = 1 + λ(b-a)/(c-a),   β = 1 + λ(c-b)/(c-a),   λ = 4 (standard); a larger λ gives a peakier shape
X = a + (c-a)·Beta(α, β)
Mean = (a + λb + c)/(λ + 2)   [= (a+4b+c)/6 for λ=4]
Var  = (μ-a)(c-μ)/(λ+3)       [= (μ-a)(c-μ)/7 for λ=4]
Classic PERT shortcut: σ ≈ (c-a)/6
```
**Triangular(a, b, c).** Mean `(a+b+c)/3`. See section 8 for the inverse CDF.
**Normal from a P10/P90 or 90% CI.** `μ = (lo+hi)/2`, `σ = (hi-lo)/(2z)`, with
`z = 1.2815515655446004` for P10–P90 and `z = 1.6448536269514722` for P5–P95 (a 90% CI).
**Lognormal from a CI with 0 < lo < hi.** `μ = (ln lo + ln hi)/2`, `σ = (ln hi - ln lo)/(2z)`.
Guesstimate and Squiggle read "lo to hi" as a lognormal 90% CI by default [34]. That is a good
default syntax for a cell: `=5 TO 50`. To parameterise from a mean M and standard deviation S:
`σ² = ln(1 + S²/M²)`, `μ = ln M - σ²/2`.

**Metalog, from Keelin (2016)** [33]. Its quantile function is linear in its coefficients, so it can
be fitted to elicited quantiles in closed form and it has an inverse CDF, which makes it good for
Latin Hypercube Sampling (LHS).
```
3-term: M(y) = a1 + a2·L + a3·(y - 0.5)·L,  L = ln(y/(1-y)),  0 < y < 1
From a symmetric percentile triplet (x_α, x_0.5, x_{1-α}), with k = ln((1-α)/α):
  a1 = x_0.5
  a2 = (x_{1-α} - x_α)/(2k)
  a3 = (x_{1-α} + x_α - 2·x_0.5)/((1-2α)·k)
Feasible (a valid distribution) iff a2 > 0 and |a3|/a2 < 1.66711   [33]
Sampling: X = M(U).  Bounded and semi-bounded variants apply the same form to ln(x-lo) etc.
```
Pitfalls: experts' "min/max" and 90% intervals are usually too narrow (overconfidence), so prefer
eliciting P10/P50/P90 over absolute extremes, and where possible check each expert's calibration on
questions with known answers. Triangular and PERT have hard bounds and cannot produce tail events.

### 4.6 Delphi and structured groups [8]
Delphi runs anonymous, asynchronous rounds, usually 2–4. After each round the facilitator feeds back
the median and interquartile range (IQR) plus the reasoning of outliers. Experts then revise, and the
process stops when answers are stable. The spreadsheet features that fit are a response range, round
columns, automatic median/IQR per round, and a stability test.

**Prediction markets:** the price can be read as a probability, but there is a known
favourite–longshot bias. **Scenario analysis:** weighted scenarios give `E = Σ p_s·v_s`; the weights
should sum to 1 and be treated as probabilities that get scored later.

### 4.7 Gott's delta-t / Lindy-type prior for durations [31]
Use when all you know is how long something has already lasted (t_past). It assumes we observe the
thing at a uniformly random moment of its life.
```
With confidence c: t_future ∈ [t_past·(1-c)/(1+c),  t_past·(1+c)/(1-c)]
   c = 0.95 → [t_past/39, 39·t_past];   c = 0.5 → [t_past/3, 3·t_past];  median = t_past
Survival: P(t_future > x·t_past) = 1/(1+x).   Sampling: t_future = t_past·(1/U - 1)   (infinite mean!)
```
Pitfall: this is only an uninformative prior. Discard it whenever real hazard information exists.

## 5. Scoring, calibration and evaluation

Build these in, so every forecast in the app can be resolved and scored.

### 5.1 Binary probability forecasts [29][28]
```
Brier (binary):   BS = (1/N) Σ (f_t - o_t)²,  o ∈ {0,1};  0 = perfect, 0.25 = always saying 50%
Original Brier (multi-category, as used by GJP): BS = (1/N) Σ_t Σ_i (f_ti - o_ti)²  ∈ [0,2]
Brier skill score: BSS = 1 - BS/BS_ref,  BS_ref = ō(1-ō)  (the "climatology" base rate)
Murphy decomposition, with forecasts grouped into K bins (bin k: n_k items, mean forecast f_k,
  observed frequency ō_k):
  BS = (1/N)Σ n_k(f_k - ō_k)²  -  (1/N)Σ n_k(ō_k - ō)²  +  ō(1-ō)
     =      RELIABILITY       -        RESOLUTION       +  UNCERTAINTY
  The identity is exact only if forecasts take exactly K distinct values; otherwise extra
  within-bin terms appear [29].
Log score:  LS = (1/N) Σ ln(p assigned to the realised outcome)   (higher is better; strictly
  proper; -∞ for a confident miss, so clamp p to [1e-4, 1-1e-4] for display only)
Metaculus baseline score (binary) = 100·(log2(p_o) + 1)   (0 at p=0.5, 100 when perfect) [28]
Metaculus peer score = 100·(ln p_o - mean_j ln p_o,j), i.e. relative to the geometric mean of the
  other forecasters [28]
```
**Calibration table and plot.** Group forecasts into bins, for example 0–5, 5–15, …, 95–100%. For
each bin show n, the mean forecast and the observed frequency. Add a 95% Wilson interval for the
observed frequency:
`(p̂ + z²/2n ± z·sqrt(p̂(1-p̂)/n + z²/4n²)) / (1 + z²/n)`.
The expected calibration error (ECE) is `Σ (n_k/N)·|ō_k - f_k|`. Points below the diagonal at high
forecasts (and above it at low ones) indicate overconfidence.

### 5.2 Continuous and interval forecasts [6][7][30]
```
Pinball (quantile) loss at level τ:  L_τ(q, y) = τ(y - q) if y ≥ q, else (1-τ)(q - y)
Winkler / interval score for a central (1-α) interval [l,u] [7]:
  W = (u - l) + (2/α)(l - y)·1{y<l} + (2/α)(y - u)·1{y>u}
CRPS from M samples x_i [30]:  CRPS = (1/M)Σ|x_i - y| - (1/(2M²))ΣΣ|x_i - x_j|
  ("fair" version: divide the second term by 2M(M-1)).  Compute the double sum in O(M log M):
  sort the samples, then ΣΣ|x_i - x_j| = 2·Σ_{i=1..M} (2i - M - 1)·x_(i)
CRPS for a normal N(μ,σ²): σ·[z(2Φ(z) - 1) + 2φ(z) - 1/√π],  z = (y-μ)/σ
PIT: u_t = F_t(y_t), estimated from samples as the fraction of samples ≤ y_t. A calibrated forecaster
  gives a uniform PIT histogram; a U-shape means intervals are too narrow (overconfident); a hump
  means they are too wide.
Coverage: the fraction of outcomes inside the nominal 80% and 95% PIs should be about 80% and 95%.
Point accuracy: MAE, RMSE,
  MAPE = mean|100·e/y| (undefined at y=0 and asymmetric; avoid as the default)
  MASE = MAE_out / [(1/(T-m)) Σ_{t=m+1..T} |y_t - y_{t-m}|]   (m=1 for non-seasonal) [6]
```
**Rolling-origin evaluation (time-series cross-validation).** Fit on data up to t, forecast t+h,
move t forward one step, and average the errors [6]. Never evaluate on the data used for fitting.

## 6. The Monte Carlo engine

### 6.1 Architecture note
Evaluate uncertain cells as vectors of N trials, one sample per trial, and propagate them through the
dependency graph with element-wise operations. This is the "Stochastic Information Packet" (SIP)
idea of SIPmath and Guesstimate [35][34]. Recalculating the whole sheet N times is much slower.
Store the trial arrays so that any cell can be queried for its histogram, percentiles,
`P(cell > x)` and correlation with other cells.

### 6.2 Number of iterations vs. error [37][39]
```
Mean:        SE = s/sqrt(n);  95% CI = x̄ ± 1.96·SE.  Halving the error needs 4× the iterations.
Iterations for relative precision r at confidence z:  n ≥ (z·s/(r·|x̄|))²
  e.g. coefficient of variation 0.5, r = 1%, z = 1.96 → n ≈ 9,604
Probability (event frequency): SE = sqrt(p̂(1-p̂)/n).  p = 0.01 at n = 10k → SE ≈ 0.001 (10% relative)
Quantile q: asymptotic SE ≈ sqrt(q(1-q)/n) / f(x_q), where f is the density at the quantile.
  Tails need far more iterations.
  Distribution-free CI from order statistics: j,k = n·q ∓ z·sqrt(n·q(1-q)) → [x_(j), x_(k)]
  e.g. n = 10,000, q = 0.95 → ranks 9,457…9,543
```
**Convergence and automatic stopping.** @RISK with iterations set to "Auto" simulates until each
monitored statistic (mean, standard deviation or a percentile) is within a tolerance such as 3% at a
confidence level such as 95% [37]. Crystal Ball's "precision control" stops when the
confidence-interval half-width of the mean, standard deviation or a chosen percentile falls below an
absolute or relative precision [39]. Implementation: check every batch (e.g. every 500 trials), track
running mean, SD and key percentiles, and always show the Monte Carlo SE next to results.

### 6.3 Percentile definition
Use Hyndman–Fan type 7, as in Excel `PERCENTILE.INC`. Sort the samples; set `h = (n-1)p`
(0-based); return `x[⌊h⌋] + (h - ⌊h⌋)(x[⌊h⌋+1] - x[⌊h⌋])`. Also offer `PERCENTILE.EXC` (type 6)
for compatibility. For many cells, avoid a full sort: use `nth_element`/introselect, or sort once and
cache the result.

### 6.4 Latin Hypercube Sampling (McKay, Beckman & Conover 1979) [36][40]
LHS stratifies each input's cumulative probability into n equal intervals and samples each interval
exactly once. Variables are paired at random.
```
For each input j: π_j = random permutation of {0..n-1};  u_ij = (π_j(i) + V_ij)/n, V ~ U(0,1)
  ("median LHS" uses 0.5 instead of V).  x_ij = F_j⁻¹(u_ij).
```
- For outputs monotone in each input, the variance of the mean is never larger than with plain Monte
  Carlo, and usually much smaller [40]. @RISK uses LHS by default [36].
- Every sampler must then be an inverse CDF: fine for normal (AS241), lognormal, triangular, uniform,
  exponential, Weibull and metalog. Beta/PERT, gamma and Student-t need numeric inversion of the
  incomplete beta/gamma (Newton or bisection on a continued fraction, as in Cephes `incbet`/`incbi`
  or Numerical Recipes `betacf`). Discrete distributions use a CDF search.
- `s/sqrt(n)` overstates the error under LHS. Estimate the SE from r independent LHS replicates
  (e.g. 10): SD of the replicate means divided by `sqrt(r)`.
- LHS fixes n in advance, which conflicts with automatic stopping; run fixed-size LHS batches instead.
- Quasi-Monte Carlo (Sobol sequences) is a further upgrade, error closer to O(1/n) for smooth models.

### 6.5 Correlated inputs [41][42]
Ignoring positive correlation between cost items makes the spread of their total far too narrow.
Two standard methods follow. In both, the user specifies a rank-correlation matrix C, as in @RISK
`RiskCorrmat` and Crystal Ball's correlation matrix.

**Gaussian copula via Cholesky.**
```
1. Convert each target Spearman ρ_s to a normal-copula Pearson ρ = 2·sin(π·ρ_s/6)
   (Kendall τ: ρ = sin(π·τ/2)).
2. Cholesky: R = L·Lᵀ.  For each trial draw ε ~ N(0, I_k) and set z = L·ε.
3. u_j = Φ(z_j);  x_j = F_j⁻¹(u_j).   Using LHS normal scores for ε combines stratification with
   correlation.
```
**Iman–Conover (1982) rank reordering.** This works with any marginal samplers, keeps the marginal
samples exactly, and only reorders them [41].
```
Given samples X (n×k) with arbitrary marginals and a target rank-correlation matrix C:
1. Score matrix M (n×k): each column is the van der Waerden scores Φ⁻¹(i/(n+1)), i = 1..n,
   independently randomly permuted.  (These scores give elliptical rather than "pinched"
   scatterplots, which plain ranks would.)
2. E = Pearson correlation of M;  Cholesky E = F·Fᵀ   (removes the spurious sample correlation)
3. Cholesky C = P·Pᵀ
4. T = M · (P·F⁻¹)ᵀ      → corr(T) ≈ C exactly in-sample
5. For each column j, rearrange X[:,j] so that its rank order matches the rank order of T[:,j]
   (sort X[:,j], then put the r-th smallest value in the row holding T[:,j]'s r-th smallest value).
```
**Validation.** C must be symmetric, unit-diagonal and positive definite; users often enter
inconsistent matrices. If Cholesky fails, repair: eigendecompose, clip negative eigenvalues to ε,
rebuild and rescale to unit diagonal (Higham's nearest-correlation-matrix algorithm is the rigorous
version). Warn the user whenever a repair changed their matrix.

### 6.6 Sensitivity analysis and tornado charts [38]
```
Spearman rank correlation of each input's samples with the output's samples:
   ρ_s = Pearson(rank(x), rank(y));  without ties, ρ_s = 1 - 6Σd_i²/(n(n²-1)).
   Use average ranks for ties.  Sort inputs by |ρ_s| to get the tornado bars.
Contribution to variance (Crystal Ball style): ρ_s,i² / Σ_j ρ_s,j², keeping the sign of ρ_s,i
Standardised regression coefficients (SRC): regress standardised y on standardised inputs.
   @RISK reports these, or "mapped values" = SRC × sd(y), the change in output per 1-SD change in
   the input.
Deterministic one-at-a-time tornado: swing each input from its P10 to its P90 with all other inputs
   at base; bar = the resulting output range.
Conditional-mean tornado: the output mean in the lowest vs highest decile of each input's samples.
```
Pitfalls: correlation-based sensitivity misleads when inputs are correlated with each other or an
effect is non-monotonic (U-shaped), so show scatterplots for the top drivers. Tiny |ρ| values are
sampling noise, roughly ±2/sqrt(n).

### 6.7 Fan charts and reporting [43]
For each future period compute pointwise quantiles across trials (e.g. 5, 10, 25, 50, 75, 90, 95)
and draw nested shaded bands, darkest at the centre. The Bank of England fan chart uses a two-piece
(split) normal with mode, uncertainty and skew parameters, with 90% of probability in 10% bands [43].
Split normal sampling: σ1 left of the mode, σ2 right; with probability `σ1/(σ1+σ2)` return
`mode - σ1·|Z|`, otherwise `mode + σ2·|Z|`. Pointwise bands are not bands for whole paths, so also
show 20–50 sample paths ("spaghetti") and `P(value > threshold)` over time. Report mean, SD,
P5/P10/P50/P90/P95, `P(< target)`, the Monte Carlo SE, the iteration count and the seed.

### 6.8 Monte Carlo pitfalls
- **Flaw of averages:** `f(E[X]) ≠ E[f(X)]` for nonlinear f (capacity limits, options, max/min);
  plans built on average inputs are wrong on average [35].
- **Precision is not accuracy:** a million iterations of a mis-specified model is precisely wrong;
  the biggest error sources are structural uncertainty and ranges that are too narrow.
- **Hidden dependence:** omitted correlations, or reusing one draw where two independent draws were
  meant (or the reverse), distort results.
- **Reproducibility:** fixed seed by default and one stream per stochastic cell (section 7.2).

## 7. Random number generation

### 7.1 Core PRNG: xoshiro256** with splitmix64 seeding [44]
xoshiro256** is fast, has a 256-bit state and period 2^256−1, passes BigCrush, and has a jump
function for parallel streams. splitmix64 expands a single 64-bit seed into the 256-bit state.
```c
#include <stdint.h>
static inline uint64_t rotl(const uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

typedef struct { uint64_t s[4]; } xoshiro256;

static uint64_t splitmix64(uint64_t *x) {              /* seeding / hashing */
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
static void xo_seed(xoshiro256 *g, uint64_t seed) {
    for (int i = 0; i < 4; i++) g->s[i] = splitmix64(&seed);  /* never all-zero */
}
static uint64_t xo_next(xoshiro256 *g) {                /* xoshiro256** */
    uint64_t *s = g->s;
    const uint64_t result = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}
/* Equivalent to 2^128 calls of next(): gives non-overlapping sub-streams for threads or cells */
static void xo_jump(xoshiro256 *g) {
    static const uint64_t J[] = { 0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
                                  0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL };
    uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 64; b++) {
            if (J[i] & (UINT64_C(1) << b)) { s0 ^= g->s[0]; s1 ^= g->s[1]; s2 ^= g->s[2]; s3 ^= g->s[3]; }
            xo_next(g);
        }
    g->s[0] = s0; g->s[1] = s1; g->s[2] = s2; g->s[3] = s3;
}
static inline double u01(xoshiro256 *g)      { return (xo_next(g) >> 11) * 0x1.0p-53; }         /* [0,1) */
static inline double u01_open(xoshiro256 *g) { return ((xo_next(g) >> 11) + 0.5) * 0x1.0p-53; } /* (0,1) */
```
Use `u01_open` wherever you take a log or a quantile, because it can never return exactly 0 or 1.
For ranges, use `a + (b-a)*u`. For integers in [0, k), use Lemire's multiply-shift, or rejection
sampling to avoid modulo bias.

### 7.2 Streams and reproducibility
Give each stochastic cell its own generator seeded from `splitmix64(global_seed ^ hash(sheet, cell_id))`,
or take successive `xo_jump` sub-streams. Rejection samplers (gamma, Poisson) consume a variable
number of uniforms, so with one shared stream, editing one cell would shift every other cell's draws.
For full order independence and trivial parallelism, derive each draw from a counter,
`hash(seed, cell_id, trial, k)`. Doug Hubbard's HDR generator (SIPmath 3.0 standard) does this with
a 4-part seed plus a trial index, so results are identical trial-by-trial across platforms, including
Excel [35]. Philox (Random123) is a well-tested counter-based alternative.

### 7.3 Normal variates [48][49][50]
```c
/* Box–Muller: two normals from two uniforms; needs sin/cos */
double r = sqrt(-2.0 * log(u01_open(g))), th = 2.0 * M_PI * u01(g);
z0 = r * cos(th); z1 = r * sin(th);            /* cache z1 for the next call */

/* Marsaglia polar: no trig functions; rejects about 21.5% of pairs */
do { u = 2*u01(g) - 1; v = 2*u01(g) - 1; s = u*u + v*v; } while (s >= 1.0 || s == 0.0);
m = sqrt(-2.0 * log(s) / s); z0 = u * m; z1 = v * m;
```
The Ziggurat method is fastest but more complex. For LHS and copulas you need the inverse CDF,
`z = Φ⁻¹(u)`:

**Acklam's algorithm** [49] is a shorter alternative: three rational-function regions (split at
p = 0.02425 and 0.97575), relative error below 1.15e-9, and full double precision after one Halley
step `e = 0.5*erfc(-x/√2) - p; u = e*√(2π)*exp(x²/2); x -= u/(1 + x*u/2)`. Its 21 coefficients are in
the C file cited in [49]. Prefer AS241 below, which needs no refinement step.

**Wichura AS241 (PPND16)** is accurate to about 1e-16 and is what R's `qnorm` uses [50]. Set
`q = p - 0.5`. Each branch below evaluates a ratio of two degree-7 polynomials by Horner's rule, with
coefficients listed from the highest power down; the last denominator coefficient is 1.
```
|q| ≤ 0.425:  r = 0.180625 - q²;  x = q · N1(r)/D1(r)
  N1: 2509.0809287301226727, 33430.575583588128105, 67265.770927008700853, 45921.953931549871457,
      13731.693765509461125, 1971.5909503065514427, 133.14166789178437745, 3.387132872796366608
  D1: 5226.495278852854561, 28729.085735721942674, 39307.89580009271061, 21213.794301586595867,
      5394.1960214247511077, 687.1870074920579083, 42.313330701600911252, 1.0
else: r = sqrt(-ln(min(p, 1-p)));
  r ≤ 5:  r -= 1.6;  x = N2(r)/D2(r)
  N2: 7.7454501427834140764e-4, .0227238449892691845833, .24178072517745061177, 1.27045825245236838258,
      3.64784832476320460504, 5.7694972214606914055, 4.6303378461565452959, 1.42343711074968357734
  D2: 1.05075007164441684324e-9, 5.475938084995344946e-4, .0151986665636164571966, .14810397642748007459,
      .68976733498510000455, 1.6763848301838038494, 2.05319162663775882187, 1.0
  r > 5:  r -= 5;  x = N3(r)/D3(r)
  N3: 2.01033439929228813265e-7, 2.71155556874348757815e-5, .0012426609473880784386, .026532189526576123093,
      .29656057182850489123, 1.7848265399172913358, 5.4637849111641143699, 6.6579046435011037772
  D3: 2.04426310338993978564e-15, 1.4215117583164458887e-7, 1.8463183175100546818e-5, 7.868691311456132591e-4,
      .0148753612908506148525, .13692988092273580531, .59983220655588793769, 1.0
  if q < 0: x = -x
```
Normal CDF: `Φ(x) = 0.5*erfc(-x/M_SQRT2)`, using C99 `erfc`.

## 8. Other variates [45][46][47]

The C below paraphrases NumPy's `distributions.c` [45] and the cited papers. Use `u` from
`u01_open` unless noted otherwise.

- **Exponential(rate):** `-log(u)/rate`. **Weibull(k, λ):** `λ·(-log(u))^(1/k)`. **Uniform(a, b):**
  `a + (b-a)·u`. **Lognormal(μ, σ):** `exp(μ + σ·Z)` (see 4.5 for fitting from a mean/SD or a CI).
- **Triangular(a, mode c, b), inverse CDF.** Let `Fc = (c-a)/(b-a)`. If `u < Fc`, return
  `a + sqrt(u·(b-a)·(c-a))`; otherwise return `b - sqrt((1-u)·(b-a)·(b-c))`.
- **Gamma(shape α, scale θ), Marsaglia–Tsang (2000)** [46]:
```c
double rgamma(xoshiro256 *g, double alpha) {      /* returns Gamma(alpha, 1); multiply by θ */
    if (alpha < 1.0)                               /* boost trick: G(α) = G(α+1)·U^{1/α} */
        return rgamma(g, alpha + 1.0) * pow(u01_open(g), 1.0 / alpha);
    const double d = alpha - 1.0/3.0, c = 1.0 / sqrt(9.0 * d);
    for (;;) {
        double x, v;
        do { x = rnorm(g); v = 1.0 + c * x; } while (v <= 0.0);
        v = v * v * v;
        double u = u01_open(g);
        if (u < 1.0 - 0.0331 * (x*x) * (x*x)) return d * v;              /* fast squeeze */
        if (log(u) < 0.5 * x*x + d * (1.0 - v + log(v))) return d * v;
    }
}
```
  Chi-square(ν) is `2·Gamma(ν/2)`. Student-t(ν) is `Z / sqrt(ChiSq(ν)/ν)`.
- **Beta(α, β) via gammas.** `X = Ga/(Ga + Gb)` with `Ga ~ Gamma(α)`, `Gb ~ Gamma(β)`. When both
  α, β ≤ 1, NumPy switches to Jöhnk's algorithm and works in logs to avoid underflow [45]. PERT is
  this beta scaled to [a, c] (section 4.5).
- **Poisson(λ).** Use Knuth's multiplication method for λ < 10 and Hörmann's PTRS for λ ≥ 10, which
  is NumPy's threshold [45][47]:
```c
long rpois_knuth(xoshiro256 *g, double lam) {        /* O(λ); exp(-λ) underflows above ~745 */
    double L = exp(-lam), p = 1.0; long k = 0;
    for (;;) { p *= u01(g); if (p > L) k++; else return k; }
}
long rpois_ptrs(xoshiro256 *g, double lam) {         /* Hörmann 1993, transformed rejection */
    double slam = sqrt(lam), loglam = log(lam);
    double b = 0.931 + 2.53 * slam, a = -0.059 + 0.02483 * b;
    double invalpha = 1.1239 + 1.1328 / (b - 3.4), vr = 0.9277 - 3.6224 / (b - 2);
    for (;;) {
        double U = u01(g) - 0.5, V = u01(g), us = 0.5 - fabs(U);
        long k = (long)floor((2 * a / us + b) * U + lam + 0.43);
        if (us >= 0.07 && V <= vr) return k;
        if (k < 0 || (us < 0.013 && V > us)) continue;
        if (log(V) + log(invalpha) - log(a / (us * us) + b) <= -lam + k * loglam - lgamma(k + 1.0))
            return k;
    }
}
```
  Knuth's method uses O(λ) uniforms and breaks when `exp(-λ)` underflows (λ ≳ 745). One third-party
  port returned a count near 750 for every mean above 745 [47], so never use it for large λ.
- **Binomial:** inversion for small n·p, BTPE (Kachitvichyanukul–Schmeiser) for large n·p.
  **Discrete/categorical:** cumulative table plus binary search, or Walker/Vose's alias method (O(1)).
- **Empirical / "SIP" input.** Draw a uniformly random index into a stored sample column, or use the
  column in order to keep the rows of multivariate samples together.

## 9. Suggested function surface (spreadsheet-level)

| Area | Functions / features |
|---|---|
| Distributions (sampled cells) | `NORMAL(μ,σ)`, `LOGNORMAL_CI(lo,hi[,conf])` / `lo TO hi`, `PERT(min,mode,max[,λ])`, `TRIANGULAR`, `UNIFORM`, `BETA`, `GAMMA`, `POISSON`, `BINOMIAL`, `DISCRETE(values,probs)`, `METALOG(p10,p50,p90)`, `SPLITNORMAL(mode,σ1,σ2)`, `RESAMPLE(range)`, `BERNOULLI(p)` |
| Time series | `FC_NAIVE/SNAIVE/DRIFT/MEAN`, `FC_SES`, `FC_HOLT(…,damped)`, `FC_HW(…,m,additive/mult)`, `FC_THETA`, `FC_AR1`, `FC_LINEAR` (+PI), `FC_GROWTH`, `FC_LOGISTIC`, `BASS(p,q,m,t)`; each returns point + `PI(level)` and can emit simulated paths |
| Processes | `GBM_PATH(S0,μ,σ,dt,n)`, `OU_PATH`, `MARKOV_STEP(P,state)`, `MARKOV_DIST(P,π0,n)`, `BOOTSTRAP_PATH(range,block_len,stationary?)` |
| Judgment | `BAYES_ODDS(prior, LR…)`, `LAPLACE(s,n)`, `BETA_POST(a,b,s,n)`, `RCF(ratios,estimate,P)`, `AGG_GEO_ODDS(probs[,weights])`, `EXTREMIZE(p,a)`, `GOTT(t_past,conf)` |
| Scoring | `BRIER(probs,outcomes)` (+decomposition), `LOGSCORE`, `CALIBRATION_TABLE`, `PINBALL`, `WINKLER`, `CRPS(samples,y)`, `PIT`, `MASE`, `COVERAGE` |
| Simulation | Trial-vector engine; `SIM.MEAN/SD/PCTL/PROB(cell>x)`, MC SE display, auto-stop tolerance, LHS toggle, `CORRMAT(range)` via Iman–Conover with positive-definite repair, tornado (Spearman, SRC, one-at-a-time), fan chart with spaghetti paths, fixed seed per workbook and a stream per cell |

## Sources

1. Hyndman & Athanasopoulos, *Forecasting: Principles and Practice* (3rd ed.), §8.1 Simple exponential smoothing. https://otexts.com/fpp3/ses.html
2. FPP3 §8.2 Methods with trend (Holt, damped). https://otexts.com/fpp3/holt.html
3. FPP3 §8.3 Methods with seasonality (Holt–Winters). https://otexts.com/fpp3/holt-winters.html
4. FPP3 §5.5 Distributional forecasts and prediction intervals. https://otexts.com/fpp3/prediction-intervals.html
5. FPP3 §8.7 Forecasting with ETS models (forecast-variance table). https://otexts.com/fpp3/ets-forecasting.html
6. FPP3 §5.8 Evaluating point forecast accuracy (MASE, time-series CV). https://otexts.com/fpp3/accuracy.html ; MASE: https://en.wikipedia.org/wiki/Mean_absolute_scaled_error
7. FPP3 §5.9 Evaluating distributional forecast accuracy (quantile score, Winkler, CRPS). https://otexts.com/fpp3/distaccuracy.html
8. FPP3 §6.3 The Delphi method. https://otexts.com/fpp3/delphimethod.html
9. Hyndman & Billah (2003) "Unmasking the Theta method". https://www.sciencedirect.com/science/article/abs/pii/S0169207001001431 ; forecast::theta_model https://pkg.robjhyndman.com/forecast/reference/theta_model.html
10. Microsoft, FORECAST.ETS function. https://support.microsoft.com/en-us/office/forecast-ets-function-15389b8b-677e-4fbd-bd95-21d464333f41
11. Penn State STAT 415, 8.2 A prediction interval for a new Y. https://online.stat.psu.edu/stat415/lesson/8/8.2
12. Tibshirani, Time Series lecture 6 (ARIMA forecasting). https://www.stat.berkeley.edu/~ryantibs/timeseries-f23/lectures/arima.pdf
13. Bass diffusion model. https://en.wikipedia.org/wiki/Bass_diffusion_model
14. Logistic equation notes (Univ. of Utah). http://www.math.utah.edu/~gustafso/2250logistic.pdf
15. MathWorks, gbm model. https://www.mathworks.com/help/finance/gbm.html ; Gundersen, Simulating GBM. https://gregorygundersen.com/blog/2024/04/13/simulating-gbm/
16. Absorbing Markov chain. https://en.wikipedia.org/wiki/Absorbing_Markov_chain
17. Bühlmann (2002) "Bootstraps for Time Series", Statistical Science 17(1). https://projecteuclid.org/journals/statistical-science/volume-17/issue-1/Bootstraps-for-Time-Series/10.1214/ss/1023798998.pdf
18. Politis & White (2004) "Automatic Block-Length Selection for the Dependent Bootstrap". https://public.econ.duke.edu/~ap172/Politis_White_2004.pdf
19. Reference class forecasting. https://en.wikipedia.org/wiki/Reference_class_forecasting ; Flyvbjerg, "From Nobel Prize to project management" (PMI). https://www.pmi.org/learning/library/nobel-project-management-reference-class-forecasting-8068
20. Rule of succession. https://en.wikipedia.org/wiki/Rule_of_succession
21. Ross, *An Introduction to Bayesian Reasoning and Methods*, ch. 11 Odds and Bayes factors. https://bookdown.org/kevin_davisross/bayesian-reasoning-and-methods/bayes-factor.html
22. Good Judgment, "Ten Commandments for Aspiring Superforecasters". https://goodjudgment.com/philip-tetlocks-10-commandments-of-superforecasting/
23. AI Impacts, "Evidence on good forecasting practices from the Good Judgment Project". https://aiimpacts.org/evidence-on-good-forecasting-practices-from-the-good-judgment-project/
24. Tetlock, Mellers et al. (2014) "Forecasting Tournaments", Current Directions in Psychological Science. https://journals.sagepub.com/doi/10.1177/0963721414534257
25. Baron, Mellers, Tetlock, Stone & Ungar (2014) "Two Reasons to Make Aggregated Probability Forecasts More Extreme", Decision Analysis. https://faculty.wharton.upenn.edu/wp-content/uploads/2015/07/2015---two-reasons-to-make-aggregated-probability-forecasts_1.pdf
26. Satopää et al. (2014) "Combining multiple probability predictions using a simple logit model", IJF 30(2). https://www.sciencedirect.com/science/article/abs/pii/S0169207013001635
27. Sempere, "When pooling forecasts, use the geometric mean of odds". https://forum.effectivealtruism.org/posts/sMjcjnnpoAQCcedL2/when-pooling-forecasts-use-the-geometric-mean-of-odds
28. Metaculus Scores FAQ. https://www.metaculus.com/help/scores-faq/
29. Brier score. https://en.wikipedia.org/wiki/Brier_score ; Stephenson, Coelho & Jolliffe (2008) "Two Extra Components in the Brier Score Decomposition". https://journals.ametsoc.org/view/journals/wefo/23/4/2007waf2006116_1.xml
30. `scores` documentation, CRPS for ensembles. https://scores.readthedocs.io/en/stable/tutorials/CRPS_for_Ensembles.html
31. Gott (1993) "Implications of the Copernican principle for our future prospects", Nature 363. https://www.nature.com/articles/363315a0 ; Doomsday argument. https://en.wikipedia.org/wiki/Doomsday_argument
32. PERT distribution. https://en.wikipedia.org/wiki/PERT_distribution ; SAS DO Loop, "That distribution is quite PERT!". https://blogs.sas.com/content/iml/2012/10/24/pert-distribution.html
33. Metalog distribution. https://en.wikipedia.org/wiki/Metalog_distribution ; Keelin (2016) "The Metalog Distributions", Decision Analysis. https://pubsonline.informs.org/doi/pdf/10.1287/deca.2016.0338 ; feasibility: http://www.metalogdistributions.com/equations/feasibility.html
34. Gooen, "Lognormal > Normal" (Guesstimate blog). https://medium.com/guesstimate-blog/lognormal-normal-833bf413c7a3
35. Probability management (SIPmath, HDR generator, flaw of averages). https://en.wikipedia.org/wiki/Probability_management
36. Palisade @RISK, Sampling Methods. https://help.palisade.com/v8_4/en/@RISK/Simulation-Process/Sampling-Methods.htm ; "Latin Hypercube Versus Monte Carlo Sampling". https://kb.palisade.com/index.php?amp=&id=28&pg=kb.page
37. Palisade, "Convergence Monitoring in @RISK". https://kb.palisade.com/index.php?pg=kb.page&id=85
38. @RISK Sensitivity Analysis (Lumivero help). https://help-risk.lumivero.com/v8_3/en/@RISK/Analysis/Sensitivity-Analysis.htm
39. Oracle Crystal Ball, Precision Control. https://docs.oracle.com/cd/E57185_01/CBREG/ch02s02s01.html ; Confidence Intervals. https://docs.oracle.com/cd/E17236_01/epm.1112/cb_statistical/ch04s04.html
40. Latin hypercube sampling. https://en.wikipedia.org/wiki/Latin_hypercube_sampling
41. Iman & Conover (1982). SAS DO Loop, "The geometry of the Iman-Conover transformation". https://blogs.sas.com/content/iml/2021/06/16/geometry-iman-conover-transformation.html ; Vose, rank-order correlation. https://riskwiki.vosesoftware.com/Rankordercorrelation.php ; EnvStats simulateMvMatrix. https://search.r-project.org/CRAN/refmans/EnvStats/html/simulateMvMatrix.html
42. MathWorks, "Generate Correlated Samples Using Copulas". https://www.mathworks.com/help/stats/copulas-generate-correlated-samples.html
43. Fan chart (time series). https://en.wikipedia.org/wiki/Fan_chart_(time_series) ; fanplot, Bank of England fan charts. https://guyabel.github.io/fanplot/articles/02_boe.html
44. Blackman & Vigna, xoshiro256** reference code. https://prng.di.unimi.it/xoshiro256starstar.c ; C++ port with splitmix64 seeding: https://github.com/Quuxplusone/Xoshiro256ss
45. NumPy `distributions.c` (gamma, beta, Poisson PTRS/Knuth, triangular). https://github.com/numpy/numpy/blob/main/numpy/random/src/distributions/distributions.c
46. Marsaglia & Tsang (2000) "A simple method for generating gamma variables", ACM TOMS 26(3). https://dl.acm.org/doi/10.1145/358407.358414 ; corrected C: https://gist.github.com/fasiha/f289c1d4b9c244a42abee54d1d6206ed
47. Hörmann (1993) "The transformed rejection method for generating Poisson random variables". https://www.sciencedirect.com/science/article/abs/pii/0167668793909974 ; Keeler, survey of Poisson methods: https://hpaulkeeler.com/simulating-poisson-random-variables-survey-methods/ ; large-λ Knuth failure noted in https://github.com/javaNoviceProgrammer/Ngspice_OpenVAF_Enhancements/releases/tag/Enhancement-709_poisson
48. Box–Muller transform. https://en.wikipedia.org/wiki/Box%E2%80%93Muller_transform ; Marsaglia polar method. https://en.wikipedia.org/wiki/Marsaglia_polar_method
49. Acklam's inverse normal: https://stackedboxes.org/2017/05/01/acklams-normal-quantile-function/ ; C implementation: https://github.com/rgcgithub/clamms/blob/master/ltqnorm.c
50. Wichura AS241 as implemented in R `qnorm.c`. https://github.com/wch/r-source/blob/trunk/src/nmath/qnorm.c
