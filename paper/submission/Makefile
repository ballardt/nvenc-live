all: main.pdf

main.aux: main.tex
	pdflatex main

main.bbl:	main.aux \
		bibliography.bib
	bibtex main
	
main.pdf: main.bbl
	pdflatex main
	pdflatex main

clean:
	rm -f main.aux main.bbl main.blg main.log main.out

realclean: clean
	rm -f main.pdf

