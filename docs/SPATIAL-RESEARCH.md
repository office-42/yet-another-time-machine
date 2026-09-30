# Pictures and maps as data: research notes

Scope: implementable formulas and algorithms for scalar-returning spreadsheet functions (C/GTK4),
plus a map/heatmap view. Degrees in the UI, radians inside. `R = 6371.0088 km` (IUGG mean radius
R1 = (2a+b)/3 of WGS84 [20]) unless stated. 1 nmi = 1.852 km exactly.

> Provenance: facts were checked against extracts of the cited pages and standard references,
> not always the primary page itself. Items tagged **(verify)** should be checked against the
> primary source before they are relied on.

These are the research notes the pictures-and-maps functions were built from. What was built, and
how, is in [METHODS.md](METHODS.md) §15 and [GUIDE.md](GUIDE.md); the proposals at the end are a
superset of what exists.

Conventions used below
```
Image:  W x H pixels, pixel (i,j) covers [i,i+1) x [j,j+1); x right, y DOWN; channels 0..1 floats.
UV:     u = x/W, v = y/H in [0,1]  (resolution-independent addressing)
Geo:    GeoJSON order is [lon, lat] (RFC 7946); spreadsheet API takes (lat, lon) like most users expect.
RNG:    every RAND.* function draws only from the calling cell's stream -> reproducible Monte Carlo.
```

---
## IMAGES

### 1. Reading pixel values

**Luma / grayscale** [1][2][43]. Rec.601 and Rec.709 weights apply to *gamma-encoded* R'G'B' (that is "luma" Y').
For physically meaningful brightness, linearize sRGB first, then use the 709 weights (relative luminance Y).
```
Y'601 = 0.299 R' + 0.587 G' + 0.114 B'          (SD video, JPEG/JFIF YCbCr, most "grayscale" converters)
Y'709 = 0.2126 R' + 0.7152 G' + 0.0722 B'       (HDTV; same weights as sRGB luminance)
sRGB -> linear:  C = C'/12.92            if C' <= 0.04045
                 C = ((C'+0.055)/1.055)^2.4  otherwise
Y_lin = 0.2126 R + 0.7152 G + 0.0722 B          (relative luminance, 0..1)
```
**RGB -> HSV** [3] (inputs 0..1):
```
M = max(R,G,B); m = min(R,G,B); C = M - m
V = M
S = (M == 0) ? 0 : C / M
if C == 0:        H = 0            (undefined hue; return 0 or NaN by option)
elif M == R:      h = (G - B)/C;  if (h < 0) h += 6      // C fmod() keeps sign: fix negatives
elif M == G:      h = (B - R)/C + 2
else:             h = (R - G)/C + 4
H = 60 * h        (degrees, 0 <= H < 360)
```
HSL variant: L = (M+m)/2, S_L = C / (1 - |2L - 1|) (0 if L in {0,1}).

**Bilinear sampling at fractional coordinates** [4] (pixel-centre convention, edge clamp):
```c
float sample_bilinear(const Img *im, float x, float y, int ch) {
    x -= 0.5f; y -= 0.5f;                     // continuous coord -> pixel-centre lattice
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    int x1 = clampi(x0+1,0,im->w-1), y1 = clampi(y0+1,0,im->h-1);
    x0 = clampi(x0,0,im->w-1); y0 = clampi(y0,0,im->h-1);
    float a = px(im,x0,y0,ch), b = px(im,x1,y0,ch), c = px(im,x0,y1,ch), d = px(im,x1,y1,ch);
    return (1-fx)*(1-fy)*a + fx*(1-fy)*b + (1-fx)*fy*c + fx*fy*d;
}
```
Notes: interpolate *values* (e.g. index, dBZ) not palette colours; for categorical rasters (land
cover, palette-coded radar) use nearest neighbour `px(im, floor(x), floor(y))`. With alpha,
use premultiplied values or treat alpha < 0.5 as NoData. For region means over large areas
precompute a summed-area table (integral image): `sum(x0..x1,y0..y1) = S[y1][x1]-S[y0-1][x1]-S[y1][x0-1]+S[y0-1][x0-1]`, O(1) per query.

**Georeferenced images.** Map (lat,lon) to pixel:
```
Equirectangular (plate carree) image with bounds W,S,E,N:
  x = (lon - W)/(E - W) * width ;  y = (N - lat)/(N - S) * height
Web Mercator image (screenshot of a slippy map) with bounds: convert lat -> merc y first (sec. 5):
  y = (m(N) - m(lat)) / (m(N) - m(S)) * height,  m(phi) = ln tan(pi/4 + phi/2)
World file (.pgw/.jgw/.tfw, 6 lines A,D,B,E,C,F) [38]:
  X = A*col + B*row + C ;  Y = D*col + E*row + F   (col,row = pixel CENTRE indices; C,F = centre of UL pixel)
  Inverse (B=D=0): col = (X - C)/A ; row = (Y - F)/E       (E is negative)
```

### 2. Vegetation / water / cloud indices from plain RGB

Chromatic coordinates (normalise out illumination) [5]:
```
R*,G*,B* = channel / 255  (or / max possible)
s = R*+G*+B* ;  r = R*/s,  g = G*/s,  b = B*/s     (if s == 0 -> NoData)
ExG   = 2g - r - b                       Woebbecke et al. 1995            range [-1, 2]
ExR   = 1.4 r - g                        Meyer & Neto 2008
ExGR  = ExG - ExR                        Meyer & Neto 2008; plant if ExGR > 0 (fixed zero threshold)
VARI  = (G - R) / (G + R - B)            Gitelson et al. 2002 (atmospherically resistant; unstable when G+R-B ~ 0)
GLI   = (2G - R - B) / (2G + R + B)      Louhaichi et al. 2001, range [-1, 1]
NGRDI = (G - R) / (G + R)                Tucker 1979, range [-1, 1]
```
[5][6]. Guard denominators: if |den| < 1e-6 return NoData (NaN) and exclude from means. ExG is the
standard input to Otsu for plant/soil segmentation; ExGR works with a fixed 0 threshold.

Water and cloud without NIR are heuristic (true NDWI/MNDWI need NIR/SWIR):
```
Sky/cloud cameras: red/blue ratio  RB = R/B ; cloud if RB > ~0.6-0.8 (clear sky is blue, RB small) [45]
                   or normalised  NBR = (B - R)/(B + R); cloud if NBR < threshold (Otsu on NBR)
Satellite RGB cloud: bright & grey -> V > 0.75 AND S < 0.15 (HSV)  (tune per product)
Open water (RGB):  b > g AND b > r AND V < 0.5, or Otsu on (B - R)/(B + R); verify with a mask
```

**Otsu's method** [7] (maximise between-class variance over a histogram of L bins):
```
Build histogram p[k], k=0..L-1 (normalised to sum 1) over [vmin,vmax] of the index.
mu_T = sum_k k*p[k]
w = 0; mu = 0; best = -1
for t in 0..L-2:
    w  += p[t]            # omega_0(t): class 0 = bins 0..t
    mu += t*p[t]          # mu(t)
    if w <= 0 or w >= 1: continue
    sB2 = (mu_T*w - mu)^2 / (w*(1-w))
    if sB2 > best: best = sB2; t_star = t
threshold = vmin + (t_star + 1) * (vmax - vmin)/L      # upper edge of bin t_star
```
Equivalent to minimising within-class variance; O(L) after the histogram. Use L = 256. Fails on
unimodal histograms: report the separability `eta = sB2(t*)/sigma_T^2` (0..1) and warn when it is below ~0.5.

