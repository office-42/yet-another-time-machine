# Methods for any domain: research notes

Research notes on domain-agnostic methods that learn from a **table of past cases** or from **expert
judgment**, written for implementation as spreadsheet functions (C, one scalar per call, arguments
are ranges and numbers). Existing features (RAND.* distributions, LHS, SIM.*, ETS, linear forecast,
AR(1), benchmarks, growth curves, Markov, Elo, GBM, Brier/log score, BAYES, EXTREMIZE, POOL.ODDS,
LAPLACE, REFCLASS) are assumed and not repeated. Roadmap items touched here: Iman–Conover, survival
curves, beta-binomial base rates, calibration tables, block bootstrap, Gott's rule.

They are what the Learning, Updating, Survival, Scoring and decision functions were built from;
[METHODS.md](METHODS.md) §10–14 describes what exists. The proposals at the end are a superset of
it, and names there may differ from what was built.

## 0. Conventions that make these methods fit a spreadsheet

- **Table layout.** `known_y` is n×1, `known_X` is n×p (rows = past cases, columns = features),
  `x0` is 1×p or p×1 (the new case). Blank/non-numeric rows are dropped pairwise. Categorical
  features: the user adds 0/1 dummy columns (or use Gower distance, §1).
- **Fit once, draw many.** A `RAND.*` function that fits a model (kNN, OLS, KDE, logistic) is called
  once per iteration (10⁴+ times) with the *same* ranges. Cache the fit keyed by (function id, range
  addresses, sheet edit-version); only the final draw uses the iteration's uniform. Without this,
  O(n·p²) fits × 10⁴ iterations dominate runtime.
- **One uniform per draw ⇒ LHS works.** The engine already needs an inverse CDF per distribution
  for Latin hypercube sampling. Every proposed `RAND.*` below is written as `Q(U)` with a single
  U∈(0,1). Trick for mixtures (kNN, KDE, bootstrap): with sorted components,
  `i = floor(U·n)`, `f = U·n − i` ⇒ i is uniform on {0..n−1} and f is U(0,1) independent of i, so
  `x = component_i⁻¹(f)` is an exact draw that keeps LHS stratification across components.
- **Parameter uncertainty.** Where cheap, predictive draws include estimation uncertainty
  (Student-t predictive for OLS, N(η̂, se²) for GLM linear predictors, posterior for conjugate
  models). Plug-in draws understate spread when n is small.
- **Numerical kernels needed** (all small, dense): Householder QR, Cholesky + triangular solves,
  quickselect/partial sort, Φ, Φ⁻¹, t and χ² CDF/inverse, lgamma, bisection/Brent root finder.
- **Errors.** Return `#NUM!` for singular fits, n ≤ parameters, empty support, conformal index > n;
  `#VALUE!` for mismatched range shapes.

## 1. Analog / k-nearest-neighbour forecasting

**What.** Find the k past cases most similar to the new case and use *their outcomes* as the
forecast distribution (Lorenz's "method of analogues" [17]; kNN regression [18]; analog ensembles
in weather reforecasting [19]). Fully domain-agnostic; needs no functional form; the output is a
(weighted) empirical distribution, so it plugs straight into Monte Carlo.

**When.** Many past cases (n ≳ 50), few informative features (p ≲ 5–8), non-linear or interaction
effects, "situations like this one ended how?" questions (project overruns, patient outcomes,
match results, crop yields, incident durations).

```text
Inputs: X (n×p), y (n), x0 (p), k (default round(sqrt(n)), clamp 1..n), kernel, [feature weights v_j]
1  Scale each column: m_j = mean(X[,j]), s_j = sd(X[,j])   (robust: median, 1.4826·MAD or IQR/1.349)
     z_ij = (x_ij − m_j)/s_j ;  z0_j = (x0_j − m_j)/s_j      (s_j = 0 ⇒ drop column)
2  Distance  d_i = sqrt( Σ_j v_j (z_ij − z0_j)² )              (v_j = 1 default)
     Mahalanobis: d_i² = (x_i − x0)ᵀ S⁻¹ (x_i − x0), S = sample covariance (Cholesky solve)
     Mixed types (Gower [21]): d_i = (1/p) Σ_j δ_ij, δ = |x_ij − x0_j|/range_j (numeric),
                                δ = 1{x_ij ≠ x0_j} (categorical)
3  Select the k smallest d (quickselect, O(n)); h = d_(k+1) (adaptive bandwidth; if n = k use 1.0001·d_(k))
4  Weights  uniform: w_i = 1
            tricube (LOESS): w_i = (1 − (d_i/h)³)³
            Epanechnikov:    w_i = 1 − (d_i/h)²
            inverse distance: w_i = 1/(d_i + ε)     (ε = 1e-9·median d; exact match dominates)
            Gaussian:        w_i = exp(−d_i²/(2h²))
5  Point forecast  ŷ = Σ w_i y_i / Σ w_i
   Weighted ECDF   F(t) = Σ w_i 1{y_i ≤ t} / Σ w_i ;  quantile Q(p) = min{ y_(j) : F(y_(j)) ≥ p }
   Effective sample size n_eff = (Σw)² / Σw²   (report; warn if < 10)
6  RAND.KNN: sort neighbours by y, cumulative weights C_j; U → first j with C_j ≥ U·ΣW.
   Optional smoothing: y_j + h_y·Φ⁻¹(f) (f from the §0 trick), h_y = Silverman bw of neighbour ys.
7  Regression-adjusted analogs (Beaumont et al. [20]): fit weighted OLS y ~ x on the k neighbours,
   use y_i* = y_i + β̂ᵀ(x0 − x_i) instead of y_i — removes bias when x0 sits at the edge of its
   neighbourhood.
Choosing k: leave-one-out is cheap — for each i predict from the others, pick k (and kernel)
minimizing mean CRPS (§7) or MSE over a grid k ∈ {3,5,10,20,√n,…}.
Time-series analogs: case vector = (y_t, y_{t−1}, …, y_{t−m+1}) (plus exogenous x), outcome = y_{t+h};
only cases with t + h ≤ now are eligible.
```

**Pitfalls.** Scaling decides the answer (an unscaled "revenue in $" swamps everything). Curse of
dimensionality: with p > ~8 all distances look alike — select features or use v_j. kNN cannot
extrapolate beyond observed y (report `min d` as an "unusualness" diagnostic; if x0 is outside the
data cloud, prefer regression). Overlapping time-series windows are not independent analogs.
Temporal drift: optionally add recency weight `w_i *= exp(−age_i/τ)`. Ties in d: break by index
deterministically so recalcs are reproducible.

## 2. Regression that learns from a table

### 2.1 Multiple linear regression (OLS) with prediction intervals [22][23]

```text
X̃ = [1 | X] (n×q, q = p+1). Householder QR: X̃ = QR  (never form X̃ᵀX̃ if avoidable; it squares the
condition number). β̂ = R⁻¹(Qᵀy)          e = y − X̃β̂ ;  s² = eᵀe/(n−q) ;  df = n − q (need n > q)
New case x̃0 = [1, x0]:  ŷ0 = x̃0ᵀβ̂
Leverage h0 = x̃0ᵀ(X̃ᵀX̃)⁻¹x̃0 = ‖v‖², where Rᵀv = x̃0 (one triangular solve)
Prediction interval (new observation):  ŷ0 ± t_{1−α/2, df} · s · sqrt(1 + h0)
Confidence interval (mean response):    ŷ0 ± t_{1−α/2, df} · s · sqrt(h0)
Predictive draw (RAND.MLR, exact Bayesian predictive under flat prior [23]):
   y* = ŷ0 + s·sqrt(1 + h0) · T.INV(U, df)
R² = 1 − eᵀe/Σ(y−ȳ)²;  adj R² = 1 − (1−R²)(n−1)/(n−q)
Ridge option (λ > 0, on standardized X, intercept unpenalized): β̂ = (X̃ᵀX̃ + λD)⁻¹X̃ᵀy, D = diag(0,1,…,1);
   solve via QR of the augmented matrix [X̃; sqrt(λ)·D^{1/2}] and [y; 0].
Rank deficiency: |R_jj| < 1e-10·max|R_ii| ⇒ #NUM! (collinear columns).
```

