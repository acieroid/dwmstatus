CC = gcc
CFLAGS = -Wall -Wextra -g -O2
LIBS = -lX11
TARGET = dwmstatus

$(TARGET) : dwmstatus.c
	$(CC) dwmstatus.c -o $@ $(LIBS) $(CFLAGS)
	$(GZIP) $(MANPAGE)

clean:
	@rm -f $(TARGET)
