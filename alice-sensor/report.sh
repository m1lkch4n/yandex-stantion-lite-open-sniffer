#!/bin/sh
# alice-sensor quick report

L=/bkupgrade/data/sensor/logs

echo "===== Wi-Fi BSS (latest value per BSSID) ====="
awk -F'|' '{k=$2; rss[k]=$3; ssid[k]=$5; fr[k]=$4; t[k]=$1}
END { for (k in rss) printf "%s|%s|%s|%s|%s\n", k, rss[k], fr[k], ssid[k], t[k] }' "$L/wifi.log" 2>/dev/null \
  | sort -t'|' -k2 -nr | sed 's/|/  /g' | head -40

echo
echo "===== Bluetooth LE (latest value per address) ====="
awk -F'|' '{rss[$2]=$3; t[$2]=$1}
END { for (k in rss) printf "%s|%s|%s\n", k, rss[k], t[k] }' "$L/ble.log" 2>/dev/null \
  | sort -t'|' -k2 -nr | sed 's/|/  /g' | head -40

echo
echo "===== LAN (latest value per MAC) ====="
awk -F'|' '{ip[$3]=$2; t[$3]=$1}
END { for (k in ip) printf "%s|%s|%s\n", k, ip[k], t[k] }' "$L/lan.log" 2>/dev/null \
  | sort | sed 's/|/  /g' | head -40

echo
echo "===== events (last 20) ====="
tail -n 20 "$L/events.log" 2>/dev/null
echo
echo "===== sizes ====="
ls -l "$L" 2>/dev/null
