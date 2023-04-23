CC = gcc
CFLAGS = -g -Wall -Wextra -pedantic
LFLAGS = -lX11
OBJ = xkeycursor.o
BINARY = xkeycursor
BUILDDIR = build/

all: $(BINARY)

$(BINARY): $(OBJ)
	mkdir -p $(BUILDDIR)
	$(CC) $(OBJ) $(CFLAGS) $(LFLAGS) -o $(BUILDDIR)$(BINARY)

%.o: %.c
	$(CC) -c $(CFLAGS) $<




#Because of a weird /dev/uinput quirk, the permissions are rw for root and
#nothing for other users, at least on my system. Because of this, we need
#to set the binary's owner to root and then append the setuid permission
#to all permission groups so that the program runs by default with the
#permissions of the superuser and not the logged-in user.

install: $(BINARY)
	printf "\n"
	echo "*****************************************************"
	echo "WARNING"
	printf "\n"
	echo "IF YOU ARE NOT RUNNING MAKE INSTALL AS ROOT, YOU WILL"
	echo "HAVE TO RUN THE BINARY WITH ROOT!"
	echo "*****************************************************"
	printf "\n"

	sudo chown root:root $(BUILDDIR)$(BINARY)
	sudo chmod a+s $(BUILDDIR)$(BINARY)

clean:
	rm -rf *.o
	rm -rf $(BINARY)
	rm -rf $(BUILDDIR)

.PHONY: all clean install
.SILENT: install
