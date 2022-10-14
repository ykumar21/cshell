OBJS = main.o 

C = clang 

output: ${OBJS}
	${C} ${OBJS} -o build/exec

main.o: main.c
	${C} -c main.c

clean: 
	rm -f *.o build/exec

run: 
	./build/exec