**Fraction of an image/region meeting a condition:**
```
frac = count{ p in region : valid(p) AND cond(index(p)) } / count{ p in region : valid(p) }
```
Region = pixel rectangle, UV rectangle, or polygon (rasterise with point-in-polygon on pixel
centres, sec. 7). For georeferenced equirectangular images, weight pixels by area `cos(lat)` so
that fractions are fractions of ground area. Return the valid-pixel count as a companion output
(e.g. `IMAGE.COUNT`) so users can judge sample size. With a Beta(1,1) prior, the uncertainty on
the fraction for a Monte Carlo draw is `Beta(k+1, n-k+1)`, which feeds naturally into `RAND.*`.

### 3. Weather-radar nowcasting by extrapolation

**3a. From image to physical values.** Radar PNGs use a discrete palette. Invert with nearest
palette colour (squared RGB distance, reject if > tolerance -> NoData, which handles map
backgrounds, borders and labels). The classic NWS/WSR-88D reflectivity scale uses 5-dBZ steps
from 5 to 75 dBZ, with up to 14-15 colours [18]. The approximate RGB values (as in MetPy's
`NWSReflectivity` table, **verify**) are:
```
dBZ:  5 (0,236,236)  10 (1,160,246)  15 (0,0,246)    20 (0,255,0)    25 (0,200,0)   30 (0,144,0)
     35 (255,255,0)  40 (231,192,0)  45 (255,144,0)  50 (255,0,0)    55 (214,0,0)   60 (192,0,0)
     65 (255,0,255)  70 (153,85,201) 75 (255,255,255)
```
dBZ -> rain rate (Z in mm^6 m^-3, R in mm/h) [17]:
```
Z = 10^(dBZ/10) ;  Z = a R^b  =>  R = (Z/a)^(1/b)
Marshall-Palmer (stratiform):   a=200, b=1.6   -> R = (10^(dBZ/10)/200)^0.625
WSR-88D default (convective):   a=300, b=1.4
Rosenfeld tropical:             a=250, b=1.2
MP check values: 20 dBZ->0.65, 30->2.7, 40->11.5, 50->48.6 mm/h
Cap at ~53 dBZ (hail contamination; NWS caps near 104 mm/h with 300R^1.4). Below ~5-10 dBZ -> 0.
```
Work in **dBR = 10 log10(R)** (or dBZ) for motion estimation and noise because it is closer to Gaussian.
Set R < 0.1 mm/h to a fixed "zero value", e.g. dBR = -15, as pysteps does [8].

**3b. Motion vector between two frames** (frames F1 at t, F2 at t+Δt; usually Δt = 5-10 min).

*Global block matching / cross-correlation* (TREC-style when done per block):
```
(dx,dy)* = argmin_{|dx|,|dy|<=S} (1/|Ω|) Σ_{p∈Ω} (F2(p) - F1(p - (dx,dy)))^2       (SSD)
   or argmax Pearson corr(F1 shifted, F2) over the overlap Ω
sub-pixel (per axis, c = cost at -1,0,+1 around the optimum): δ = (c₋ - c₊) / (2(c₋ - 2c₀ + c₊))
```
Cost O(S² N): fine for S ≤ 20 px on a 256² downsample. Use only pixels where either frame has rain;
otherwise the dry background dominates and pulls the estimate toward (0,0).

*Phase correlation via FFT* [12] (O(N log N), robust to brightness changes):
```
w(x,y)   = Hann window (reduces edge wrap artefacts)
G1 = FFT(w·F1),  G2 = FFT(w·F2)
Q  = conj(G1) · G2 / (|conj(G1) · G2| + ε)      (element-wise; ε ~ 1e-9·max)
r  = real(IFFT(Q))                              -> peak at displacement d of F1 -> F2
(kx,ky) = argmax r; wrap: if kx > N/2 then kx -= N (same for ky)
sub-pixel: parabola fit on r at k-1,k,k+1 per axis (as above, with sign flipped for a maximum)
```
Peak height (0..1) is a confidence score. Use a C FFT library such as KissFFT or pocketfft (both BSD);
FFTW is GPL. Pad to a size of 2^k·3^m.

*Local optical flow* (what pysteps uses by default) [11][40]: Lucas-Kanade on Shi-Tomasi corners,
followed by interpolation to a dense field. For a window around a feature:
```
Ix,Iy spatial gradients (Sobel/central diff), It = F2 - F1
A = [ΣIx²  ΣIxIy ; ΣIxIy  ΣIy²],  b = -[ΣIxIt ; ΣIyIt],   v = A⁻¹ b
accept if λ_min(A) > τ (Shi-Tomasi criterion); use image pyramid for motions > ~2 px
dense field: IDW or Gaussian RBF interpolation of sparse vectors (pysteps: idwinterp2d / rbfinterp2d)
```
pysteps also offers VET (variational echo tracking), DARTS (spectral) and Proesmans [8]. For a
spreadsheet, a **single global vector (phase correlation) plus an optional coarse 4×4 block field** is enough
and far cheaper. Use 3+ frames and average the vectors, or take the median of pairwise estimates, to cut noise.

**3c. Advection: semi-Lagrangian backward scheme** [8][13]. The forecast at lead time τ = nΔt at pixel x is the
value found by tracing the trajectory back from x:
```
constant global V:   F(x, τ) = F0(x - τ·V)                       (one bilinear sample)
spatially varying V(x), per step with midpoint (Staniforth-Côté) iteration:
   α = Δt·V(x);  repeat 1-3 times: α = Δt·V(x - α/2);  x ← x - α     (repeat n times)
   F(x, τ) = F0(x_n)          (bilinear; outside domain -> NoData / "unknown")
```
Backward (gather) mapping never leaves holes. Forward (scatter) mapping does. pysteps' `semilagrangian`
extrapolator is backward with bilinear interpolation by default [8].

**3d. Adding uncertainty**
- *Velocity perturbation (BPS, Bowler-Pierce-Seed 2006, used in pysteps STEPS)* [9][10]. For each
  member draw ε∥, ε⊥ ~ Laplace(0, scale 1/√2), which has unit variance. They are fixed for the whole member:
  ```
  V' = V + σ∥(t)·ε∥·ê∥ + σ⊥(t)·ε⊥·ê⊥ ,  σ(t) = p1·t^p2 + p3
  defaults p∥ = (10.88, 0.23, -7.68), p⊥ = (5.76, 0.31, -2.72)   (σ in km/h, t = lead time in minutes;
  unit convention per pysteps implementation — verify)   e.g. σ∥(60 min) ≈ 20 km/h
  ```
