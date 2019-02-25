set boxwidth 0.2
set style fill solid

# set style fill solid 0.25 border -1
# set style boxplot pointtype 7
# set style data boxplot

set xlabel "Groups of ROWS, with COLS=1..6 in each group"
set ylabel "SSIM score"
set yrange [0.5:1]
set title 'SSIM results' font 'Serif,14';
set xtics ( "1x..." 3.5, "2x..." 11.5, "3x..." 19.5, "4x..." 27.5, "5x..." 35.5, "6x..." 43.5, "7x..." 51.5, "8x..." 59.5 )
set xtics font "Serif,12"
set ytics font "Serif,12"
set xlabel font "Serif,12"
set ylabel font "Serif,12"
set grid ytics
set border 3
set tics scale 0,0.001

set label "1 → 6" at 3.5,0.94 center font 'Serif,14'
set label "1 → 6" at 11.5,0.94 center font 'Serif,14'
set label "1 → 6" at 19.5,0.8  center font 'Serif,14'
set label "1 → 6" at 27.5,0.94 center font 'Serif,14'
set label "1 → 6" at 35.5,0.75 center font 'Serif,14'
set label "1 → 6" at 43.5,0.75 center font 'Serif,14'
set label "1 → 6" at 51.5,0.8  center font 'Serif,14'
# set label "1 → 6" at 59.5,0.92 center font 'Serif,14'

# plot for [i=1:3] 'data.txt' using (i):i notitle
plot "/tmp/UML5.txt"   using ($1+0.3):4  with boxes title "high"
replot "/tmp/UML5.txt" using ($1-0.0):10 with boxes title "middle"
set terminal pdf
set output "ssim.pdf"
replot "/tmp/UML5.txt" using ($1-0.3):7  with boxes title "low"
