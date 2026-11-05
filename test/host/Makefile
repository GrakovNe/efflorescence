CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
MAIN    := ../../main
TESTS   := test_dimmer test_lamp_core test_gesture

.PHONY: test clean

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_dimmer: test_dimmer.c tinytest.h $(MAIN)/dimmer.c $(MAIN)/dimmer.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_dimmer.c $(MAIN)/dimmer.c

test_lamp_core: test_lamp_core.c tinytest.h $(MAIN)/lamp_core.c $(MAIN)/lamp_core.h $(MAIN)/dimmer.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_lamp_core.c $(MAIN)/lamp_core.c

test_gesture: test_gesture.c tinytest.h $(MAIN)/gesture.c $(MAIN)/gesture.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_gesture.c $(MAIN)/gesture.c

clean:
	rm -f $(TESTS)
