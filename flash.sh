#!/usr/bin/env bash
# Flash Open Firenet firmware to ESP32-S3
# Preserves NVS partition (WiFi credentials are kept across flashes)

set -e

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS] [PORT]

Flash the Open Firenet firmware onto an ESP32-S3.

Arguments:
  PORT              Serial port of the ESP32-S3 (default: /dev/ttyACM0)

Options:
  -h, --help        Show this help message and exit
  --ota <ip>        Flash via WiFi OTA instead of serial (requires curl)

Examples:
  $(basename "$0")                    Flash via serial /dev/ttyACM0
  $(basename "$0") /dev/ttyACM1      Flash via a specific serial port
  $(basename "$0") --ota 192.168.1.x  Flash via WiFi OTA

Notes:
  - Requires: arduino-cli, esptool (serial mode) or curl (OTA mode)
  - Only bootloader + partition table + app are flashed in serial mode.
    The NVS partition (0x9000) is left untouched — WiFi credentials survive.
  - The port is released automatically if held by another process.
EOF
}

OTA_IP=""
PORT="/dev/ttyACM0"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --ota)
      shift
      OTA_IP="${1:?--ota requires an IP address}"
      ;;
    --ota=*)
      OTA_IP="${1#--ota=}"
      ;;
    -*)
      echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    *)
      PORT="$1"
      ;;
  esac
  shift
done

BUILD_DIR="$(dirname "$0")/.build"

echo "=== Open Firenet flash ==="
if [[ -n "$OTA_IP" ]]; then
  echo "Mode   : OTA (WiFi)"
  echo "Target : http://$OTA_IP/update"
else
  echo "Mode   : Serial"
  echo "Port   : $PORT"
fi
echo "Board  : ESP32-S3"
echo ""

# Compile
echo "[1/2] Compiling..."
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=4M,PartitionScheme=default,PSRAM=disabled \
  --output-dir "$BUILD_DIR" \
  "$(dirname "$0")/open-firenet/open-firenet.ino"

if [[ -n "$OTA_IP" ]]; then
  # OTA flash via HTTP upload
  echo "[2/2] Flashing via OTA → http://$OTA_IP/update ..."
  curl -s -X POST "http://$OTA_IP/update" \
    -F "firmware=@$BUILD_DIR/open-firenet.ino.bin" \
    --progress-bar \
    -w "\n"
  echo ""
  echo "Done. Device is rebooting — watch the serial monitor at 115200 baud."
else
  # Release port if held
  fuser -k "$PORT" 2>/dev/null || true
  sleep 1

  # Flash (bootloader + partition table + app only — NVS at 0x9000 is untouched)
  echo "[2/2] Flashing via serial → $PORT ..."
  esptool --chip esp32s3 --port "$PORT" --baud 460800 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB \
    0x0000  "$BUILD_DIR/open-firenet.ino.bootloader.bin" \
    0x8000  "$BUILD_DIR/open-firenet.ino.partitions.bin" \
    0x10000 "$BUILD_DIR/open-firenet.ino.bin"

  echo ""
  echo "Done. Connect a serial monitor at 115200 baud to follow startup."
fi
