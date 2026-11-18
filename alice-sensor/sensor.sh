#!/bin/sh
# alice-sensor — 24/7 Wi-Fi / Bluetooth / LAN sensor logger
# Part of alice-station-open github.com/YOURUSER/alice-station-open
# Started by /etc/init.d/S98netwatch or cron @reboot

SENSOR_DIR=/opt/alice-sensor
CONFIG=$SENSOR_DIR/config

[ -f "$CONFIG" ] && . "$CONFIG"

: ${USB_MOUNT:=/tmp/usb/Ventoy}
: ${USB_DIR:=alice-sensor}
: ${LOG_MAX_DAYS:=3}
: ${WIFI_EVERY:=90}
: ${BLE_EVERY:=180}
: ${LAN_EVERY:=300}
: ${BLE_SCAN_SEC:=8}
: ${SCAN_TIMEOUT:=30}
: ${PING_PARALLEL:=32}
: ${LOG_KEEP:=500}
: ${PING_HOST:=192.168.1.1}

# Куда писать логи: USB-flash если доступна, иначе /bkupgrade
if [ -d "$USB_MOUNT" ] && [ -w "$USB_MOUNT" ]; then
    BASE=$USB_MOUNT/$USB_DIR
else
    BASE=/bkupgrade/data/sensor
fi

mkdir -p "$BASE/logs" "$BASE/state/wifi" "$BASE/state/ble" "$BASE/state/lan"

TS() { date '+%Y-%m-%d %H:%M:%S'; }
LOG=$BASE/logs
STATE=$BASE/state

# Символический якорь — если USB отключена, сенсор продолжает на /bkupgrade
run_to() { # run_to <secs> <outfile> <cmd...>
    _t=$1; _out=$2; shift 2
    "$@" > "$_out" 2>/dev/null &
    _p=$!
    ( sleep "$_t"; kill -0 $_p 2>/dev/null && kill $_p 2>/dev/null ) &
    _w=$!
    wait $_p 2>/dev/null
    kill $_w 2>/dev/null
}

note_event() { # note_event <kind> <key> <text>
    # флаг появления: первое появление устройства
    if [ ! -f "$STATE/$1/$2" ]; then
        echo "$(TS)|$1|new|$2|$3" >> "$LOG/events.log"
        : > "$STATE/$1/$2"
    fi
}

note_change() { # note_change <kind> <key> <delta> <text>
    # значительное изменение RSSI / появление на новой частоте
    echo "$(TS)|$1|change|$2|$3" >> "$LOG/events.log"
}

# --- Очистка: удаляем записи старше LOG_MAX_DAYS ---
cleanup() {
    local kind=$1; shift
    local logf=$LOG/${kind}.log
    [ -f "$logf" ] || return 0
    local tmp=$(mktemp)
    awk -v max_days="${LOG_MAX_DAYS}" '
        BEGIN { t = systime() - max_days * 86400 }
        NR == 1 { if ($1 < t) next; else { print; next } }
        { print }
    ' "$logf" > "$tmp" && mv "$tmp" "$logf"
    # удаляем пустые файлы состояния
    find "$STATE/${kind}" -type f -empty -delete 2>/dev/null
}

