s:main.c ds.c
	cc -g -o s main.c ds.c tree.c
.PYTHON:clean
clean:
	rm -rf *.o core* a a.out s
