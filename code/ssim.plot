set boxwidth 0.2
set style fill solid

# set style fill solid 0.25 border -1
# set style boxplot pointtype 7
# set style data boxplot

set xlabel "Groups of ROWS, within COLS=1..6 in each group"
set ylabel "SSIM score"
set yrange [0.5:1]
set title 'SSIM results' font 'Arial,14';
set xtics ( "1x?" 3.5, "2x?" 11.5, "3x?" 19.5, "4x?" 27.5, "5x?" 35.5, "6x?" 43.5, "7x?" 51.5, "8x?" 59.5 )

# plot for [i=1:3] 'data.txt' using (i):i notitle
plot "/tmp/UML5.txt"   using ($1+0.3):4  with boxes title "high"
replot "/tmp/UML5.txt" using ($1-0.0):10 with boxes title "middle"
set terminal pdf
set output "ssim.pdf"
replot "/tmp/UML5.txt" using ($1-0.3):7  with boxes title "low"