- *STEPS* (Seed 2003 S-PROG; Bowler, Seed & Pierce 2006) [8][9]:
  1. transform to dBR; 2. estimate V; 3. FFT band-pass cascade into K levels (pysteps default
  `n_cascade_levels=6`), normalise each level; 4. per level fit AR(2) in the Lagrangian frame
  from lag-1/lag-2 autocorrelations γ1, γ2:
  ```
  φ1 = γ1(1 - γ2)/(1 - γ1²) ;  φ2 = (γ2 - γ1²)/(1 - γ1²)          (Yule-Walker)
  σ_ε = sqrt( (1+φ2)·((1-φ2)² - φ1²) / (1-φ2) )                  (keeps unit variance)
  X_j(t+1) = φ1,j X_j(t) + φ2,j X_j(t-1) + σ_ε,j · η_j(t)
  ```
  η_j is spatially correlated noise, band-passed the same way. It is "nonparametric": the observed field's
  FFT amplitude with random phases. 5. recompose, advect with the member's perturbed V, back-transform,
  6. probability-match the CDF to the observed rain distribution and apply the rain/no-rain mask. Defaults: `ar_order=2`,
  `noise_method='nonparametric'`, `vel_pert_method='bps'`, `probmatching_method='cdf'`,
  `mask_method='incremental'` [9]. Small scales decorrelate fastest (γ small), so they are
  progressively replaced by noise. This is the physics of "skill decays at small scales first".
- *Spreadsheet-grade ensemble* (cheap, captures most of the value):
  ```
  per trial:  V' = V + σ_v(τ)·(Laplace or Gaussian ε, rotated into ∥/⊥ axes)
              x_b = x - τ·V'             (plus a random offset ~ N(0, s(τ)²) for position error)
              value = F0(x_b) + σ_I(τ)·Z (in dBR; AR(1)-decayed intensity noise)
  P(rain > thr at x, τ) = mean over trials of 1[value > thr]
  ```
- *Local Lagrangian probability* (Germann & Zawadzki 2004, Part II) [13]. This is a deterministic
  probability without trials: `P = fraction of pixels > thr inside a disk of radius L(τ) centred at x - τ·V`, with L
  growing with lead time (fit L(τ) by verification, e.g. start at ~5 km and add ~5-10 km per 30 min).

**3e. Known skill decay.** Lagrangian-persistence lifetime scales with feature size. Large
stratiform systems stay predictable for hours, while convective cells (~1-10 km) last tens of minutes
[13]. Across 1,533 Dutch events, pysteps decorrelation times were ~25 min for 1-h accumulations and
~40 min for 3 h [14]. The extrapolation-vs-NWP crossover is typically **~2-3 h** (HRRR with radar DA: just
over 2 h) [15][16]. Practical guidance: extrapolation is trustworthy at 0-60 min, degrades over 1-2 h,
and should not be used alone beyond ~2-3 h. Always inflate spread with lead time and, for
point forecasts, report probabilities rather than single values beyond ~30 min.

### 4. Image change over time
```
Pixel differencing:   D = I2 - I1   (after co-registration: phase-correlation shift, sec. 3b)
Radiometric normalise first: I1' = (I1 - μ1)·σ2/σ1 + μ2  (or histogram matching)
Change mask:          |D - μ_D| > k·σ_D  (k = 2..3)  or Otsu on |D|
Ratio / log-ratio:    L = ln((I2+ε)/(I1+ε))  (multiplicative changes, illumination)
Multichannel (CVA):   |ΔC| = sqrt(ΔR² + ΔG² + ΔB²)  or on (ExG, V, ...) vectors
Scalar outputs:       changed fraction; mean Δindex over region; centroid shift of a mask
```
**Trend of an index over a series** y_i at times t_i (one value per image, e.g. mean ExG of a field) [39]:
```
OLS slope     β = Σ(t_i - t̄)(y_i - ȳ) / Σ(t_i - t̄)² ; SE = sqrt( Σe_i²/(n-2) / Σ(t_i - t̄)² )
Theil-Sen     β_TS = median_{i<j} (y_j - y_i)/(t_j - t_i)      (robust to cloudy/outlier frames)
Mann-Kendall  S = Σ_{i<j} sign(y_j - y_i);  Var(S) = n(n-1)(2n+5)/18  (no ties)
              Z = (S-1)/√Var if S>0 ; 0 if S=0 ; (S+1)/√Var if S<0 ;  p = 2(1 - Φ(|Z|))
Forecast:     y(t*) ~ N(ŷ, SE_pred²),  SE_pred² = s²(1 + 1/n + (t*-t̄)²/Σ(t_i-t̄)²)  -> Monte Carlo draw
```
Mask cloudy frames (sec. 2 cloud test) before computing the index. Seasonal series (vegetation)
need a harmonic term `a + βt + c·cos(2πt/365.25) + d·sin(2πt/365.25)`.

---
## MAPS / SPATIAL

### 5. Great-circle formulas and projections [19][20]
φ = latitude, λ = longitude (radians), δ = d/R angular distance, θ = bearing (clockwise from north).
```
Haversine:   a = sin²(Δφ/2) + cos φ1 cos φ2 sin²(Δλ/2)
             c = 2·atan2(√a, √(1-a))          d = R·c
Bearing:     θ = atan2( sin Δλ · cos φ2 , cos φ1 · sin φ2 - sin φ1 · cos φ2 · cos Δλ )
             compass = fmod(deg(θ) + 360, 360)            (final bearing = initial bearing of reverse + 180)
Destination: φ2 = asin( sin φ1 cos δ + cos φ1 sin δ cos θ )
             λ2 = λ1 + atan2( sin θ sin δ cos φ1 , cos δ - sin φ1 sin φ2 )
             normalise lon: fmod(deg(λ2) + 540, 360) - 180
Midpoint:    Bx = cos φ2 cos Δλ ;  By = cos φ2 sin Δλ
             φm = atan2( sin φ1 + sin φ2 , √((cos φ1 + Bx)² + By²) ) ;  λm = λ1 + atan2(By, cos φ1 + Bx)
Intermediate point at fraction f:  A = sin((1-f)δ)/sin δ ; B = sin(fδ)/sin δ
             x = A cos φ1 cos λ1 + B cos φ2 cos λ2 ; y = A cos φ1 sin λ1 + B cos φ2 sin λ2
             z = A sin φ1 + B sin φ2 ;  φi = atan2(z, √(x²+y²)) ; λi = atan2(y, x)
Cross-track distance of P from great circle 1→2:  d_xt = asin( sin δ13 · sin(θ13 - θ12) )·R
Along-track: d_at = acos( cos δ13 / cos(d_xt/R) )·R    (clamp → segment distance for track tests)
Equirectangular approx (short distances, fast):  x = Δλ·cos((φ1+φ2)/2), y = Δφ, d = R·√(x²+y²)
Grid-cell area on sphere:  A = R²·(λ2 - λ1)·(sin φ2 - sin φ1)
```
The sphere is off from the WGS84 ellipsoid by up to ~0.3% in distance [19]. That is acceptable for predictions, while
Karney/Vincenty is needed only for survey-grade work. Clamp `a` to [0,1] and the asin/acos arguments to [-1,1]
to avoid NaN from rounding.

