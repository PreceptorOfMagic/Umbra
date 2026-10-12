#!/bin/sh

PORT=47990

if ! curl -k https://localhost:$PORT > /dev/null 2>&1; then
  # Only graphical launch asks about input. The service runs sunshine directly.
  if [ -x /app/bin/umbra-input-setup ]; then
    /app/bin/umbra-input-setup --first-run || true
  fi
  (sleep 3 && xdg-open https://localhost:$PORT) &
  exec sunshine "$@"
else
  echo "Sunshine is already running, opening the web interface..."
  xdg-open https://localhost:$PORT
fi