**Pitfalls.** Extrapolation (flag h0 > 2q/n, or 3q/n). n < ~10–20 per predictor overfits.
Heteroscedastic or skewed y: fit log y, interval `exp(ŷ0 ± …)` (median-unbiased; mean needs
`×exp(s²/2)`). Autocorrelated residuals (time data) ⇒ intervals too narrow — use conformal (§3) or
residual block bootstrap. Omitted-variable/causal misreading: coefficients are predictive, not causal.

### 2.2 Logistic regression for binary events (IRLS / Newton) [24][25]

```text
p_i = 1/(1 + exp(−x̃_iᵀβ)).  Start β = 0 except β_0 = logit(ȳ).
repeat (≤ 50 iterations, stop when max|Δβ| < 1e-8):
   w_i = max(p_i(1−p_i), 1e-10)
   z_i = x̃_iᵀβ + (y_i − p_i)/w_i                         (working response)
   β   = (X̃ᵀWX̃ + λD)⁻¹ X̃ᵀWz                               (weighted LS; Cholesky of X̃ᵀWX̃ + λD)
Equivalent Newton step: β += (X̃ᵀWX̃)⁻¹ X̃ᵀ(y − p)
Output  η0 = x̃0ᵀβ̂,  p0 = 1/(1+e^{−η0});  se0² = x̃0ᵀ(X̃ᵀŴX̃)⁻¹x̃0  (= ‖L⁻¹x̃0‖², LLᵀ = X̃ᵀŴX̃)
CI for p0:   logistic(η0 ± z_{1−α/2}·se0)
Predictive probability integrating parameter uncertainty (probit approximation):
   p̄0 ≈ logistic( η0 / sqrt(1 + π·se0²/8) )
RAND.LOGIT draw: η* = η0 + se0·Φ⁻¹(U1); event = 1{U2 < logistic(η*)}  (or return p* for a SIM.PROB input)
Firth bias-reduced fit (fixes separation, small samples [25]): use the modified score
   U*(β) = Σ_i (y_i − p_i + h_i(1/2 − p_i)) x̃_i,   h_i = diag of W^{1/2}X̃(X̃ᵀWX̃)⁻¹X̃ᵀW^{1/2}
   Newton step β += (X̃ᵀWX̃)⁻¹U*(β).
```

**Pitfalls.** Perfect/quasi separation ⇒ β → ∞ (detect |β| > 30 or non-convergence; use Firth or
λ ≈ 0.1–1 on standardized X). Need ≳10 events per predictor ("EPV"). Rare events: probabilities
biased low. Evaluate with log score/Brier and a reliability table (§7), never with accuracy at a 0.5
threshold. Output is P(event | features) for the *population the table came from*.

### 2.3 Poisson regression for counts per exposure [24][26]

```text
μ_i = t_i·exp(x̃_iᵀβ)   (t_i = exposure: days, km, person-years, visits; offset log t_i)
Start β_0 = log(Σy/Σt), rest 0.   IRLS:
   w_i = μ_i ;  z_i = x̃_iᵀβ + (y_i − μ_i)/μ_i ;  β = (X̃ᵀWX̃)⁻¹X̃ᵀWz
Overdispersion φ̂ = (1/(n−q)) Σ (y_i − μ_i)²/μ_i   (≈1 if Poisson holds)
se0² = φ̂ · x̃0ᵀ(X̃ᵀŴX̃)⁻¹x̃0 ;  expected count for new exposure t0: μ0 = t0·exp(x̃0ᵀβ̂)
Predictive draw: η* = η0 + se0·Φ⁻¹(U1); λ* = t0 e^{η*};
   φ̂ ≤ 1: Y ~ Poisson(λ*);  φ̂ > 1: Y ~ NegBin with mean λ*, size r = λ*/(φ̂−1)  (var = φ̂λ*)
   (single-U version: invert the NB/Poisson CDF by summing the pmf)
```

**Pitfalls.** Excess zeros (zero-inflation) and φ̂ ≫ 1 are common; ignoring them gives far too
narrow intervals. Exposure must be on the same scale for all rows. Log link ⇒ multiplicative effects.

## 3. Quantile regression and conformal prediction

### 3.1 Linear quantile regression [27][28][29]

```text
Pinball (check) loss ρ_τ(u) = u·(τ − 1{u < 0}).   β̂_τ = argmin_β Σ_i ρ_τ(y_i − x̃_iᵀβ)
Exact: linear program (Barrodale–Roberts simplex). Spreadsheet-friendly MM/IRLS (Hunter–Lange):
   β ← OLS start;  ε = 1e-6·(scale of y)
   repeat: r_i = y_i − x̃_iᵀβ ;  w_i = 1/(ε + |r_i|)
           β ← (X̃ᵀWX̃)⁻¹ X̃ᵀ( W·y + (2τ − 1)·1 )          (majorizer of ρ_τ; τ=0.5 ⇒ L1 IRLS)
   until relative change of Σρ_τ(r) < 1e-9 (typ. 20–200 iterations)
Output ŷ_τ(x0) = x̃0ᵀβ̂_τ
Cheap alternative (location–scale): fit ŷ(x) by OLS, fit ŝ(x) by OLS of |e_i| on x (floor at >0),
   standardized residuals u_i = e_i/ŝ(x_i); ŷ_τ(x0) = ŷ(x0) + ŝ(x0)·Q_τ(u).
```

**Pitfalls.** Separately fitted quantiles can cross — sort the predicted quantiles at x0
("rearrangement" [29]). Extreme τ needs n·min(τ, 1−τ) ≫ q. The MM result is approximate (ε).

### 3.2 Split conformal prediction [1][2][3][4][5][58]

**What.** A wrapper that turns *any* point forecaster (regression, kNN, ETS, a human) into intervals
with a finite-sample coverage guarantee, assuming calibration cases and the new case are exchangeable.

```text
Split data: training part (fit model f) and a calibration part of size n not used for fitting
(time series: calibration = most recent out-of-sample forecasts, e.g. rolling-origin errors).
Scores  s_i = |y_i − f(x_i)|,  i = 1..n
k = ceil( (n+1)(1−α) );   if k > n ⇒ interval is (−∞, ∞)  (return #NUM!; need n ≥ 1/α − 1)
q̂ = s_(k)   (k-th smallest; = empirical quantile at level ceil((n+1)(1−α))/n)
C(x0) = [ f(x0) − q̂ ,  f(x0) + q̂ ]
Guarantee: 1−α ≤ P(y0 ∈ C) ≤ 1−α + 1/(n+1)   (upper bound if scores have no ties)
Asymmetric: signed residuals r_i = y_i − f(x_i); lower = f + r_(k_lo), upper = f + r_(k_hi) with
   k_lo = floor((n+1)α/2), k_hi = ceil((n+1)(1−α/2))  (k_lo < 1 ⇒ −∞)
Normalized (adaptive width): s_i = |y_i − f(x_i)|/σ̂(x_i) ⇒ C = f(x0) ± q̂·σ̂(x0)
CQR (conformalized quantile regression): s_i = max( q̂_lo(x_i) − y_i , y_i − q̂_hi(x_i) )
   ⇒ C = [ q̂_lo(x0) − q̂ , q̂_hi(x0) + q̂ ]
Adaptive conformal inference for drifting series (Gibbs & Candès [5]):
   α_{t+1} = α_t + γ(α − err_t), err_t = 1{y_t ∉ C_t}, γ ≈ 0.005–0.05 (a sheet recurrence)
Jackknife+ [58] avoids the split (n refits) — fine for OLS/kNN, heavier otherwise.
```

**Pitfalls.** Guarantee is *marginal* (on average over x), not per case. Non-exchangeable data
(trends, regime change) break it — use recent calibration windows or ACI. Calibration residuals
must be genuinely out-of-sample (in-sample residuals are too small). Multi-step horizons need
horizon-specific calibration errors.

## 4. Kernel density estimation and empirical distributions

### 4.1 KDE and the smoothed bootstrap [14][15]

