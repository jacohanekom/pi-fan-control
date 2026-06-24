# pi-fan-control

PWM fan controller for Raspberry Pi 5. Adjusts a 4-wire fan speed based on CPU temperature and monitors RPM via the tach wire. Exposes a TCP socket for manual control and status queries.

## Requirements

- Raspberry Pi 5 (arm64)
- 4-wire PWM fan connected to GPIO 12 (PWM) and GPIO 24 (tach, BCM)
- `liblgpio-dev`

## Build

```sh
sudo apt install liblgpio-dev
make
```

## Install

### From Debian package

```sh
sudo dpkg -i pi-fan-control_*.deb
sudo systemctl enable --now pi-fan-control
```

### Manual

```sh
sudo make install
sudo systemctl enable --now pi-fan-control
```

## Configuration

Edit `/etc/pi-fan-control.conf` (changes take effect after `sudo systemctl restart pi-fan-control`):

```
# Tach pin (BCM numbering)
tach_pin 24

# TCP port for the control socket
port 7777

# Temperature/speed thresholds (°C → fan %)
# Fan is off below the first entry, full speed at or above the last entry
temp_speed 40 0
temp_speed 50 25
temp_speed 60 50
temp_speed 70 75
temp_speed 80 100
```

## Control socket

Send commands via `nc` (or any TCP client) to `localhost:7777`:

| Command | Description |
|---|---|
| `status` | Returns current temp, fan speed, RPM, and mode |
| `speed=<0-100>` | Set fan to a fixed speed (%) |
| `auto` | Return to automatic temperature-based control |

```sh
echo "status"   | nc localhost 7777
echo "speed=50" | nc localhost 7777
echo "auto"     | nc localhost 7777
```

Example status response:

```
temp=52.5 speed=25 rpm=1320 mode=auto
```
