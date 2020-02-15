# mininal status bar 
# Copyright (c) 2019 Pierre Evenou
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

.POSIX:

MAJOR = 0
MINOR = 1
PATCH = 0

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

# includes and libs
INCS = `pkg-config --cflags xcb gtk+-3.0 libudev`
LIBS = -liw `pkg-config --libs xcb gtk+-3.0 libudev`

# flags
NDEBUG ?= 0
ifeq ($(NDEBUG), 1)
    CFLAGS += -O2
    CPPFLAGS += -DNDEBUG
else
    CFLAGS += -g -Wno-unused-parameter
endif
CPPFLAGS += -DVERSION=\"$(MAJOR).$(MINOR).$(PATCH)\"
CFLAGS += $(INCS) $(CPPFLAGS)
LDFLAGS += $(LIBS)

SRC = $(wildcard *.c)
HDR = $(wildcard *.h)
OBJ = $(SRC:.c=.o)

all: mbar

.c.o:
	$(CC) $(CFLAGS) -c $<

resources.c: mbar.gresource.xml mbar.ui mbar.css
	glib-compile-resources --target=resources.c --generate-source mbar.gresource.xml 

$(OBJ): $(HDR)

mbar: $(OBJ) resources.c

	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f mbar $(OBJ)

install: mbar
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f mbar $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/mbar

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mbar

.PHONY: all clean install uninstall
