main: libtgcommon.a
	gcc -g telega.c -Wall -L. -ltgcommon -lcurl -ljson-c -lpthread -o run

libtgcommon.a: tgcommon.o
	ar cr libtgcommon.a tgcommon.o

tgcommon.o: tgcommon.c
	gcc -g -Wall -lcurl -ljson-c -c tgcommon.c -lpthread -o tgcommon.o

run: main
	./run

clean:
	rm -f *.o *.a *.gch run
