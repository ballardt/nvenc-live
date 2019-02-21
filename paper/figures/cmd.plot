set dgrid 8,8
set hidden3d
set xlabel "rows"
set ylabel "cols"
set key off

set terminal pdf

set zlabel "MBytes"
set output "Sizes.pdf"
splot "Sizes-for-youtubeclip.txt" u 1:2:($3/1000000) with lines
	
set zlabel "seconds"
set output "Times.pdf"
splot "Times-for-youtubeclip.txt" u 1:2:3 with lines
