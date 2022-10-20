
.PHONY:all
CFLAGS= -O2 -Wall

all: fan-control

clean:
	$(RM) fan-control *.o

fan-control: fan-control.o
	$(CC) $(CFLAGS) $^ -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@
