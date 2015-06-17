CFLAGS=

all: sockimux

debug: CFLAGS += -DDEBUG -g
debug: sockimux

sockimux: sockimux.c
	$(CC) $(CFLAGS) -o sockimux sockimux.c

test: sockimux
	./test.sh

clean:
	rm -rf sockimux test_tmp
