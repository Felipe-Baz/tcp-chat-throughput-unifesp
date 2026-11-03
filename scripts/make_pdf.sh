#!/bin/sh
# Compila report/relatorio.tex -> report/relatorio.pdf (requer pdflatex).
cd "$(dirname "$0")/../report" || exit 1
pdflatex -interaction=nonstopmode relatorio.tex > /dev/null
pdflatex -interaction=nonstopmode relatorio.tex | grep -E '^!|Output written'
rm -f relatorio.aux relatorio.log relatorio.out
