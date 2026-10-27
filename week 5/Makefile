CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
TARGET = page_replacement
SOURCES = main.c core.c ui.c logger.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean run
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)