```text
f̂(x) = (1/(n h)) Σ K((x − x_i)/h),   Gaussian K ⇒ F̂(x) = (1/n) Σ Φ((x − x_i)/h)
Silverman's rule (robust): h = 0.9 · min( ŝ , IQR/1.34 ) · n^(−1/5)
Normal reference (Scott):  h = 1.06 · ŝ · n^(−1/5)   (oversmooths multimodal data)
Epanechnikov kernel: multiply the Gaussian-kernel h by ≈ 2.214 (canonical bandwidth ratio)
KDE.INV(p): solve F̂(x) = p by Brent/bisection on [x_min − 6h, x_max + 6h]
Smoothed bootstrap draw (RAND.KDE): sort data; i = floor(U·n), f = U·n − i;  x* = x_(i+1) + h·Φ⁻¹(f)
Variance-preserving version (Silverman §6.4.1):  x* = x̄ + (x_(i+1) − x̄ + h·Φ⁻¹(f)) / sqrt(1 + h²/ŝ²)
Bounded support:  x ≥ 0 → KDE on log x then exp;  or reflection: if x* < L then x* = 2L − x*
```

**Pitfalls.** Rule-of-thumb h oversmooths bimodal data (offer a manual bandwidth argument).
KDE tails are Gaussian-thin beyond the data — use a GPD tail (§11.4) for risk questions. Small n
(< 20): KDE invents shape; prefer a fitted parametric or the Bayesian bootstrap.

### 4.2 Empirical inverse CDF with interpolation [13]

```text
Sorted x_(1..n). Hyndman–Fan type 7 (Excel PERCENTILE.INC):  h = (n−1)p + 1
   Q(p) = x_(⌊h⌋) + (h − ⌊h⌋)(x_(⌊h⌋+1) − x_(⌊h⌋))     — range limited to [x_(1), x_(n)]
Type 6 (PERCENTILE.EXC): h = (n+1)p;  Type 8 (H&F recommend, median-unbiased): h = (n+1/3)p + 1/3
   (clamp h to [1, n])
RAND.EMPIRICAL(data): x* = Q(U) — continuous, LHS-exact, but never beyond observed min/max.
Tail extension: below p_lo = 1/(n+1) and above p_hi = n/(n+1) attach exponential or GPD tails fitted
to the extreme 10% (semi-parametric; §11.4).
Bayesian bootstrap (Rubin [16]) for "uncertainty about a mean from few data":
   g_i = E_i/ΣE, E_i ~ Exp(1) (i.e. Dirichlet(1,…,1));  draw of mean = Σ g_i x_i  (O(n) per draw)
```

## 5. Bayesian updating from data; partial pooling

### 5.1 Conjugate models [23][30]

```text
Beta–binomial (success rate):  prior Beta(a,b); data s successes in n trials
   posterior Beta(a' = a+s, b' = b+n−s); mean a'/(a'+b'); interval BETA.INV(α/2, a', b') …
   P(next trial succeeds) = a'/(a'+b')            (a=b=1 ⇒ Laplace's rule)
   Predictive for m future trials: P(K=k) = C(m,k)·B(k+a', m−k+b')/B(a',b')
   Draw: θ = BETA.INV(U1, a', b'); K ~ Binomial(m, θ)  (or single-U inverse of the beta-binomial CDF)
   Expert prior from mean μ and "worth ν pseudo-trials": a = μν, b = (1−μ)ν.
Gamma–Poisson (events per exposure):  prior Gamma(shape α, rate β); counts y_i over exposures t_i
   posterior Gamma(α' = α + Σy, β' = β + Σt);  rate mean α'/β'
   Predictive count in new exposure t0: Negative binomial
      P(Y=y) = Γ(α'+y)/(Γ(α') y!) · (β'/(β'+t0))^α' · (t0/(β'+t0))^y
      mean t0α'/β', var (t0α'/β')(1 + t0/β')
   Vague prior: α = 0.5, β → 0 (Jeffreys).  Also the exponential-lifetime model: y = failures,
   t = total time on test including censored units (§9).
Normal, known σ:  prior μ ~ N(μ0, τ0²); data x̄ from n obs
   1/τn² = 1/τ0² + n/σ² ;  μn = τn²(μ0/τ0² + n x̄/σ²) ;  predictive y_new ~ N(μn, σ² + τn²)
Normal, unknown σ (Normal–Inv-χ² prior κ0, μ0, ν0, σ0²):
   κn = κ0+n; μn = (κ0μ0 + n x̄)/κn; νn = ν0+n;
   νnσn² = ν0σ0² + (n−1)s² + κ0 n (x̄−μ0)²/κn
   predictive y_new ~ μn + σn·sqrt(1 + 1/κn)·t_{νn}
   Flat-prior limit: y_new ~ x̄ + s·sqrt(1 + 1/n)·t_{n−1}   ← "RAND.NEXT(data)": the honest small-n
   replacement for RAND.BOOTSTRAP when data are roughly normal (or normal after log).
```

### 5.2 Partial pooling / empirical Bayes shrinkage [31][32][33]

For many related units (stores, hospitals, players, sites, projects) with noisy per-unit estimates.

```text
Normal means, estimates y_j with standard errors σ_j, j = 1..k:
 James–Stein (equal σ, k ≥ 4):  θ̂_j = ȳ + (1 − (k−3)σ²/Σ(y_j − ȳ)²)₊ · (y_j − ȳ)
 General (DerSimonian–Laird τ², the random-effects meta-analysis estimator):
   w_j = 1/σ_j²;  ȳ_w = Σw_j y_j/Σw_j;  Q = Σ w_j (y_j − ȳ_w)²
   τ̂² = max(0, (Q − (k−1)) / (Σw_j − Σw_j²/Σw_j))
   μ̂ = Σ y_j/(σ_j²+τ̂²) / Σ 1/(σ_j²+τ̂²);   se(μ̂)² = 1/Σ 1/(σ_j²+τ̂²)
   B_j = σ_j²/(σ_j² + τ̂²);  θ̂_j = μ̂ + (1 − B_j)(y_j − μ̂);  var ≈ (1 − B_j)σ_j²  (ignores τ̂ uncertainty)
   New, unseen unit (generalized reference class): y ~ N(μ̂, τ̂² + se(μ̂)²)
Rates (beta-binomial EB): p_j = s_j/n_j, m = Σs/Σn, v = var(p_j) (weighted by n_j)
   τ² = max(ε, v − m(1−m)·mean(1/n_j))          (between-unit variance after removing binomial noise)
   ν = m(1−m)/τ² − 1;  a = mν;  b = (1−m)ν;  shrunk rate_j = (a + s_j)/(ν + n_j)
Counts (gamma-Poisson EB): r_j = y_j/t_j, m = Σy/Σt, τ² = max(ε, var(r_j) − m·mean(1/t_j))
   α = m²/τ², β = m/τ²;  shrunk rate_j = (α + y_j)/(β + t_j)
```

**Pitfalls.** Assumes units are exchangeable (no known reason one is special — add covariates via
regression otherwise). k < 5 ⇒ τ̂² unstable and often 0 (full pooling ⇒ overconfident). Plug-in
EB ignores uncertainty in τ̂ (intervals slightly too narrow). Genuinely exceptional units get pulled in.

## 6. Ensembles and model averaging [11][34][35][36][37][38][39][61]

```text
Point forecasts f_mt from M models, outcomes y_t on a common out-of-sample window t = 1..T
Equal weights:        w_m = 1/M   (also: median or 20%-trimmed mean of the M forecasts — robust)
Inverse-MSE (Bates–Granger): w_m = (1/MSE_m) / Σ_k (1/MSE_k)
Akaike-type:          w_m ∝ exp(−Δ_m/2), Δ_m = AIC_m − min AIC   (or softmax of −λ·CRPS_m)
Stacking (Breiman; Granger–Ramanathan constrained): min_w Σ_t (y_t − Σ_m w_m f_mt)², w ≥ 0, Σw = 1
   M small ⇒ Lawson–Hanson NNLS on centred data, or projected gradient onto the simplex.
Shrink estimated weights toward equal: w = λ·ŵ + (1−λ)/M  (λ ≈ 0.5 when T is short)
Distributional combination:
   Linear pool (mixture): F(x) = Σ w_m F_m(x).  Draw: pick m with prob w_m (RAND.DISCRETE), draw from F_m.
      Variance = Σw_m σ_m² + Σw_m(μ_m − μ̄)²  (adds disagreement ⇒ wider; often well calibrated or too wide)
   Quantile averaging (Vincentization [39]): Q(p) = Σ w_m Q_m(p).  Draw: ONE U for all components,
      x = Σ w_m Q_m(U) (comonotone).  Same mean, always sharper than the linear pool.
   Log/geometric pool for event probabilities: odds ∝ Π odds_m^{w_m}  (= POOL.ODDS; then EXTREMIZE)
   Stacking of predictive densities (Yao et al. [38]): maximize Σ_t log Σ_m w_m p_m(y_t) over simplex,
      where p_mt = out-of-sample predictive density of model m at the realized y_t. EM fixed point:
         w_m ← (1/T) Σ_t  w_m p_mt / Σ_k w_k p_kt      (start 1/M, iterate ~200×; monotone ascent)
```