**Web Mercator (EPSG:3857)** [21] with R_a = 6378137 m (WGS84 semi-major axis, *not* 6371 km):
```
x = R_a·λ ;  y = R_a·ln tan(π/4 + φ/2) = R_a·asinh(tan φ)
inverse: λ = x/R_a ;  φ = atan(sinh(y/R_a)) = 2·atan(exp(y/R_a)) - π/2
valid |φ| ≤ 85.05112878° (= atan(sinh π)), which makes the world square
Tiles/pixels at zoom z, tile size T = 256:  n = 2^z
  xtile = n·(lon+180)/360 ;  ytile = n·(1 - asinh(tan φ)/π)/2      (floor → tile index, frac·T → pixel)
  lon = xtile/n·360 - 180 ;  φ = atan(sinh(π·(1 - 2·ytile/n)))
Local scale factor = sec φ (lengths), sec² φ (areas) → never average Mercator pixels unweighted.
```
**Equirectangular / plate carrée** (EPSG:4326 rasters, simplest heatmap grid): `x = (λ-λ0)·cos φ1`,
`y = φ - φ0`. With φ1 = 0 it is plain lon/lat → pixel. Recommended internal grid for the heatmap
view: a regular lon/lat grid, with cells area-weighted by `cos φ` (or the exact cell area formula above).
Display it on a Web Mercator basemap by resampling rows.

### 6. Spatial interpolation (scattered stations → value at (lat,lon))
Distances are great-circle (haversine), or equirectangular for small regions (kriging needs a valid metric).

**Nearest neighbour** (Thiessen/Voronoi): `ẑ(x0) = z_k, k = argmin_i d(x0, x_i)`.

**Inverse distance weighting** (Shepard 1968) [22]:
```
ẑ(x0) = Σ w_i z_i / Σ w_i ,  w_i = 1/d(x0,x_i)^p ;  if d(x0,x_i) = 0 → return z_i
p = 2 default; larger p → more local (p→∞ = nearest neighbour); p ≤ dim (2) gives far points too
much weight when data are dense. Restrict to k nearest (e.g. 8-12) or radius R_s.
Modified Shepard: w_i = ( max(0, R_s - d_i) / (R_s·d_i) )²
```
IDW is exact and bounded by min/max(z), gives no uncertainty, and produces "bull's-eyes".

**Ordinary kriging** [23]. Empirical semivariogram with lag bins h_k:
```
γ̂(h_k) = 1/(2N(h_k)) Σ_{(i,j): d_ij ∈ bin k} (z_i - z_j)²      (use lags up to ~½ max distance)
Models (nugget c0, partial sill c, range a; γ(0) = 0 exactly):
 spherical:    γ(h) = c0 + c·(1.5·h/a - 0.5·(h/a)³)   for 0 < h ≤ a ;  c0 + c for h > a
 exponential:  γ(h) = c0 + c·(1 - exp(-3h/a))          (a = practical range, 95% of sill)
 Gaussian:     γ(h) = c0 + c·(1 - exp(-3h²/a²))        (very smooth; add nugget for stability)
Fit (c0,c,a) by weighted least squares, weights N(h_k)/γ(h_k)² (Cressie), or grid search.
```
Kriging system for n neighbours (λ weights, μ Lagrange multiplier; solve (n+1)×(n+1) by LU):
```
| γ11 … γ1n 1 | |λ1|   |γ10|
|  ⋮      ⋮  ⋮ | |⋮ | = | ⋮ |      γij = γ(d(x_i,x_j)), γi0 = γ(d(x_i,x0))
| γn1 … γnn 1 | |λn|   |γn0|
|  1  …  1  0 | |μ |   | 1 |
ẑ(x0) = Σ λ_i z_i            σ²_OK(x0) = Σ λ_i γi0 + μ
```
Use ≤ 16-32 nearest neighbours per prediction to keep it O(n³) small. Cache the LU of the neighbour
matrix when the neighbour set repeats. Kriging weights can be negative, so ẑ may leave the data range.

**Gaussian-process view** [24]. With covariance C(h) = (c0 + c) - γ(h) for bounded variograms, the nugget acts as
noise variance σn². Simple kriging is exactly the GP posterior with a known mean m:
```
μ*(x0) = m + k*ᵀ (K + σn² I)⁻¹ (y - m) ;  σ*²(x0) = k(x0,x0) - k*ᵀ (K + σn² I)⁻¹ k*
```
Ordinary kriging is the GP with an unknown constant mean under a flat prior. Hyperparameters can be fitted by
maximising the log marginal likelihood `-½ yᵀK⁻¹y - ½ log|K| - n/2 log 2π` instead of variogram fitting.
For Monte Carlo, `RAND.KRIGE` draws `N(ẑ, σ²_OK)`. Independent draws per cell ignore spatial
correlation between cells, which is fine for a single-location question. Joint fields need a Cholesky
factor of the posterior covariance.

**Cross-validation (choose p, model, range)**. Leave-one-out: for each i predict ẑ₋ᵢ(x_i) from the other points.
```
ME = mean(ẑ - z) (bias) ; RMSE = sqrt(mean((ẑ - z)²)) ; MAE
kriging: standardised errors e_i/σ_i should have mean ≈ 0 and RMS ≈ 1 (calibration of σ)
Pick the IDW p ∈ {1, 1.5, 2, 3, 4} with minimum LOO-RMSE. Use k-fold/spatial blocks when points are clustered.
```

### 7. Polygons: point-in-polygon, area, centroid, GeoJSON

