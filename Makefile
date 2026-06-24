CXX      = g++
CXXFLAGS = -Wall -O2 -std=c++17
LDFLAGS  = -llgpio -lpthread
TARGET   = fan_control
SRCDIR   = src

all: $(TARGET)

$(TARGET): $(SRCDIR)/main.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCDIR)/main.cpp $(LDFLAGS)

install:
	install -d $(DESTDIR)/usr/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/bin/
	install -d $(DESTDIR)/usr/share/pi-fan-control/src
	install -m 644 $(SRCDIR)/main.cpp $(DESTDIR)/usr/share/pi-fan-control/src/
	install -m 644 Makefile $(DESTDIR)/usr/share/pi-fan-control/
	install -d $(DESTDIR)/etc
	install -m 644 debian/pi-fan-control.conf $(DESTDIR)/etc/pi-fan-control.conf

clean:
	rm -f $(TARGET)
