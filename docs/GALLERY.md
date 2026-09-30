# Gallery

What Time Machine looks like, feature by feature. Every picture here is
the real program, rendered headlessly; [how](#how-these-were-made) is at
the end.

## The spreadsheet

A model is an ordinary sheet whose uncertain cells (tinted) hold
distributions. After **F5** the panel on the right shows what the
selected cell's futures look like: a histogram marked at P5, P50 and
P95, the statistics in the cell's own format, and a tornado chart of
what drives it.

![The product-launch example after simulating: inputs drawn from PERT, lognormal, triangular and Bernoulli distributions; the profit cell selected; a histogram of 10,000 simulated profits with the loss-making ones in red; and a tornado chart showing market size and share drive profit most](images/launch.png)

While a formula is typed, clicking or dragging over cells puts their
reference in, marked with a dashed border, as in Excel:

![Typing =AVERAGE( into a cell and dragging over twelve months of sales: the formula bar and the cell read =AVERAGE(B16:B27 and the range is marked with a dashed orange border](images/pointing.png)

**File › Open Example** has sixteen models, one method each, and
**Help › Functions** (F1) lists all 249 functions, with a search box:

![The Open Example menu: weather, football and stock-price models; profit, sales, schedule and savings; learning from past cases, an A/B test, pump lifetimes and a forecasting tournament; and a rain radar, a wildfire, a hurricane track, rain gauges and a crop canopy](images/examples-menu.png)

![The Functions window searched for "regression": seven of 249 functions, all under Learning, from FORECAST.MLR to RAND.MLR, each with its syntax and a line of help](images/functions.png)

## Fans: a quantity over time

Select a row or column and the panel draws the 50% and 90% bands of its
futures, the median, and a dozen single futures, growing out of the
plain values before them.

![A fan chart of retirement savings over 25 years, the median rising from 100k to about 780k inside bands spreading to over 2M](images/retirement.png)

The months of an exponential-smoothing forecast share their surprises,
so the fan is as wide as the model says, and single futures wander as
real ones do:

![Three years of monthly sales as a line, and the next year from Holt-Winters smoothing as a fan, with single futures running high or low for months together](images/sales.png)

A column of measurements continued by a forecast: green cover read off
six weekly photographs, then a logistic curve fitted to it, the line's
own uncertainty shared across the weeks:

![The crop example: cover measured at 4% to 50% over six weeks as a line, then weeks 7 to 10 as a fan rising towards 94%, the chance the canopy has closed by week 8 at 52%](images/crops.png)

## Learning from past cases

Sixty past projects as a table; the new one as a row of features.
Nearest analogues, multiple, quantile and logistic regression, a
conformal interval from the last twenty projects' own misses, and then
the decision: bid a fixed price, or cost plus? The histogram is the
fixed-price profit on the regression's overruns — a loss in almost a
third of futures; the chance it is the better deal (37%), what knowing
the overrun first would be worth, and the certainty equivalents are
beside it.

![The project-overrun example: a table of sixty projects with team, months, novelty, links and overrun; forecasts by analogues, regression, quantile regression and logistic regression; and the bid's profit distribution in the panel](images/analogues.png)

Rates learnt from counts by Bayes' rule: the new page's lift over the
old, the chance it is really better (95%), and eight landing pages
pooled so that small samples are trusted only as far as they deserve:

![The A/B-test example: the lift of B over A as a histogram, negative lifts in red, 4.9% of futures below zero; and a table of eight landing pages with raw and pooled rates](images/abtest.png)

Lifetimes, some of them not over yet: a Weibull fitted by maximum
likelihood counting the pumps still running, Kaplan–Meier beside it, and
the hour our pump, 3,000 hours old, fails:

![The pump example: twenty lifetimes with failed flags, the fitted Weibull shape 2.05 and scale 6,194, and a histogram of when a 3,000-hour-old pump fails, with a 34% chance within the warranty](images/lifetimes.png)

Futures that come out as words are counted:

![Where a hurricane's centre is after 120 hours, as bars: at sea 75%, the United States 22%, the Bahamas 2%, Cuba 1%](images/categories.png)

## Pictures and maps

Pictures and maps are loaded under a name and read by formula.
**Data › Pictures and Maps** lists them, with a picture's place on the
Earth:

![The Pictures and Maps dialog for the radar example: two radar frames with thumbnails and their bounds, 4 57 14 62, and a world map of 177 regions](images/pictures-dialog.png)

![The same dialog for the crop example: six weekly photographs of a field, the green cover visibly growing from week 1 to week 6](images/photos-dialog.png)

Places on a map: home on the latest radar frame, over the coast of
southern Norway and Sweden, with the rain approaching from the west:

![The radar example: the motion between two frames measured by formula, the chance of rain within the hour at 69%, and the panel showing the radar frame over the map with home marked](images/nowcast.png)

A block of cells is a heat map. Here the radar an hour ahead, every cell
read through the colour legend at the point its rain comes from:

![The radar extrapolated an hour ahead as a 32 by 24 grid of reflectivity in dBZ, drawn as a heat map of the rain cells](images/nowcast-grid.png)

The fuel under a wildfire, read off a land-cover picture through its
legend — forest, grass, town, road and water — and the chance each cell
burns, from a fire spread two thousand times with the wind:

![The wildfire example's fuel grid, 40 by 30 cells of values from 0 to 1, and its heat map showing forest, meadows, a lake, a river, a road and a town](images/wildfire-fuel.png)

![The wildfire example after simulating: one future's burnt cells as 1s and 0s, and the heat map of the chance each cell burns, highest near the start and stopped by the river and the road](images/wildfire.png)

Tracks: the official hurricane forecast with the National Hurricane
Center's cone errors as a random walk, a hundred simulated tracks drawn
over the world map:

![A hurricane track from the Caribbean to Florida with a hundred faint simulated tracks around the median, over the outlines of Cuba, Hispaniola, the Bahamas and Florida](images/storm.png)

Stations, coloured by what they measured, with kriging between them:

![Fifteen rain gauges on a map, coloured by rainfall, beside inverse-distance and kriged estimates at a farm between them](images/gauges.png)

## Particular futures

![The weather example: a fortnight of rain from a Markov chain and temperature from an AR(1) anomaly, with a fan of temperatures](images/weather.png)

![The football example: the rest of a six-team season played 20,000 times, title chances of 82%, 13% and 1%, and a fan of final points](images/football.png)

![The stock example: two years of weekly prices and a fan of the next year from geometric Brownian motion](images/stocks.png)

![The project schedule: PERT task estimates and the merge bias that makes plans built from averages run late](images/project.png)

## How these were made

The window renders itself to a PNG without a display:

```sh
GSK_RENDERER=cairo xvfb-run -a -s "-screen 0 1920x1080x24" \
  builddir/src/timemachine samples/abtest.tm --simulate --select B7 \
  --size 1280x800 --screenshot abtest.png
```

`--select` takes a cell or a range; the wider sheets used `--size
1600x860` or more. The menus, dialogs and the pointing picture were made
by driving the window under Xvfb with `xdotool` and capturing it with
ImageMagick's `import`.
