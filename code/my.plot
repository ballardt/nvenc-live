set boxwidth 0.1
set style fill solid

# set style fill solid 0.25 border -1
# set style boxplot pointtype 7
# set style data boxplot

set xlabel "SSIM results for scaled and well-aligned number of rows"
set ylabel "SSIM score" font 'Serif,14';
set yrange [0.5:1]
set title 'SSIM results' font 'Serif,14';
set xtics ( "5 rows (scaled)" 1, "8 rows (aligned)" 3 )
set xtics font "Serif,14"
set ytics font "Serif,14"
set xlabel font "Serif,14"
set ylabel font "Serif,14"
set grid ytics
set border 3
set tics scale 0,0.001
set key left

set label "1 column" at 0.5,0.75 center font 'Serif,14'
set label "6 columns" at 1.5,0.75 center font 'Serif,14'
set label "1 column" at 2.5,0.94 center font 'Serif,14'
set label "6 columns" at 3.5,0.94 center font 'Serif,14'

# plot for [i=1:3] 'data.txt' using (i):i notitle
plot "values.txt" using 1:3 index 0  with boxes title "low" lt rgb "red"
replot "values.txt" using 1:3 index 1  with boxes title "middle" lt rgb "green"
set terminal pdf
set output "my.pdf"
replot "values.txt" using 1:3 index 2  with boxes title "high" lt rgb "blue"