**Forecast combination puzzle.** Equal weights routinely beat "optimal" estimated weights
(Stock & Watson [61]); the explanation is estimation error in the weights when true optimal weights
are near-equal and T is short (Smith & Wallis; Claeskens et al. [36]). Practical rule: default to
equal/trimmed-mean; estimate weights only with long, stable track records; drop clearly bad models
rather than down-weighting everyone. Diversity (low error correlation) matters more than individual
accuracy. Combine intervals by combining the *distributions* (sample paths), then read off quantiles;
averaging interval endpoints is quantile averaging (sharper), mixing samples is the linear pool (wider)
— choose deliberately and check coverage (§7).

## 7. Scoring any probabilistic forecast

All scores below are negatively oriented (lower is better) unless stated. Scores from Monte Carlo
samples let the sheet backtest *any* model, including judgmental ones.

```text
CRPS from m samples x_i and outcome y (energy form, Gneiting & Raftery [6]; estimators as in [8][9]):
   CRPS = (1/m)Σ|x_i − y| − (1/(2m²)) Σ_i Σ_j |x_i − x_j|
O(m log m): sort x_(1) ≤ … ≤ x_(m); use Σ_{i<j}|x_i − x_j| = Σ_{i=1}^{m} (2i − m − 1)·x_(i)
   CRPS_nrg  = (1/m)Σ|x_i − y| − (1/m²)·Σ_i (2i − m − 1) x_(i)
   CRPS_fair = (1/m)Σ|x_i − y| − (1/(m(m−1)))·Σ_i (2i − m − 1) x_(i)   (unbiased for the true F;
                use to compare ensembles of different size)
Closed form for a normal forecast N(μ,σ²), z = (y−μ)/σ:
   CRPS = σ·[ z(2Φ(z) − 1) + 2φ(z) − 1/√π ]
From K quantiles q_k at τ_k = (k − 0.5)/K:  CRPS ≈ (2/K) Σ_k ρ_{τ_k}(y − q_k)   (CRPS = 2∫₀¹ QS_τ dτ [62])
Quantile (pinball) score: QS_τ(q, y) = (1{y < q} − τ)(q − y) = ρ_τ(y − q)
Interval / Winkler score for central (1−α) interval [l,u] [6][11]:
   IS_α = (u − l) + (2/α)(l − y)·1{y < l} + (2/α)(y − u)·1{y > u}
Weighted interval score (K intervals + median m̃; COVID-hub standard, ≈ CRPS for large K [10]):
   WIS = (1/(K + 1/2)) · [ ½|y − m̃| + Σ_k (α_k/2)·IS_{α_k} ]
PIT (calibration), m samples, randomized for ties [7]:
   u = ( #{x_i < y} + V·(1 + #{x_i = y}) ) / (m + 1),  V ~ U(0,1)
   Calibrated ⇔ u over many cases ~ U(0,1). var(u) > 1/12 (U-shaped histogram) ⇒ overconfident;
   < 1/12 (hump) ⇒ too wide; mean(u) ≠ 0.5 ⇒ biased.  Discrete forecast F: u = F(y−1) + V·(F(y) − F(y−1))
Coverage of N intervals: ĉ = (1/N)Σ 1{l_t ≤ y_t ≤ u_t};  SE = sqrt(ĉ(1−ĉ)/N)  — compare with 1−α
MASE (Hyndman & Koehler [12]; FPP3 [11]); season length m (1 = non-seasonal), scale from TRAINING data:
   MASE = mean_t |e_t| / ( (1/(T−m)) Σ_{t=m+1}^{T} |y_t − y_{t−m}| )
   RMSSE = sqrt( mean_t e_t² / ( (1/(T−m)) Σ (y_t − y_{t−m})² ) )
Skill score vs a reference (climatology/naive): SS = 1 − S_model/S_ref  (CRPSS, BSS …)
Brier decomposition (Murphy [57]), K probability bins, n_k cases, mean forecast f̄_k, hit rate ō_k:
   REL = (1/N)Σ n_k (f̄_k − ō_k)²; RES = (1/N)Σ n_k (ō_k − ō)²; UNC = ō(1 − ō); BS ≈ REL − RES + UNC
Recalibration of probabilities learned from a track record (past p_i, outcomes y_i ∈ {0,1}) [56]:
   Platt/logistic: fit y ~ a + b·logit(p) (§2.2 with one feature) ⇒ p' = logistic(a + b·logit p0).
      b > 1 ⇒ forecasts were underconfident: b is a *data-estimated extremizing factor*.
   Isotonic (PAV): sort by p; blocks start as (value y_i, weight 1); scan left→right, merging a block
      into its predecessor while predecessor value > block value (weighted mean). Calibrated p0 =
      linear interpolation between block mean-p positions. Needs ≳ 200 cases; Platt for fewer.
```

**Pitfalls.** Proper scores only compare forecasts of the *same* outcomes; average over many cases
before comparing. CRPS has the units of y (use CRPSS or scale by MAD for cross-series summaries).
MASE is undefined for constant training series. PIT needs many cases (≥ 50) to say anything. Don't
evaluate on the data used to fit.

## 8. Dependence between inputs [40][41][63]

```text
Gaussian copula (NORTA). Target latent correlation matrix R (p×p, PD), Cholesky R = LLᵀ.
Per iteration: ε_j = Φ⁻¹(U_j) iid;  z = Lε;  u_j = Φ(z_j);  x_j = F_j⁻¹(u_j)   (any marginal's .INV)
Two variables with only spreadsheet formulas:
   Z1 = NORM.S.INV(RAND());  Z2 = ρ*Z1 + SQRT(1−ρ^2)*NORM.S.INV(RAND())
   X1 = LOGNORM.INV(NORM.S.DIST(Z1,TRUE), μ1, σ1);  X2 = BETA.INV(NORM.S.DIST(Z2,TRUE), a, b)
Convert an elicited rank correlation to the latent normal ρ:
   Spearman ρ_S:  ρ = 2·sin(π ρ_S / 6);     Kendall τ:  ρ = sin(π τ / 2)
t copula (tail dependence, ν d.f.): shared w = χ²_ν draw; t_j = z_j / sqrt(w/ν); u_j = T_ν(t_j)
One-factor model (always PSD, easy to elicit "how much does each input ride on the common driver?"):
   z_j = β_j·F + sqrt(1 − β_j²)·ε_j, F, ε_j ~ N(0,1) ⇒ corr(z_i, z_j) = β_i β_j
Repair a non-PD matrix: eigen-clip to ≥ 1e-6 and rescale to unit diagonal (Higham [63] for the
nearest one), or shrink R' = λR + (1−λ)I until Cholesky succeeds.
Iman–Conover (post-hoc, distribution-free, preserves marginals exactly — designed for LHS) [40]:
 1 Draw N iterations of each correlated input independently (LHS fine) → columns X_j.
 2 Score matrix S (N×p): each column a random permutation of van der Waerden scores Φ⁻¹(i/(N+1)).
 3 E = corr(S) = FFᵀ (Cholesky);  C = target rank-correlation matrix = PPᵀ.
 4 T = S·(F⁻¹)ᵀ·Pᵀ  ⇒ sample corr(T) = C exactly.
 5 Reorder each X_j so its ranks equal the ranks of column T_j.  Achieved rank correlation ≈ C.
```

