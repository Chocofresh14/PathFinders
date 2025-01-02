# Makefile pour PathFinders
CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lcurl
TARGET = pfinders
INSTALL_DIR = /usr/local/bin

all: $(TARGET)

$(TARGET): pathfinders.c
	$(CC) $(CFLAGS) -o $(TARGET) pathfinders.c $(LDFLAGS)
	sudo chown root:root $(TARGET)
	sudo chmod u+s $(TARGET)

install: all
	sudo cp $(TARGET) $(INSTALL_DIR)/
	sudo chown root:root $(INSTALL_DIR)/$(TARGET)
	sudo chmod u+s $(INSTALL_DIR)/$(TARGET)

uninstall:
	sudo rm -f $(INSTALL_DIR)/$(TARGET)

clean:
	rm -f $(TARGET)