**Ray casting (even-odd; W.R. Franklin's PNPOLY)** [25] in lon/lat space:
```c
int pnpoly(int n, const double *x, const double *y, double px, double py) {
    int c = 0;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if (((y[i] > py) != (y[j] > py)) &&
            (px < (x[j] - x[i]) * (py - y[i]) / (y[j] - y[i]) + x[i]))
            c = !c;
    return c;
}
```
**Winding number (Sunday 2001)** [25][42]. It also gives the correct answer for self-intersecting rings:
```
isLeft(a,b,p) = (b.x-a.x)(p.y-a.y) - (p.x-a.x)(b.y-a.y)
wn = 0; for each edge a→b:
   if a.y <= p.y: if b.y >  p.y and isLeft(a,b,p) > 0: wn++       (upward crossing, p left)
   else:          if b.y <= p.y and isLeft(a,b,p) < 0: wn--       (downward crossing, p right)
inside ⇔ wn ≠ 0
```
Holes and multipolygons:
`inside(Polygon) = inside(ring0) AND NOT any(inside(hole_k))`, and `inside(MultiPolygon) = OR over polygons`.
Running even-odd over *all* rings of a Polygon at once gives the same result without special-casing holes.
Speed: test the bbox first, and keep a uniform grid index (e.g. 1°×1° buckets → candidate features) or an
R-tree for layers with thousands of polygons. Boundary points are ambiguous, so pick a convention and document it.
RFC 7946 defines edges as straight lines in lon/lat [26], so a planar test in lon/lat matches the spec.
Features crossing the antimeridian SHOULD be split, so don't unwrap longitudes.

**Area on the sphere.** turf.js `ringArea` (Chamberlain & Duquette 2007, JPL Pub 07-03) [27]:
```
A_ring = (R²/2) · Σ_i (λ_{i+1} - λ_{i-1}) · sin φ_i    (indices cyclic over the n distinct vertices)
area(Polygon) = |A_ring0| - Σ |A_hole_k| ;  turf uses R = 6378137 m (R = 6371008.8 m changes it by ~0.2%)
```
d3-geo `geoArea` [28] sums exact spherical excess per edge using the south-pole triangle (Todhunter/Cagnoli):
```
φ' = φ/2 + π/4 for each vertex;  for edge (λ0,φ0')→(λ,φ'):  dλ = λ - λ0, s = sign(dλ), adλ = |dλ|
k = sin φ0' · sin φ' ;  u = cos φ0' · cos φ' + k·cos(adλ) ;  v = k · s · sin(adλ)
ringSum += atan2(v, u);   polygon steradians = 2·Σ (ring < 0 ? ring + 2π : ring) ;  area = sr·R²
```
d3 assumes clockwise exterior rings (the *opposite* of RFC 7946's right-hand rule). A wrong winding gives
"whole sphere minus polygon". turf's absolute-value approach is simpler and orientation-agnostic, so it is
**recommended**. Exact alternative for triangles: L'Huilier's theorem for spherical excess.

**Centroid.** Planar (shoelace) centroid in an equal-area-ish local frame is fine for polygons of
country size or smaller:
```
A = ½ Σ (x_i y_{i+1} - x_{i+1} y_i);  Cx = (1/6A) Σ (x_i + x_{i+1})(x_i y_{i+1} - x_{i+1} y_i);  Cy likewise
multi-ring: area-weighted sum (holes have negative A). Use x = λ·cos φ̄, y = φ, then convert back.
```
turf `centroid` = mean of vertices, turf `centerOfMass` = shoelace, d3 `geoCentroid` = spherical. For a
label/"representative point" guaranteed inside, use polylabel or a point-in-polygon check with a fallback.

**GeoJSON essentials (RFC 7946)** [26]:
- Geometry types: Point, MultiPoint, LineString, MultiLineString, Polygon, MultiPolygon,
  GeometryCollection. `Feature {type, geometry (or null), properties (object or null), id? (string|number)}`,
  `FeatureCollection {type, features: [...]}`.
- Position = `[longitude, latitude(, altitude)]`, WGS84 only (the old `crs` member was removed).
- Polygon = array of linear rings. Ring 0 is the exterior and the rest are holes. Each ring is closed (first == last)
  with ≥ 4 positions. Right-hand rule: exterior counter-clockwise, holes clockwise, but parsers must not
  reject the wrong winding (legacy data). MultiPolygon = array of Polygon coordinate arrays.
- `bbox: [west, south, east, north]`. When crossing the antimeridian, west > east.
- ~6 decimal places (~10 cm) is ample precision. Media type `application/geo+json`.
- Parsing in C: json-glib (GLib-native), cJSON or yyjson. Store rings as flat double arrays plus a bbox per
  ring. Map `MAP.REGION(... , property)` to `properties[property]` (numbers → number, else string).

### 8. Spatial spread models on grids

**Stochastic CA for wildfire (Alexandridis et al. 2008, Spetses 1990)** [29]. States: 1 = no fuel, 2 = fuel
not burning, 3 = burning, 4 = burned out. Moore (8-cell) neighbourhood.
```
Rules per step: state 3 → 4; state 1,4 unchanged;
  for each burning cell s and each neighbour n in state 2: n ignites (→3 next step) with prob p_burn
p_burn = p_h · (1 + p_veg) · (1 + p_den) · p_w · p_s          (clamp to [0,1])
p_h = 0.58   (burn prob. of a neighbour in no-wind, flat, "normal" fuel)
p_w = exp(c1·V) · exp(V·c2·(cos θ - 1)) ;  c1 = 0.045, c2 = 0.131 (V = wind speed, m/s)
      θ = angle between wind direction (direction the wind blows TOWARD) and spread direction s→n
p_s = exp(a·θ_s) ;  a = 0.078 ;  θ_s = atan((E_n - E_s)/L) in degrees  (L = cell size; √2·L diagonal)
      (uphill spread θ_s > 0 → faster.) The paper writes atan((E1-E2)/l); orient the sign so uphill is positive.
p_veg: agricultural -0.3, thickets 0, hallepo-pine 0.4 ;  p_den: sparse -0.4, normal 0, dense 0.3   (verify)
```
Sanity check: V = 10 m/s gives p_w = 1.57 downwind and 0.11 upwind. A 10° upslope doubles p_s (×2.18). The paper also
has a spotting (ember) term, which is optional. The time step is calibrated so that simulated spread matches the observed
rate. Map vegetation/density from a land-cover raster (e.g. ESA WorldCover classes → p_veg, p_den).

*Percolation view* [30]. With one ignition attempt per neighbour pair, the model is bond percolation:
an unbounded burn is possible only if p > p_c (square 4-neighbour bond p_c = 0.5, site p_c ≈ 0.5927;
8-neighbour site p_c ≈ 0.4073). This explains the sharp "fire goes out vs. burns everything"
transition that users will see in Monte Carlo histograms.

Implementation: two byte grids (cur/next) or a frontier queue of burning cells. Draw one uniform per
(burning cell, neighbour) from the cell's stream in a fixed scan order, so the run is reproducible. Per
Monte Carlo trial, return a scalar: burned area (cells × cell area), 1[target cell burned], or the step at which the
target ignites (time of arrival; ∞ if never). The mean over trials gives the burn probability map for the heatmap.

**Spatial SIR on a grid** [41]. Metapopulation form, cell c with N_c, S_c, I_c, R_c:
```
λ_c = β · [ (1-κ)·I_c/N_c + κ · Σ_{n ∈ nbr(c)} w_cn · I_n/N_n ]      (κ = coupling, Σ_n w_cn = 1)
new_inf ~ Binomial(S_c, 1 - exp(-λ_c Δt)) ;  new_rec ~ Binomial(I_c, 1 - exp(-γ Δt))
S -= new_inf ; I += new_inf - new_rec ; R += new_rec          R0 ≈ β/γ ; mean infectious period 1/γ
```
Individual-based form (one host per cell): a susceptible cell with k infected neighbours becomes
infected with p = 1 - (1 - τ)^k. An infected cell recovers with prob γ per step, or after a fixed D steps.
Outputs are the attack rate, peak time, or 1[cell ever infected]. Gravity-model coupling
`w_cn ∝ N_c N_n / d_cn²` handles non-adjacent cities (a map of points rather than a grid).

**Flood fill by elevation ("bathtub" with connectivity)** [31]:
```
flooded(c) ⇔ z(c) < h AND c is connected (4- or 8-neighbour) through cells with z < h to a seed
           (seed = ocean/river cells or a user point). Unconnected low basins stay dry.
BFS: push seeds; pop c; for each nbr n: if !seen[n] && z[n] < h → seen[n]=1, push n.  O(cells)
Depth(c) = h - z(c) for flooded cells.
```
Probabilistic version: per trial h = tide + surge + SLR drawn from distributions, plus DEM error
`z' = z + N(0, σ_z²)` (σ_z = DEM vertical RMSE from metadata; spatially correlated error is more
realistic than independent errors). P(flooded at x) = fraction of trials. Without connectivity, the closed form is
`P = Φ((h - z)/σ_z)`. Bathtub models overestimate flooding from short surges because they ignore flow
dynamics and friction [31]. Say so in the UI.

### 9. Hurricane tracks and the NHC cone

**Cone construction** [32]. Circles are centred on the official forecast positions at 12, 24, 36, 48, 60,
72, 96 and 120 h. Each radius is the distance that encloses **two-thirds of official track errors over the previous
5 years**. The cone is the area swept by the circles along the track, interpolating radii between forecast points. It
describes only the probable path of the **centre**: the centre falls outside it roughly one time in three, and
it says nothing about size or impacts. 2026 Atlantic radii (from 2021-2025 errors), nmi [32]:
```
t (h):  12   24   36   48   60   72   96   120
r (nmi): 25   39   49   62   77*  95   134  200        (*60 h value from a secondary source — verify)
r (km):  46   72   91  115  143  176  248  370
```
Radii shrink a few percent most years, e.g. about 3-5% smaller in 2025 [32]. Eastern/Central Pacific radii differ: read the
table on the NHC page. Five-year mean official Atlantic track errors (2021-2025): 24 h 33.6, 48 h
55.9, 72 h 86.4, 96 h 124.9, 120 h 181.0 nmi [33]. 2024 was a record year at 19 nmi (12 h) to 115 nmi (120 h).

**Cone radius → Gaussian error scale.** For an isotropic bivariate normal position error,
`P(|e| ≤ r) = 1 - exp(-r²/(2σ²))`. Setting this equal to 2/3 gives
```
σ(t) = r(t) / sqrt(2·ln 3) = r(t) / 1.4823      (per-axis σ)
σ (km): 12h 31.2, 24h 48.7, 36h 61.2, 48h 77.5, 60h 96.2, 72h 118.7, 96h 167.4, 120h 249.9
check: mean radial error = σ·√(π/2) → 120 h: 169 nmi vs observed 5-yr mean 181 nmi (tails are heavier)
```
**Monte Carlo track model** (spreadsheet-grade). Inputs: official forecast points (t_k, lat_k, lon_k)
and σ(t) from the table, interpolated linearly in t.
```
Correlated error in lead time (keeps marginals exact):  z_0 ~ N(0, I2)
   z_k = ρ·z_{k-1} + sqrt(1-ρ²)·ξ_k ,  ξ_k ~ N(0, I2) ,  ρ ≈ 0.8-0.95 per 12 h (tune)
   e_k = σ(t_k)·z_k  (km; optionally scale the along-track axis up and the cross-track axis down)
   member position_k = destination(official_k, |e_k|, atan2(e_east, e_north))
Alternative "physics-lite": s_{k+1} = s_k·exp(σ_s ε), heading_{k+1} = heading_k + Δθ_k + σ_h ε,
   advance with destination(); calibrate σ_s, σ_h so that ~2/3 of members fall inside the cone radii.
Heavier tails: use Student-t (ν ≈ 4-6) instead of the normal for ξ.
```
Synthetic-climatology models (Vickery et al. 2000) regress changes in ln(speed) and heading on the
current state for long-range risk. NHC's own wind-speed probabilities use **1000 Monte Carlo
realizations** sampled from the last 5 years of official track/intensity errors (with serial correlation and
bias) plus a wind-radii model. Probability at a point = fraction of realizations whose wind radii
cover it [34].

**P(location or region hit)**:
```
densify each member track to ≤ 1 h steps (intermediate-point formula, linear in time)
hit_n = 1[ min_k d(x, segment_k) ≤ r_hit ]      (segment distance via cross/along-track, clamped)
P(hit) = (1/N)·Σ hit_n ;  95% CI ≈ P ± 1.96·sqrt(P(1-P)/N)   (N = 1000 → ±3 pp at P = 0.5)
region: hit if any densified point is inside the polygon, or its distance to the boundary ≤ r_hit
r_hit: 0 (centre passes over) or the 34-kt wind radius (typically ~50-150 nmi, storm-specific; from advisory)
time window: restrict k to t_k ≤ T for "hit within T hours"; also return the arrival time.
```

### 10. Useful open data and simple import formats
- **Natural Earth** [36]: vector (admin-0 countries, admin-1 states/provinces, populated places,
  coastlines, rivers, lakes) and raster at 1:10m / 1:50m / 1:110m. **Public domain**, no permission
  needed and attribution optional. GeoJSON builds are in the `nvkelso/natural-earth-vector` GitHub repo, and
  1:110m admin-0 is small enough to ship with the app as the default `MAP.REGION` layer.
- **OpenStreetMap tiles** [37]: the OSMF tile usage policy **prohibits bulk downloading/scraping and
  "download area for offline" prefetch**. Requirements: a valid identifying User-Agent, visible attribution
  "© OpenStreetMap contributors", honouring cache headers (≥ 7 days if you can't read them), and no no-cache
  headers. Heavy use needs your own tile server or a commercial provider. OSM *data* is ODbL (share-alike
  on derived databases). For bulk data use extracts (e.g. Geofabrik), not tiles. For Time Machine: fetch
  basemap tiles on demand only while the map view is visible, cache on disk, show attribution, and let users
  configure the tile URL.
- Other open sources: Copernicus DEM GLO-30 / SRTM (elevation for bathtub/slope), ESA WorldCover
  10 m (land cover, CC BY 4.0) [46], NHC HURDAT2 and IBTrACS (historical tracks for calibration) [47],
  national radar composites (e.g. NOAA/NWS, US public domain). Check each licence before bundling.
- **Import formats** (easiest first):
  1. CSV with `lat,lon[,value,...]` columns (points; also WKT `POINT(lon lat)`); decimal degrees, WGS84.
  2. GeoJSON (RFC 7946) FeatureCollection → polygon/point layer with properties.
  3. PNG/JPEG + bounds: either explicit `W,S,E,N` plus projection flag (`equirect` | `webmercator`),
     like Leaflet ImageOverlay or KML GroundOverlay LatLonBox, or an ESRI world file sidecar [38].
  4. ESRI ASCII grid `.asc` (header `ncols nrows xllcorner yllcorner cellsize NODATA_value`, then rows
     north→south) [44]. This is the simplest DEM/raster format, and trivially parsed in C.
  5. GeoTIFF only via optional libtiff/libgeotiff or GDAL (heavy dependency; defer).
  Decode images with gdk-pixbuf/GdkTexture into a float cache keyed by name and file mtime.

---
## Implementation notes for Time Machine
- **Resource registry**: `IMAGE.LOAD`/sheet-level "Data sources" panel registers `name → {path, bounds,
  projection, palette}`. Functions reference names, so recalculation doesn't re-decode. Cache derived
  products (index rasters, summed-area tables, FFTs, motion vectors, polygon indices) keyed by
  (name, mtime, params).
- **Determinism**: RAND.* functions consume only the cell's stream in a fixed order (scan order for CA,
  member index for tracks). Heavy simulations (CA, SIR) should run once per trial per cell, and several cells
  that reference the same simulation should share it through a per-trial memo `(sim-id, trial) → result grid`.
- **Heatmap view**: evaluate a formula over a lon/lat grid (e.g. 200×100) by binding `lat`/`lon` names.
  Show P(·) with a sequential colormap (0..1 fixed scale) and deterministic fields with an auto range. Area-weight
  summary statistics by cos φ. Draw on an equirectangular canvas, or reproject rows to Web Mercator over tiles.
- **Units**: accept `"km"|"mi"|"nmi"|"m"` in distance functions. Bearings are degrees clockwise from north;
  wind direction must say "from" (meteorological) or "toward", and the CA formula uses *toward*.

---
## Proposed spreadsheet functions (prioritised)

| # | Signature | Returns | Method (sec.) | Pri |
|---|-----------|---------|---------------|-----|
| 1 | `GEO.DISTANCE(lat1, lon1, lat2, lon2, [unit="km"])` | great-circle distance | haversine (5) | P1 |
| 2 | `GEO.BEARING(lat1, lon1, lat2, lon2)` | initial bearing 0-360° | atan2 formula (5) | P1 |
| 3 | `GEO.DESTINATION(lat, lon, dist, bearing, part, [unit="km"])` | `part`="lat"\|"lon" of destination | (5) | P1 |
| 4 | `MAP.REGION(layer, lat, lon, [property="name"], [default])` | property of containing feature, else `default`/#N/A | bbox + PNPOLY, holes, Multi (7) | P1 |
| 5 | `MAP.CONTAINS(layer, feature, lat, lon)` | TRUE/FALSE (feature = id or property match) | (7) | P1 |
| 6 | `IMAGE.AT(name, x, y, [channel="luma"], [coords="px"])` | bilinear sample; channel r,g,b,a,luma601,luma709,h,s,v,exg,vari,gli,ngrdi,exgr,dbz; coords "px"\|"uv"\|"geo" (x=lon,y=lat) | (1)(2) | P1 |
| 7 | `IMAGE.MEAN(name, channel, [x0, y0, x1, y1], [coords])` | mean of channel/index over rectangle (valid pixels) | summed-area table (1)(2) | P1 |
| 8 | `IMAGE.FRACTION(name, channel, op, threshold, [x0,y0,x1,y1], [coords])` | fraction of pixels where `channel op thr`; thr may be `"otsu"` | (2) | P1 |
| 9 | `GEO.IDW(lats, lons, values, lat, lon, [power=2], [k=12])` | interpolated value | IDW (6) | P1 |
| 10 | `RADAR.RAINRATE(dbz, [a=200], [b=1.6])` | mm/h (0 below 5 dBZ, capped at 53 dBZ) | Z-R (3a) | P1 |
| 11 | `RAND.TRACKHIT(track_range, lat, lon, [radius_km=0], [hours=120], [rho=0.9])` | 1/0 this trial (mean = P hit); track_range = cols (hour, lat, lon) | cone σ(t)=r/1.4823, AR(1) errors (9) | P1 |
| 12 | `MAP.AREA(layer, feature, [unit="km2"])` | spherical area | turf ringArea (7) | P2 |
| 13 | `MAP.CENTROID(layer, feature, part)` | "lat"\|"lon" of area centroid | shoelace, local frame (7) | P2 |
| 14 | `MAP.DISTANCE(layer, feature, lat, lon, [unit])` | distance to polygon (0 if inside) | PNPOLY + min segment cross-track (5)(7) | P2 |
| 15 | `IMAGE.OTSU(name, channel, [x0,y0,x1,y1])` | Otsu threshold (value) | (2) | P2 |
| 16 | `RADAR.MOTION(frame1, frame2, part, [method="phase"])` | part = "u"\|"v" (px/frame), "speed_kmh", "dir" | phase correlation / block match (3b) | P2 |
| 17 | `RADAR.NOWCAST(frame1, frame2, lat, lon, lead_min, [out="dbz"])` | extrapolated dBZ or mm/h at point | global V + backward advection (3c) | P2 |
| 18 | `RADAR.PROB(frame1, frame2, lat, lon, lead_min, thr_dbz, [radius_km])` | P(> thr), no RNG | local Lagrangian neighbourhood (3d) | P2 |
| 19 | `RAND.NOWCAST(frame1, frame2, lat, lon, lead_min)` | one ensemble realisation (dBZ) | BPS-style velocity perturbation + noise (3d) | P2 |
| 20 | `GEO.KRIGE(lats, lons, values, lat, lon, [model="spherical"], [range], [sill], [nugget], [out="value"\|"sd"])` | OK estimate or kriging SD; auto-fit if params omitted | (6) | P2 |
| 21 | `RAND.KRIGE(lats, lons, values, lat, lon, [model], ...)` | draw from N(ẑ, σ²_OK) | (6) | P2 |
| 22 | `RAND.SPREAD(fuel_grid, ign_lat, ign_lon, tgt_lat, tgt_lon, steps, [wind_ms=0], [wind_to_deg=0], [dem], [out="hit"])` | this trial: 1/0 target burned, or "area_km2", or "arrival" (steps) | Alexandridis CA (8) | P2 |
| 23 | `MAP.FLOOD(dem, seed_lat, seed_lon, level_m, lat, lon, [out="flag"])` | 1/0 flooded or depth (m) | connected bathtub BFS (8); pass RAND level for MC | P2 |
| 24 | `IMAGE.TREND(names_range, times_range, channel, [x0,y0,x1,y1], [out="slope"])` | slope, "sen", "p" (Mann-Kendall), "forecast" | OLS/Theil-Sen/MK (4) | P3 |
| 25 | `IMAGE.CHANGE(name1, name2, channel, [k=2], [region])` | fraction of pixels changed | normalised difference, k·σ or "otsu" (4) | P3 |
| 26 | `RAND.SIR(grid, seed_lat, seed_lon, lat, lon, beta, gamma, steps, [kappa=0.2], [out="ever"])` | this trial: 1/0 ever infected, or attack rate | stochastic metapopulation SIR (8) | P3 |
| 27 | `GEO.PROJECT(lat, lon, part, [proj="webmercator"], [zoom])` | "x"\|"y" in metres or tile pixels | (5) | P3 |

Priority rationale: P1 = pure maths and the cheapest image reads, which unlock most user stories (distance to X,
which region, how green, P(hurricane hits)). P2 = needs caching/registries (radar, kriging, CA).
P3 = time series and epidemics. Every `RAND.*` function also has a deterministic twin, or can be averaged by
the Monte Carlo engine to give a probability.

---
## Sources
1. Rec. 601 luma coefficients — https://en.wikipedia.org/wiki/Rec._601 ; Luma — https://en.wikipedia.org/wiki/Luma_(video)
2. Rec. 709 — https://en.wikipedia.org/wiki/Rec._709
3. HSL and HSV conversion — https://en.wikipedia.org/wiki/HSL_and_HSV
4. Bilinear interpolation — https://en.wikipedia.org/wiki/Bilinear_interpolation ; Summed-area table — https://en.wikipedia.org/wiki/Summed-area_table
5. Meyer & Neto (2008), Verification of color vegetation indices for automated crop imaging applications, Comput. Electron. Agric. 63:282-293 — https://www.researchgate.net/publication/222248807_Verification_of_color_vegetation_indices_for_automated_crop_imaging_applications
6. RGB vegetation index tables (VARI, GLI, NGRDI, ExG) — https://www.researchgate.net/figure/Common-color-vegetation-indices-based-on-RGB-images_tbl3_327796600 ; https://pmc.ncbi.nlm.nih.gov/articles/PMC9355326/ ; https://github.com/AgPipeline/transformer-rgb-indices
7. Otsu's method (Otsu 1979, IEEE Trans. SMC 9:62-66) — https://en.wikipedia.org/wiki/Otsu%27s_method
8. Pulkkinen et al. (2019), Pysteps v1.0, GMD 12:4185 — https://gmd.copernicus.org/articles/12/4185/2019/
9. pysteps `steps.forecast` API (defaults) — https://pysteps.readthedocs.io/en/stable/generated/pysteps.nowcasts.steps.forecast.html
10. pysteps motion perturbations (BPS; Bowler, Pierce & Seed 2006) — https://pysteps.readthedocs.io/en/latest/generated/pysteps.noise.motion.generate_bps.html ; https://pysteps.readthedocs.io/en/stable/pysteps_reference/noise.html
11. pysteps dense Lucas-Kanade — https://pysteps.readthedocs.io/en/stable/generated/pysteps.motion.lucaskanade.dense_lucaskanade.html ; https://pysteps.readthedocs.io/en/stable/pysteps_reference/motion.html
12. Phase correlation — https://en.wikipedia.org/wiki/Phase_correlation ; Foroosh et al., subpixel extension — https://www.cs.ucf.edu/~foroosh/subreg.pdf
13. Germann & Zawadzki (2002) MWR 130:2859-2873 ; (2004) Part II probability forecasts — https://www.semanticscholar.org/paper/Scale-Dependence-of-the-Predictability-of-from-Part-Germann-Zawadzki/e06aecc6f88fc1a355a414439c3b72f5cafeb7c2
14. Imhoff et al. (2020), Spatial and temporal evaluation of radar rainfall nowcasting techniques on 1,533 events, WRR — https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2019WR026723
15. NWP and radar extrapolation: comparisons and explanation of errors, MWR 148(12) 2020 — https://journals.ametsoc.org/view/journals/mwre/148/12/MWR-D-20-0221.1.xml
16. Prudden et al., A review of radar-based nowcasting of precipitation — https://arxiv.org/pdf/2005.04988
17. NWS Tallahassee, Reflectivity-rainfall rate relationships — https://www.weather.gov/tae/research-zrpaper ; OU Z-R notes — https://www.ou.edu/radar/z_r_relationships.pdf ; WSR-88D rainfall estimation — https://www.weather.gov/mrx/radarrainfallestimates
18. Weather Underground, Understanding radar (5-dBZ colour steps) — https://www.wunderground.com/prepare/understanding-radar ; MetPy colortables — https://unidata.github.io/MetPy/latest/api/generated/metpy.plots.ctables.html
19. Movable Type Scripts, Calculate distance, bearing and more between lat/lon points — https://www.movable-type.co.uk/scripts/latlong.html
20. Earth radius (IUGG mean radius R1 = 6371.0088 km) — https://en.wikipedia.org/wiki/Earth_radius
21. OSM wiki, Slippy map tilenames — https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames ; Web Mercator — https://en.wikipedia.org/wiki/Web_Mercator_projection
22. Inverse distance weighting — https://en.wikipedia.org/wiki/Inverse_distance_weighting
23. Kriging — https://en.wikipedia.org/wiki/Kriging ; Variogram — https://en.wikipedia.org/wiki/Variogram
24. Rasmussen & Williams, Gaussian Processes for Machine Learning (2006) — http://gaussianprocess.org/gpml/
25. Point in polygon — https://en.wikipedia.org/wiki/Point_in_polygon ; W.R. Franklin PNPOLY — https://wrfranklin.org/Research/Short_Notes/pnpoly.html
26. RFC 7946, The GeoJSON Format — https://datatracker.ietf.org/doc/html/rfc7946
27. turf-area source (Chamberlain & Duquette 2007, JPL Pub 07-03) — https://github.com/Turfjs/turf/blob/master/packages/turf-area/index.ts ; https://turfjs.org/docs/api/area
28. d3-geo area.js — https://github.com/d3/d3-geo/blob/main/src/area.js
29. Alexandridis et al. (2008), A cellular automata model for forest fire spread prediction: Spetses 1990, Appl. Math. Comput. 204:191-201 — https://www.sciencedirect.com/science/article/abs/pii/S0096300308004943 ; follow-up — https://nhess.copernicus.org/articles/19/169/2019/ ; https://arxiv.org/pdf/2403.08817
30. Percolation threshold — https://en.wikipedia.org/wiki/Percolation_threshold
31. Best practices for elevation-based assessments of sea-level rise and coastal flooding exposure (Frontiers 2018) — https://www.frontiersin.org/journals/earth-science/articles/10.3389/feart.2018.00230/full ; CoastAdapt bathtub explainer — https://coastadapt.com.au/resource-centre/explainers-templates-and-how-to-pages/explainer-bathtub-inundation-modelling/
32. NHC, Definition of the track forecast cone — https://www.nhc.noaa.gov/aboutcone.shtml ; NHC 2025 product updates — https://www.nhc.noaa.gov/pdf/NHC_New_Products_Updates_2025.pdf
33. NHC official 5-year average errors (2021-2025) — https://www.nhc.noaa.gov/verification/pdfs/OFCL_5-yr_averages.pdf ; 2024 verification report — https://www.nhc.noaa.gov/verification/pdfs/Verification_2024.pdf
34. DeMaria et al. (2009), A new method for estimating tropical cyclone wind speed probabilities, WAF 24:1573 — https://www.nhc.noaa.gov/pdf/2009waf_wsp.pdf
35. Vickery, Skerlj & Twisdale (2000), Simulation of hurricane risk in the U.S. using empirical track model — https://ascelibrary.org/doi/10.1061/(ASCE)0733-9445(2000)126:10(1222)
36. Natural Earth terms of use — https://www.naturalearthdata.com/about/terms-of-use/
37. OSMF Tile Usage Policy — https://operations.osmfoundation.org/policies/tiles/
38. World file — https://en.wikipedia.org/wiki/World_file ; GDAL WLD driver — https://gdal.org/en/stable/drivers/raster/wld.html
39. Theil-Sen estimator — https://en.wikipedia.org/wiki/Theil%E2%80%93Sen_estimator ; Mann-Kendall test — https://en.wikipedia.org/wiki/Mann%E2%80%93Kendall_test
40. Lucas-Kanade method — https://en.wikipedia.org/wiki/Lucas%E2%80%93Kanade_method
41. Compartmental models in epidemiology — https://en.wikipedia.org/wiki/Compartmental_models_in_epidemiology
42. Sunday winding-number implementation notes — https://github.com/hayeswise/Leaflet.PointInPolygon
43. sRGB transfer function — https://en.wikipedia.org/wiki/SRGB
44. GDAL Esri ASCII grid driver — https://gdal.org/en/stable/drivers/raster/aaigrid.html
45. Long et al. (2006), Retrieving cloud characteristics from ground-based daytime color all-sky images, JTECH 23:633 — https://doi.org/10.1175/JTECH1875.1
46. ESA WorldCover (CC BY 4.0) — https://esa-worldcover.org/
47. NHC HURDAT2 — https://www.nhc.noaa.gov/data/#hurdat ; IBTrACS — https://www.ncei.noaa.gov/products/international-best-track-archive