**Implementation shape.** Per-iteration copula: `RAND.COPULA(corr_range, index, [group], [df])`
returns the correlated uniform u_index; the engine computes the group's latent vector once per
iteration on first access and caches it. Iman–Conover needs a two-pass engine: pass 1 draws all
grouped input cells (which must not depend on other random cells), reorders, pass 2 evaluates the
model reading the reordered values.

**Pitfalls.** Correlation of latent normals ≠ correlation of outputs for skewed marginals (Pearson
shrinks; ranks are preserved). Gaussian copula has zero tail dependence ⇒ understates joint crashes
(use t copula). People elicit correlations badly — ask conditional questions ("if X is in its top
10%, what's the chance Y is above its median?") and back out ρ. Ignoring dependence between
positively related costs is the classic cause of too-narrow totals.

## 9. Survival / time-to-event [42][43]

```text
Weibull(shape k, scale λ): F(t) = 1 − exp(−(t/λ)^k);  S(t) = exp(−(t/λ)^k)
   hazard h(t) = (k/λ)(t/λ)^{k−1}  (k<1 early failures, k=1 constant/exponential, k>1 wear-out)
   mean λΓ(1+1/k); median λ(ln 2)^{1/k}
   Draw: t = λ(−ln(1−U))^{1/k}
   Conditional on survival to a (remaining-life question): t = λ[(a/λ)^k − ln(1−U)]^{1/k}  (total age)
Median rank regression (complete data): sort t_(1..n); Benard F_i = (i − 0.3)/(n + 0.4)
   x_i = ln t_(i), y_i = ln(−ln(1 − F_i));  OLS y = k·x + c ⇒ shape k = slope, λ = exp(−c/k)
   (reliability convention regresses x on y: x = a + b·y ⇒ k = 1/b, λ = e^a)
   Right-censored (suspensions), Johnson adjusted ranks: order ALL units by time; for each failure
   with reverse rank R (= n − position + 1): adj_i = (R·adj_prev + (n+1))/(R + 1), adj_0 = 0;
   then F_i = (adj_i − 0.3)/(n + 0.4) for failures only.
MLE with censoring (δ_i = 1 failure, r = Σδ): solve for k (Newton or bisection on k ∈ [0.05, 20]):
   g(k) = Σ_all t_i^k ln t_i / Σ_all t_i^k − 1/k − (1/r) Σ_{δ=1} ln t_i = 0
   λ = ( Σ_all t_i^k / r )^{1/k}
Kaplan–Meier: distinct event times t_1 < … < t_J; d_j events at t_j; n_j at risk just before t_j
   (units censored at t_j count as at risk at t_j)
   Ŝ(t) = Π_{t_j ≤ t} (1 − d_j/n_j)
   Greenwood: Var Ŝ(t) = Ŝ(t)² Σ_{t_j ≤ t} d_j / (n_j(n_j − d_j))
   log-log CI: Ŝ(t)^{exp(± z·σ_L)},  σ_L² = Σ d_j/(n_j(n_j − d_j)) / (ln Ŝ(t))²
Nelson–Aalen cumulative hazard: Ĥ(t) = Σ_{t_j ≤ t} d_j/n_j   (S ≈ e^{−Ĥ}; smoother for small n)
Restricted mean survival to τ: RMST = Σ_j Ŝ(t_{j−1})·(min(t_j, τ) − t_{j−1}) over t_j ≤ τ, + tail to τ
Draw from KM: t* = min{ t_j : Ŝ(t_j) ≤ 1 − U }; if 1 − U < Ŝ(t_J) the draw is beyond the data:
   extend with an exponential tail, rate = d_total / total time at risk, or return t_J + Exp draw.
Constant hazard with exposure = gamma-Poisson (§5.1): failures d over total time T ⇒ rate ~ Gamma(α+d, β+T).
```

**Pitfalls.** Censoring must be non-informative (people who drop out are not systematically
sicker). Ignoring censored units biases survival downward. KM is undefined past the last event;
extrapolation needs a parametric tail. Competing risks (other events preclude the one of interest):
1 − KM overstates cumulative incidence. Weibull MRR is fine for small complete samples; prefer MLE
with heavy censoring.

## 10. Scenarios, value of information and decisions [44][45][46]

```text
What-if sweeps: re-run the simulation for each value of a decision/scenario input with the SAME seed.
   Per-cell streams already give common random numbers ⇒ Var(A − B) = VarA + VarB − 2Cov(A,B) is small,
   so scenario *differences* are precise even when levels are noisy. Two-way tables = nested sweeps.
Options d = 1..D with value cells V_d; S iterations v_{d,s} (same iteration index = same world).
   Current-information value  EV0 = max_d (1/S) Σ_s v_{d,s};  d* = argmax
   Perfect-information value  EV1 = (1/S) Σ_s max_d v_{d,s}
   EVPI = EV1 − EV0 = mean_s ( max_d v_{d,s} − v_{d*,s} )  ≥ 0;  MC SE = sd(regret_s)/sqrt(S)
   P(option d is best) = (1/S) Σ_s 1{v_{d,s} = max_k v_{k,s}}  (ties shared)
   Expected opportunity loss EOL_d = mean_s(max_k v_{k,s} − v_{d,s});  EVPI = min_d EOL_d
EVPPI for one uncertain input X (Strong–Oakley nonparametric regression, local-constant version):
   sort iterations by X; split into B equal-count bins (default B ≈ sqrt(S)/2, e.g. 50 for 10⁴)
   ḡ_{d,b} = mean of v_{d,s} in bin b  (≈ E[V_d | X])
   EVPPI(X) = (1/S) Σ_b n_b · max_d ḡ_{d,b} − max_d mean(v_d)
   Too many bins ⇒ biased up (noise in max); too few ⇒ biased down. EVPPI ≤ EVPI always (clip).
First-order sensitivity (given-data Sobol index, same bins):
   S_X = [ (1/S) Σ_b n_b (ȳ_b − ȳ)² − (B−1)·σ̂_w²/S ] / Var(Y),  σ̂_w² = pooled within-bin variance
Risk attitude — exponential utility with risk tolerance R (Howard [46]):
   CE = −R · ln( (1/S) Σ_s exp(−x_s/R) )   compute stably: c = max_s(−x_s/R);
   CE = −R·( c + ln( (1/S) Σ exp(−x_s/R − c) ) );  R → ∞ gives the mean.
Newsvendor-type decisions (stock, capacity, staffing, buffer): optimal quantity
   q* = F⁻¹( c_u/(c_u + c_o) ) = SIM.PERCENTILE(demand, c_u/(c_u+c_o))  (c_u underage, c_o overage cost)
```

**Pitfalls.** EVPI/EVPPI need all options evaluated on the same iterations (CRN). EVPI is an upper
bound on what *any* study is worth; EVSI (partial, imperfect data) is heavier. Utility choice
matters for irreversible, large-stake decisions; expected value is fine for small repeated ones.

## 11. Other widely used methods, and what is too heavy

### 11.1 Cheap, strong benchmarks for series
```text
Theta method (= SES with drift; Hyndman & Billah [51]; forecast::thetaf source):
   b = OLS slope of y on t = 0..n−1; α and ℓ_n from simple exponential smoothing
   ŷ_{n+h} = ℓ_n + (b/2)·( h − 1 + (1 − (1−α)^n)/α )   (deseasonalize multiplicatively first if seasonal)
Croston / SBA for intermittent demand (spare parts, rare incidents) [50]:
   on periods with y_t > 0:  z ← z + α(y_t − z);  p ← p + α(q − p);  q ← 1   else q ← q + 1
   forecast per period = z/p;  SBA (bias-corrected) = (1 − α/2)·z/p
```

### 11.2 Change-point detection (use only the current regime's data) [48][49]
```text
Single mean shift (offline), O(n) with cumulative sums:
   τ̂ = argmin_τ [ SSE(1..τ) + SSE(τ+1..n) ],  SSE(a..b) = Σx² − (Σx)²/len  over the segment
   accept if SSE_0 − SSE_1 > 2·ln(n)·σ̂²  (BIC-type penalty, σ̂² from SSE_1/(n−2))
CUSUM monitoring (online): S_t = max(0, S_{t−1} + (x_t − μ0 − k)), alarm when S_t > h
   (k = δ/2 for a shift δ; h ≈ 4–5 σ)
Multiple change points: binary segmentation (repeat the single-split test) is simple; PELT (Killick
et al.) is exact with ~linear cost but more code; Bayesian online change-point (Adams–MacKay) gives
P(run length) each step, O(n²) naive — moderate.
```

### 11.3 Expert judgment beyond a single estimate [54][55]
```text
Fit a distribution to elicited quantiles: 2 quantiles → lognormal/normal (RAND.LOGCI/RAND.CI exist);
   3 → metalog (exists); SHELF practice: elicit median then quartiles, fit, show back, revise.
Cooke's Classical Model (performance weights from seed questions with known answers):
   expert gives 5/50/95% for N seed questions; bins probs p = (.05,.45,.45,.05); s = observed fractions
   I(s,p) = Σ s_i ln(s_i/p_i);  calibration C = 1 − χ²_3.CDF(2N·I(s,p))
   information per question: background [L,U] = min/max of all experts' quantiles and realization,
      widened by 10% each side; r_i = bin width/(U−L);  Inf = Σ p_i ln(p_i/r_i); average over questions
   weight ∝ C·Inf·1{C ≥ cutoff}; normalize; combine with a linear pool (§6).
Gott's delta-t rule (Lindy): P(remaining > x·age) = 1/(1 + x) ⇒ c-interval for remaining life:
   [age·(1−c)/(1+c), age·(1+c)/(1−c)]  [64]
```

### 11.4 Extremes: peaks over threshold with a generalized Pareto tail [53]
```text
Threshold u (e.g. 90th–95th percentile); excesses e_i = x_i − u > 0, n_u of them out of n.
PWM estimators (Hosking–Wallis, valid for ξ < 0.5), sorted ascending e_(1..n_u):
   a0 = mean(e);  a1 = (1/n_u) Σ_i ((n_u − i)/(n_u − 1))·e_(i)
   ξ̂ = 2 − a0/(a0 − 2a1);   σ̂ = 2·a0·a1/(a0 − 2a1)
Tail probability: P(X > x) = (n_u/n)·(1 + ξ̂(x − u)/σ̂)^{−1/ξ̂}   (ξ̂ → 0: (n_u/n)·exp(−(x−u)/σ̂))
Return level exceeded once per N observations: x_N = u + (σ̂/ξ̂)·((N·n_u/n)^{ξ̂} − 1)
Tail draw: x = u + (σ̂/ξ̂)·((1 − V)^{−ξ̂} − 1)  — splice onto the empirical body (§4.2) above p = 1 − n_u/n.
```
Pitfalls: threshold choice (mean-excess plot roughly linear above u); ξ̂ ≥ 0.5 ⇒ use MLE; serial
clusters of extremes must be declustered.

### 11.5 Gaussian process regression [47]
```text
Kernel k(x,x') = σ_f² exp(−‖(x − x')/ℓ‖²/2);  K = k(X,X) + σ_n² I = LLᵀ
α = Lᵀ \ (L \ (y − m)) ;  mean f̄0 = m + k0ᵀα ;  v = L \ k0 ;  var = k(x0,x0) − vᵀv (+ σ_n² for a new y)
log marginal likelihood = −½ (y−m)ᵀα − Σ ln L_ii − (n/2) ln 2π
```
O(n³) fit, O(n²) memory: fine in C up to n ≈ 500–2000 with the Cholesky cached. Hyperparameters
(ℓ, σ_f, σ_n) need numerical optimization of the marginal likelihood — moderate; a fallback is
ℓ = median pairwise distance, σ_n² = 10% of var(y). Kernel choice silently encodes assumptions.

### 11.6 Other items and their fit to cell functions
| Method | Verdict |
|---|---|
| ETS state space with AICc model selection, multiplicative errors [52] | Moderate; extends existing Holt–Winters; worthwhile |
| ARIMA / auto.arima | Moderate–heavy (Kalman MLE + search); AR(p) via OLS on lags is cheap |
| Nonlinear least squares (Levenberg–Marquardt) to fit logistic/Gompertz/Bass | Moderate; one routine serves all growth curves (roadmap) |
| Queueing (Erlang C) for service/logistics/health [60] | Cheap: P_wait = [A^c/c!·c/(c−A)] / [Σ_{k<c} A^k/k! + A^c/c!·c/(c−A)], W_q = P_wait/(cμ − λ), A = λ/μ |
| Cox proportional hazards | Moderate (Newton on partial likelihood); not first priority |
| System dynamics (stocks & flows, SIR, Bass) [59] | No new functions: Euler integration down rows + RAND.* parameters already works; ship templates |
| Agent-based models | Too heavy for cell functions; out of scope |
| Random forests, gradient boosting, quantile regression forests, neural nets | Too heavy/opaque; accept externally produced predictions as a column and wrap with conformal (§3.2) |
| Full MCMC hierarchical models | Too heavy; empirical Bayes (§5.2) covers most spreadsheet use |

## 12. Proposed functions, prioritized

Priority 1 = most general, cheap, high leverage. "Cell" means a cell simulated by the engine (like
`SIM.PERCENTILE(cell, p)`); ranges follow Excel's `FORECAST.LINEAR(x, known_y, known_x)` order.

| # | Signature | Returns | § | Cost |
|---|---|---|---|---|
| 1 | `RAND.KNN(x0, known_y, known_X, [k], [kernel], [smooth])` | draw from the outcomes of the k most similar past cases (kernel 0 uniform, 1 tricube, 2 inverse distance; z-scored features) | 1 | O(n p) per fit, cached |
| 2 | `KNN.PERCENTILE(x0, known_y, known_X, p, [k], [kernel])` | weighted analog quantile (p omitted ⇒ weighted mean) | 1 | same |
| 3 | `FORECAST.MLR(x0, known_y, known_X, [ridge])` | multiple-regression point forecast | 2.1 | QR O(n q²) |
| 4 | `FORECAST.MLR.CONFINT(x0, known_y, known_X, [conf], [ridge])` | prediction-interval half-width t·s·√(1+h0) | 2.1 | same |
| 5 | `RAND.MLR(x0, known_y, known_X, [ridge])` | Student-t predictive draw | 2.1 | same |
| 6 | `LOGIT.PROB(x0, known_events, known_X, [ridge], [firth])` | P(event) for the new case (probit-adjusted predictive) | 2.2 | IRLS ≤ 50 iters |
| 7 | `CONFORMAL.CONFINT(residuals, [conf])` | split-conformal half-width s_(⌈(n+1)(1−α)⌉); #NUM! if index > n | 3.2 | sort |
| 8 | `SIM.CRPS(cell, observed, [fair])` and `CRPS(samples_range, observed, [fair])` | CRPS via sorted formula | 7 | O(m log m) |
| 9 | `SIM.PIT(cell, observed)` | randomized PIT value (collect across cases for a calibration histogram) | 7 | O(m) |
| 10 | `SIM.EVPI(option1, option2, …)` | mean(max) − max(mean) | 10 | O(S D) |
| 11 | `SIM.PBEST(k, option1, option2, …)` | P(option k is best) | 10 | O(S D) |
| 12 | `RAND.RATE(events, exposure, [future_exposure], [prior_shape], [prior_rate])` | gamma-Poisson predictive count (or rate if future_exposure omitted) | 5.1 | O(1) |
| 13 | `RAND.PROPORTION(successes, trials, [future_trials], [prior_a], [prior_b])` | beta(-binomial) posterior/predictive draw | 5.1 | O(1) |
| 14 | `RAND.NEXT(data)` | flat-prior normal predictive x̄ + s√(1+1/n)·t_{n−1} | 5.1 | O(n) cached |
| 15 | `SHRINK(estimate, se, estimates, ses)` / `SHRINK.RATE(successes, trials, all_successes, all_trials)` | partial-pooled (empirical Bayes) estimate | 5.2 | O(k) |
| 16 | `RAND.COPULA(corr_matrix, index, [group], [df])` | correlated U(0,1) to feed any `.INV` | 8 | Cholesky once |
| 17 | `RAND.KDE(data, [bandwidth], [lower_bound])`, `KDE.DIST(x, data, [bw], [cumulative])`, `KDE.INV(p, data, [bw])` | smoothed bootstrap draw / density / quantile (Silverman default) | 4.1 | O(n) per eval |
| 18 | `RAND.EMPIRICAL(data, [tail])` | interpolated inverse-ECDF draw (type 7), optional GPD tails | 4.2, 11.4 | sort once |
| 19 | `INTERVAL.SCORE(lowers, uppers, observed, alpha)`, `PINBALL(quantiles, observed, tau)`, `COVERAGE(lowers, uppers, observed)` | mean Winkler score / quantile loss / hit rate over a backtest | 7 | O(N) |
| 20 | `MASE(actuals, forecasts, training, [season])` | scaled error | 7 | O(N) |
| 21 | `CALIBRATE(p0, past_probs, past_outcomes, [method])` | recalibrated probability (0 logistic/Platt, 1 isotonic PAV) | 7 | O(N log N) |
| 22 | `COMBINE.WEIGHT(m, forecasts, actuals, [method])` | weight of model m (0 equal, 1 inverse-MSE, 2 constrained stacking, 3 half-shrunk) | 6 | small NNLS |
| 23 | `SIM.EVPPI(input, option1, option2, …)` and `SIM.SOBOL(input, output, [bins])` | partial EVPI / first-order sensitivity from the run | 10 | sort O(S log S) |
| 24 | `SIM.CE(cell, risk_tolerance)` | certainty equivalent under exponential utility | 10 | O(S) |
| 25 | `WEIBULL.FIT(times, [censored_flags], which, [method])`, `RAND.WEIBULL(shape, scale, [survived_to])`, `KM.SURV(t, times, event_flags)` | shape/scale (MRR or MLE), conditional lifetime draw, Kaplan–Meier S(t) | 9 | O(n log n) |
| 26 | `POISSON.REG(x0, counts, known_X, [exposures], [x0_exposure])` | expected count (with RAND.POISREG for draws incl. overdispersion) | 2.3 | IRLS |
| 27 | `QUANTILE.REG(x0, known_y, known_X, tau)` | linear conditional quantile (MM/IRLS) | 3.1 | 20–200 WLS solves |
| 28 | `FORECAST.THETA(series, h, [season])`, `CROSTON(series, [alpha], [sba])`, `CHANGEPOINT(series)` | cheap series benchmarks / last regime start index | 11 | O(n) |
| 29 | `EXPERT.WEIGHT(q05s, q50s, q95s, realized)` | Cooke classical-model weight (unnormalized C·Inf) | 11.3 | O(N) |
| 30 | `TAIL.PROB(x, data, [threshold_quantile])` | GPD peaks-over-threshold exceedance probability | 11.4 | sort |

Suggested build order: 1–7 (learning from a table), 8–11 (scoring and decisions from existing
simulation output), 12–16 (updating and dependence), then the rest. Items 8–11, 19–20 and 23–24 need
no fitting code at all and immediately make every existing model testable and decision-relevant.

## Sources

1. Angelopoulos, A. & Bates, S. (2021/2023). *A Gentle Introduction to Conformal Prediction and Distribution-Free Uncertainty Quantification*. https://arxiv.org/abs/2107.07511
2. Vovk, V., Gammerman, A. & Shafer, G. *Algorithmic Learning in a Random World* (2nd ed., 2022). https://doi.org/10.1007/978-3-031-06649-8
3. Lei, J., G'Sell, M., Rinaldo, A., Tibshirani, R. & Wasserman, L. (2018). Distribution-free predictive inference for regression. JASA. https://arxiv.org/abs/1604.04173
4. Romano, Y., Patterson, E. & Candès, E. (2019). Conformalized quantile regression. https://arxiv.org/abs/1905.03222
5. Gibbs, I. & Candès, E. (2021). Adaptive conformal inference under distribution shift. https://arxiv.org/abs/2106.00170
6. Gneiting, T. & Raftery, A. (2007). Strictly proper scoring rules, prediction, and estimation. JASA 102. https://sites.stat.washington.edu/raftery/Research/PDF/Gneiting2007jasa.pdf
7. Gneiting, T., Balabdaoui, F. & Raftery, A. (2007). Probabilistic forecasts, calibration and sharpness. JRSS-B 69. https://doi.org/10.1111/j.1467-9868.2007.00587.x
8. scoringrules (Python) — CRPS ensemble estimators (nrg, fair, pwm, int, qd), source. https://github.com/frazane/scoringrules/blob/main/scoringrules/core/crps/_gufuncs.py
9. Jordan, A., Krüger, F. & Lerch, S. (2019). Evaluating probabilistic forecasts with scoringRules. J. Stat. Software 90(12). https://doi.org/10.18637/jss.v090.i12
10. Bracher, J., Ray, E., Gneiting, T. & Reich, N. (2021). Evaluating epidemic forecasts in an interval format. PLOS Comp. Biol. https://doi.org/10.1371/journal.pcbi.1008618
11. Hyndman, R. & Athanasopoulos, G. *Forecasting: Principles and Practice* (3rd ed.): accuracy https://otexts.com/fpp3/accuracy.html ; distributional accuracy https://otexts.com/fpp3/distaccuracy.html ; combinations https://otexts.com/fpp3/combinations.html
12. Hyndman, R. & Koehler, A. (2006). Another look at measures of forecast accuracy. IJF 22. https://doi.org/10.1016/j.ijforecast.2006.03.001
13. Hyndman, R. & Fan, Y. (1996). Sample quantiles in statistical packages. Am. Stat. 50. https://robjhyndman.com/publications/quantiles/
14. Silverman, B. (1986). *Density Estimation for Statistics and Data Analysis*. https://doi.org/10.1007/978-1-4899-3324-9 ; overview https://en.wikipedia.org/wiki/Kernel_density_estimation ; Hansen lecture notes https://users.ssc.wisc.edu/~behansen/709/kernel.pdf
15. Silverman, B. & Young, G. (1987). The bootstrap: to smooth or not to smooth? Biometrika 74. https://doi.org/10.1093/biomet/74.3.469
16. Rubin, D. (1981). The Bayesian bootstrap. Ann. Stat. 9. https://doi.org/10.1214/aos/1176345338
17. Lorenz, E. (1969). Atmospheric predictability as revealed by naturally occurring analogues. J. Atmos. Sci. 26. https://doi.org/10.1175/1520-0469(1969)26<636:APARBN>2.0.CO;2
18. scikit-learn User Guide — Nearest neighbors regression. https://scikit-learn.org/stable/modules/neighbors.html#nearest-neighbors-regression
19. Hamill, T. & Whitaker, J. (2006). Probabilistic quantitative precipitation forecasts based on reforecast analogs. Mon. Wea. Rev. 134. https://doi.org/10.1175/MWR3237.1
20. Beaumont, M., Zhang, W. & Balding, D. (2002). Approximate Bayesian computation in population genetics (regression adjustment). Genetics 162. https://doi.org/10.1093/genetics/162.4.2025
21. Gower, J. (1971). A general coefficient of similarity and some of its properties. Biometrics 27. https://doi.org/10.2307/2528823
22. Ordinary least squares; Prediction interval (Wikipedia). https://en.wikipedia.org/wiki/Ordinary_least_squares ; https://en.wikipedia.org/wiki/Prediction_interval
23. Gelman, A. et al. *Bayesian Data Analysis* (3rd ed.), ch. 2–3, 14. http://www.stat.columbia.edu/~gelman/book/
24. Iteratively reweighted least squares / GLM (Wikipedia; McCullagh & Nelder 1989). https://en.wikipedia.org/wiki/Iteratively_reweighted_least_squares
25. Firth, D. (1993). Bias reduction of maximum likelihood estimates. Biometrika 80. https://doi.org/10.1093/biomet/80.1.27
26. Poisson regression (Wikipedia). https://en.wikipedia.org/wiki/Poisson_regression
27. Koenker, R. & Bassett, G. (1978). Regression quantiles. Econometrica 46. https://doi.org/10.2307/1913643
28. Hunter, D. & Lange, K. (2000). Quantile regression via an MM algorithm. JCGS 9. https://doi.org/10.1080/10618600.2000.10474866
29. Chernozhukov, V., Fernández-Val, I. & Galichon, A. (2010). Quantile and probability curves without crossing. Econometrica 78. https://doi.org/10.3982/ECTA7880
30. Conjugate prior (Wikipedia, table of conjugate distributions). https://en.wikipedia.org/wiki/Conjugate_prior
31. Efron, B. & Morris, C. (1975). Data analysis using Stein's estimator and its generalizations. JASA 70. https://doi.org/10.1080/01621459.1975.10479864 ; https://en.wikipedia.org/wiki/James%E2%80%93Stein_estimator
32. DerSimonian, R. & Laird, N. (1986). Meta-analysis in clinical trials. Control. Clin. Trials 7. https://doi.org/10.1016/0197-2456(86)90046-2
33. Casella, G. (1985). An introduction to empirical Bayes data analysis. Am. Stat. 39. https://doi.org/10.1080/00031305.1985.10479400
34. Bates, J. & Granger, C. (1969). The combination of forecasts. Oper. Res. Q. 20. https://doi.org/10.1057/jors.1969.103
35. Wang, X., Hyndman, R., Li, F. & Kang, Y. (2023). Forecast combinations: an over 50-year review. IJF. https://arxiv.org/abs/2205.04216
36. Claeskens, G., Magnus, J., Vasnev, A. & Wang, W. (2016). The forecast combination puzzle: a simple theoretical explanation. IJF 32. https://doi.org/10.1016/j.ijforecast.2015.12.005 ; Smith, J. & Wallis, K. (2009). A simple explanation of the forecast combination puzzle. OBES 71. https://doi.org/10.1111/j.1468-0084.2008.00541.x
37. Breiman, L. (1996). Stacked regressions. Machine Learning 24. https://doi.org/10.1007/BF00117832
38. Yao, Y., Vehtari, A., Simpson, D. & Gelman, A. (2018). Using stacking to average Bayesian predictive distributions. Bayesian Analysis 13. https://doi.org/10.1214/17-BA1091
39. Lichtendahl, K., Grushka-Cockayne, Y. & Winkler, R. (2013). Is it better to average probabilities or quantiles? Management Science 59. https://doi.org/10.1287/mnsc.1120.1667
40. Iman, R. & Conover, W. (1982). A distribution-free approach to inducing rank correlation among input variables. Commun. Stat. Simul. Comput. 11. https://doi.org/10.1080/03610918208812265 ; Wicklin, R., The geometry of the Iman–Conover transformation. https://blogs.sas.com/content/iml/2021/06/16/geometry-iman-conover-transformation.html
41. Copula (Wikipedia). https://en.wikipedia.org/wiki/Copula_(probability_theory) ; Embrechts, McNeil & Straumann (2002), Correlation and dependence in risk management: properties and pitfalls. https://people.math.ethz.ch/~embrecht/ftp/pitfalls.pdf
42. Kaplan–Meier estimator (Wikipedia) https://en.wikipedia.org/wiki/Kaplan%E2%80%93Meier_estimator ; Wellner, Notes on Greenwood's variance estimator https://sites.stat.washington.edu/jaw/COURSES/580s/582/HO/Greenwood.pdf ; Nelson–Aalen estimator https://en.wikipedia.org/wiki/Nelson%E2%80%93Aalen_estimator
43. ReliaSoft, Life Data Analysis Reference — parameter estimation (rank regression, Benard's approximation, adjusted ranks, MLE). https://help.reliasoft.com/reference/life_data_analysis/lda/parameter_estimation.html ; Weibull distribution https://en.wikipedia.org/wiki/Weibull_distribution
44. Strong, M., Oakley, J. & Brennan, A. (2014). Estimating multiparameter partial EVPI from a probabilistic sensitivity analysis sample. Med. Decis. Making 34. https://doi.org/10.1177/0272989X13505910 ; voi R package overview https://chjackson.github.io/voi/articles/voi.html
45. Plischke, E., Borgonovo, E. & Smith, C. (2013). Global sensitivity measures from given data. EJOR 226. https://doi.org/10.1016/j.ejor.2012.11.047 ; Scalable extensions to given-data Sobol' index estimators (2025). https://arxiv.org/abs/2509.09078
46. Howard, R. (1988). Decision analysis: practice and promise. Management Science 34. https://doi.org/10.1287/mnsc.34.6.679
47. Rasmussen, C. & Williams, C. (2006). *Gaussian Processes for Machine Learning*, ch. 2 (Algorithm 2.1). https://gaussianprocess.org/gpml/chapters/RW2.pdf
48. Adams, R. & MacKay, D. (2007). Bayesian online changepoint detection. https://arxiv.org/abs/0710.3742 ; Killick, R., Fearnhead, P. & Eckley, I. (2012). Optimal detection of changepoints with a linear computational cost (PELT). JASA 107. https://arxiv.org/abs/1101.1438
49. CUSUM (Page 1954) (Wikipedia). https://en.wikipedia.org/wiki/CUSUM
50. Croston (1972) and Syntetos–Boylan approximation — fable `CROSTON()` reference. https://fable.tidyverts.org/reference/CROSTON.html
51. Hyndman, R. & Billah, B. (2003). Unmasking the Theta method. IJF 19. https://doi.org/10.1016/S0169-2070(01)00143-1 ; forecast package source (theta drift formula) https://github.com/robjhyndman/forecast/blob/master/R/theta.R
52. Hyndman, R., Koehler, A., Ord, J. & Snyder, R. (2008). *Forecasting with Exponential Smoothing: The State Space Approach*. https://robjhyndman.com/expsmooth/
53. Hosking, J. & Wallis, J. (1987). Parameter and quantile estimation for the generalized Pareto distribution. Technometrics 29. https://doi.org/10.1080/00401706.1987.10488243 ; Coles, S. (2001). *An Introduction to Statistical Modeling of Extreme Values*. https://doi.org/10.1007/978-1-4471-3675-0
54. Colson, A. & Cooke, R. (2018). Expert elicitation: using the Classical Model to validate experts' judgments. REEP 12. https://doi.org/10.1093/reep/rex022
55. Oakley, J. & O'Hagan, A. SHELF: the Sheffield Elicitation Framework. https://shelf.sites.sheffield.ac.uk/
56. Niculescu-Mizil, A. & Caruana, R. (2005). Obtaining calibrated probabilities from boosting (Platt scaling vs isotonic/PAV; Zadrozny & Elkan 2002). https://www.cs.cornell.edu/~caruana/niculescu.scldbst.crc.rev4.pdf
57. Murphy, A. (1973). A new vector partition of the probability score. J. Appl. Meteor. 12. https://doi.org/10.1175/1520-0450(1973)012<0595:ANVPOT>2.0.CO;2 ; Siegert, S. (2017). Simplifying and generalising Murphy's Brier score decomposition. QJRMS. https://doi.org/10.1002/qj.2985
58. Barber, R., Candès, E., Ramdas, A. & Tibshirani, R. (2021). Predictive inference with the jackknife+. Ann. Stat. 49. https://doi.org/10.1214/20-AOS1965
59. Sterman, J. (2000). *Business Dynamics: Systems Thinking and Modeling for a Complex World*. McGraw-Hill. https://mitsloan.mit.edu/faculty/directory/john-d-sterman
60. Erlang C formula (Wikipedia). https://en.wikipedia.org/wiki/Erlang_(unit)#Erlang_C_formula
61. Stock, J. & Watson, M. (2004). Combination forecasts of output growth in a seven-country data set. J. Forecasting 23. https://doi.org/10.1002/for.928
62. Laio, F. & Tamea, S. (2007). Verification tools for probabilistic forecasts of continuous hydrological variables (CRPS as integral of quantile scores). HESS 11. https://doi.org/10.5194/hess-11-1267-2007
63. Higham, N. (2002). Computing the nearest correlation matrix. IMA J. Numer. Anal. 22. https://doi.org/10.1093/imanum/22.3.329
64. Gott, J. R. (1993). Implications of the Copernican principle for our future prospects. Nature 363. https://doi.org/10.1038/363315a0
