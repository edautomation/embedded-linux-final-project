CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -g -O0 -Wno-format
LDFLAGS ?= -lpthread -lrt
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

TARGET ?= serial_control

all: $(TARGET)

default : $(TARGET)

$(TARGET) : $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS) 

*.o: *.c 
	$(CC) $(CFLAGS) -c $< -o $@	

clean:
	rm -f $(OBJ) $(TARGET)

PHONY: all default clean