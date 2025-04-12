#!/bin/bash

# Script to trigger Daisy bootloader via serial and then run make program-dfu

# --- Configuration ---
# The command your firmware expects to receive
BOOTLOADER_COMMAND="ENTER_BOOTLOADER\\n" 
# Time to wait (in seconds) for the device to reset after sending command
RESET_WAIT_TIME=1.5 

# --- Get Serial Port Argument ---
if [ -z "$1" ]; then
  echo "Usage: $0 <serial_port>"
  echo "Example: $0 /dev/tty.usbmodem1234561"
  # Attempt to guess the port (may not be reliable)
  PORT_GUESS=$(ls /dev/tty.usbmodem* | head -n 1)
  if [ -n "$PORT_GUESS" ]; then
      echo "Attempting to use guessed port: $PORT_GUESS"
      SERIAL_PORT="$PORT_GUESS"
  else
      exit 1
  fi
else
  SERIAL_PORT="$1"
fi

# --- Check if Port Exists ---
if [ ! -c "$SERIAL_PORT" ]; then
  echo "Error: Serial port '$SERIAL_PORT' not found."
  exit 1
fi

# --- Send Command to Daisy ---
echo "Sending bootloader command to $SERIAL_PORT..."
# Use echo -ne to prevent extra newline and interpret \n
echo -ne "$BOOTLOADER_COMMAND" > "$SERIAL_PORT"

# Check if echo command succeeded (basic check)
if [ $? -ne 0 ]; then
  echo "Error: Failed to write to serial port $SERIAL_PORT."
  echo "Check permissions or if another program is using the port."
  echo "You might need to configure the port first using 'stty'"
  exit 1
fi

# --- Wait for Reset ---
echo "Waiting ${RESET_WAIT_TIME}s for device to reset..."
sleep $RESET_WAIT_TIME

# --- Run Make Program-DFU ---
echo "Running make program-dfu..."
make program-dfU

# Check if make succeeded
if [ $? -ne 0 ]; then
  echo "Error: 'make program-dfu' failed."
  exit 1
fi

echo "Flash process complete."
exit 0
