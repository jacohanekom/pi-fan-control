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

### From the APT repository

CI publishes to a signed APT repository (shared with other aipicam Raspberry Pi packages) hosted on Cloudflare R2, with two channels:

- **`main`** — pushing a `v*` tag publishes the clean release version here.
- **`nightly`** — every push (to any branch, and PRs) publishes a dev build here, versioned with a `+<UTC timestamp>` suffix.

```bash
curl -fsSL https://apt.aipicam.com/pubkey.asc | sudo gpg --dearmor -o /usr/share/keyrings/aipicam.gpg

# stable releases
echo "deb [signed-by=/usr/share/keyrings/aipicam.gpg] https://apt.aipicam.com main main" | sudo tee /etc/apt/sources.list.d/aipicam.list

# or nightly builds instead
echo "deb [signed-by=/usr/share/keyrings/aipicam.gpg] https://apt.aipicam.com nightly main" | sudo tee /etc/apt/sources.list.d/aipicam.list

sudo apt-get update
sudo apt-get install pi-fan-control
```

Builds run on GitHub's native `ubuntu-24.04-arm` hosted runner (no QEMU), matching the Dockerfile.native-arm64 build environment. Uses the same `R2_ACCOUNT_ID`, `R2_ACCESS_KEY_ID`, `R2_SECRET_ACCESS_KEY`, `GPG_PRIVATE_KEY`, and `GPG_KEY_ID` repo secrets described in [pi-block-cpu-cores](../pi-block-cpu-cores)'s README, since it publishes into the same shared repo.

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