# --- Wi-Fi скан ---
scan_wifi() {
    ts=$(TS)
    tmp=/tmp/sensor.wifi.$$
    run_to "$SCAN_TIMEOUT" "$tmp" /opt/bin/iw dev wlan0 scan
    [ -s "$tmp" ] || { rm -f "$tmp"; return 1; }

    awk -v ts="$ts" '
        function flush() {
            if (bssid != "") {
                gsub(/\|/, "_", ssid)
                printf "%s|%s|%s|%s|%s\n", ts, bssid, rssi, freq, ssid
            }
            bssid = ""; rssi = ""; freq = ""; ssid = ""
        }
        /^BSS /        { flush(); bssid = $2; sub(/\(.*/, "", bssid); next }
        /^\tfreq:/     { freq = $2; next }
        /^\tsignal:/   { rssi = $2; next }
        /^\tSSID:/     { ssid = $0; sub(/^\tSSID: */, "", ssid); next }
        END            { flush() }
    ' "$tmp" > "$tmp.parsed"
    rm -f "$tmp"
    [ -s "$tmp.parsed" ] || { rm -f "$tmp.parsed"; return 1; }

    # ротация по размеру
    [ -f "$LOG/wifi.log" ] && wc -c < "$LOG/wifi.log" | awk '{ if ($1 > 10485760) exit 1 }' || true
    cat "$tmp.parsed" >> "$LOG/wifi.log"
    while IFS='|' read -r t b r f s; do
        [ -n "$b" ] || continue
        note_event wifi "$b" "$b|$r|$f|$s"
    done < "$tmp.parsed"
    rm -f "$tmp.parsed"
    return 0
}

# --- BLE LE scan ---
scan_ble() {
    ts=$(TS)
    out=/tmp/sensor.btmon.$$
    snoop=/tmp/sensor.bt.$$.snoop
    btmon -w "$snoop" > "$out" 2>/dev/null &
    bp=$!
    sleep 1
    ( echo "menu scan"; echo "transport le"; echo "back"; echo "scan on"
      sleep "$BLE_SCAN_SEC"
      echo "scan off"; sleep 1; echo quit ) | bluetoothctl >/dev/null 2>&1 &
    cp=$!
    ( sleep $((BLE_SCAN_SEC + 12)); kill -0 $cp 2>/dev/null && kill $cp 2>/dev/null ) &
    cw=$!
    wait $cp 2>/dev/null
    kill $cw 2>/dev/null
    kill $bp 2>/dev/null
    wait $bp 2>/dev/null

    awk -v ts="$ts" '
        function flush() {
            if (addr != "" && rssi != "" && rssi + 0 != -127)
                printf "%s|%s|%s\n", ts, addr, rssi
            addr = ""; rssi = ""
        }
        /LE Advertising Report/ { flush(); next }
        /^ *Address: /          { if (addr == "") addr = $2; next }
        /^ *RSSI: /             { if (rssi == "") { rssi = $2; sub(/ dBm.*/, "", rssi) } next }
        END                     { flush() }
    ' "$out" \
    | awk -F'|' 'BEGIN {OFS="|"}
        { if (!($2 in best) || $3 + 0 > best[$2] + 0) { best[$2] = $3; line[$2] = $0 } }
        END { for (a in line) print line[a] }' > "$out.parsed"

    rm -f "$out" "$snoop"
    [ -s "$out.parsed" ] || { rm -f "$out.parsed"; return 1; }

    cat "$out.parsed" >> "$LOG/ble.log"
    while IFS='|' read -r t a r; do
        [ -n "$a" ] || continue
        note_event ble "$a" "$a|$r"
    done < "$out.parsed"
    rm -f "$out.parsed"
    return 0
}

# --- LAN sweep ---
scan_lan() {
    ts=$(TS)
    [ -z "$LAN_PREFIX" ] && LAN_PREFIX=$(ip -4 addr show wlan0 2>/dev/null \
        | awk '/inet /{split($2,a,"/"); n=split(a[1],o,"."); print o[1]"."o[2]"."o[3]"."; exit}')
    [ -z "$LAN_PREFIX" ] && LAN_PREFIX=192.168.1.

    i=1; n=0
    while [ $i -le 254 ]; do
        ping -c 1 -W 1 "$LAN_PREFIX$i" >/dev/null 2>&1 &
        n=$((n + 1))
        [ $((n % PING_PARALLEL)) -eq 0 ] && wait
        i=$((i + 1))
    done
    wait

    tmp=/tmp/sensor.lan.$$
    awk -v ts="$ts" 'NR > 1 && $4 != "00:00:00:00:00:00" && $4 != "" \
                     { printf "%s|%s|%s\n", ts, $1, $4 }' /proc/net/arp > "$tmp"
    [ -s "$tmp" ] || { rm -f "$tmp"; return 1; }
    cat "$tmp" >> "$LOG/lan.log"
    while IFS='|' read -r t ip mac; do
        [ -n "$mac" ] || continue
        note_event lan "$mac" "$ip|$mac"
    done < "$tmp"
    rm -f "$tmp"
    return 0
}

# --- Heartbeat ---
heartbeat() {
    rssi=$(/opt/bin/iw dev wlan0 link 2>/dev/null | awk '/signal:/{print $2; exit}')
    up=$(uptime | sed 's/.*up //; s/,.*//')
    # проверка что сенсор видит устройства
    wc=$(cat "$LOG/wifi.log" "$LOG/ble.log" "$LOG/lan.log" 2>/dev/null | wc -l)
    echo "$(TS)|beat|up=$up|rssi=${rssi:-?}|records=$wc" >> "$LOG/events.log"
}

# --- Контроль запущенного процесса ---
check_alive() {
    [ -f /var/run/alice-sensor.pid ] && kill -0 $(cat /var/run/alice-sensor.pid) 2>/dev/null && return 0
    return 1
}

start_sensor() {
    check_alive && return 0
    "$SENSOR_DIR/sensor.sh" >/dev/null 2>&1 &
    echo $! > /var/run/alice-sensor.pid
    echo "$(TS)|sensor|started" >> "$LOG/events.log"
}

# --- MAIN ---
PIDFILE=/var/run/alice-sensor.pid
case "$1" in
start)
    start_sensor
    ;;
stop)
    [ -f "$PIDFILE" ] && kill $(cat "$PIDFILE") 2>/dev/null
    rm -f "$PIDFILE"
    ;;
restart)
    "$0" stop; sleep 1; "$0" start
    ;;
status)
    if check_alive; then
        echo "running pid=$(cat $PIDFILE)"
        uptime | sed 's/.*up/up /'
    else
        echo "not running"
    fi
    ;;
*)
    # режим демона
    if [ "x$1" = "xdaemon" ] || [ "x$1" = "x" ]; then
        :
    else
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
    fi
    ;;
esac

# если вызваны без аргументов — запускаем main loop
if [ "x$1" = "xdaemon" ] || [ "x$1" = "x" ]; then
    trap 'exit 0' TERM INT
    start_sensor

    now=$(date +%s)
    nw=$now; nb=$now; nl=$now; nh=$((now + 3600)); nc=$((now + 86400))

    echo "$(TS)|boot|BASE=$BASE|up=$(uptime | sed 's/.*up //; s/,.*//')" >> "$LOG/events.log"

    while :; do
        now=$(date +%s)
        [ $now -ge $nw ] && { scan_wifi; nw=$((now + WIFI_EVERY)); }
        [ $now -ge $nb ] && { scan_ble;  nb=$((now + BLE_EVERY)); }
        [ $now -ge $nl ] && { scan_lan;  nl=$((now + LAN_EVERY)); }
        [ $now -ge $nh ] && { heartbeat; nh=$((now + 3600)); }
        [ $now -ge $nc ] && {
            # ночная очистка
            for k in wifi ble lan; do cleanup $k; done
            nc=$((now + 86400))
        }
        [ ! -d "$USB_MOUNT" ] && [ "$BASE" != "${BASE#/bkupgrade}" ] && {
            # USB пропал — переоткрываем на /bkupgrade
            BASE=/bkupgrade/data/sensor
            mkdir -p "$BASE/logs" "$BASE/state"
        }
        sleep 15
    done
fi
