CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
APPCONF := ../../components/appconf
CJSON   := $(HOME)/esp/esp-idf/components/json/cJSON
MAIN    := ../../main
TESTS   := test_dimmer test_lamp_core test_gesture test_appconf \
           test_config_merge test_light_cmd test_ota_logic

.PHONY: test clean

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_dimmer: test_dimmer.c tinytest.h $(MAIN)/dimmer.c $(MAIN)/dimmer.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_dimmer.c $(MAIN)/dimmer.c

test_lamp_core: test_lamp_core.c tinytest.h $(MAIN)/lamp_core.c $(MAIN)/lamp_core.h $(MAIN)/dimmer.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_lamp_core.c $(MAIN)/lamp_core.c

test_gesture: test_gesture.c tinytest.h $(MAIN)/gesture.c $(MAIN)/gesture.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_gesture.c $(MAIN)/gesture.c

test_appconf: test_appconf.c tinytest.h $(APPCONF)/appconf.c $(APPCONF)/include/appconf.h
	$(CC) $(CFLAGS) -I$(APPCONF)/include -o $@ test_appconf.c $(APPCONF)/appconf.c

test_config_merge: test_config_merge.c tinytest.h $(MAIN)/config_merge.c $(MAIN)/config_merge.h $(APPCONF)/appconf.c
	$(CC) $(CFLAGS) -Wno-deprecated-declarations -I$(MAIN) -I$(APPCONF)/include -I$(CJSON) -o $@ \
	    test_config_merge.c $(MAIN)/config_merge.c $(APPCONF)/appconf.c $(CJSON)/cJSON.c -lm

test_light_cmd: test_light_cmd.c tinytest.h $(MAIN)/light_cmd.c $(MAIN)/light_cmd.h
	$(CC) $(CFLAGS) -Wno-deprecated-declarations -I$(MAIN) -I$(CJSON) -o $@ \
	    test_light_cmd.c $(MAIN)/light_cmd.c $(CJSON)/cJSON.c -lm

test_ota_logic: test_ota_logic.c tinytest.h $(MAIN)/ota_logic.c $(MAIN)/ota_logic.h
	$(CC) $(CFLAGS) -Wno-deprecated-declarations -I$(MAIN) -I$(CJSON) -o $@ \
	    test_ota_logic.c $(MAIN)/ota_logic.c $(CJSON)/cJSON.c -lm

clean:
	rm -f $(TESTS)
