# Яндекс.Станция Лайт как Wi-Fi / BLE / LAN сенсор

## О проекте

**Задача:** превратить root-доступную колонку **Яндекс.Станция Лайт / Микро** в круглосуточный сетевой сенсор, который собирает данные об окружающих устройствах — без USB Wi-Fi адаптера, без паяльника, без разборки корпуса.

Что было сделано в рамках этого проекта:

| Достижение | Статус |
|---|---|
| Root-доступ через SSH по ключу | ✅ |
| Kernel module `alice-tools.ko` — прямой доступ к firmware | ✅ |
| Реверс-инжиниринг драйвера `sprdwl_ng` — найден `g_sprdwl_priv` | ✅ |
| Реверс-инжиниринг прошивки `wcnmodem.bin` — найдены команды 85/87 (sniffer) | ✅ |
| Сенсор Wi-Fi (iw scan) — 27 AP обнаружено | ✅ |
| Сенсор BLE (bluetoothctl LE scan) — 81 устройство | ✅ |
| Сенсор LAN (ping sweep + ARP) — 3 хоста | ✅ |
| Автозапуск при каждой загрузке | ✅ |
| Хранение на UBIFS (`/bkupgrade`) — survives reboot | ✅ |

---

## Зачем это нужно

**Сетевая разведка и мониторинг** — станция работает 24/7, тихо сидит в углу, никто не знает что она собирает данные:

- **Wi-Fi** — все AP в радиусе действия (MAC, SSID, RSSI, частота). Можно отслеживать появление новых сетей, перемещение устройств по RSSI
- **Bluetooth LE** — все BLE-устройства вокруг (телефоны, часы, наушники, метки). MAC-адрес + RSSI = приблизительное положение
- **LAN** — кто в сети (ARP). Смартфоны, компьютеры, IoT-устройства

**Никакого дополнительного оборудования** — станция работает на встроенном чипсете **Unisoc UWE5621 (Marlin3)**. Не нужен USB Wi-Fi адаптер, не нужен Ethernet, не нужен паяльник.

---

## Железо

### Yandex Station Micro v1.2 (yandex-station-lite)

```
Процессор:    Amlogic A113X (ARM Cortex-A35 @ 1.2 GHz, 4 ядра)
Wi-Fi/BT:      Unisoc UWE5621 (Marlin3) — SDIO, встроенная антенна
RAM:           512 MB DDR3
Flash:         NAND 4 GB (UBIFS)
Сеть:          Только Wi-Fi (2.4/5 GHz, STA mode)
Питание:       USB-C, 5V 2A
Размеры:       90×90×55 мм
Цена на Avito: ~1500–3000₽
```

```
Station MAC:   b8:87:6e:7f:dc:80
BLE MAC:       b8:87:6e:7f:dc:81
Wi-Fi чип:     0x2355 (Unisoc UWE5621)
Прошивка:     Marlin3
Ядро:          4.9.68 #1 SMP PREEMPT aarch64
```

### Чипсет Unisoc UWE5621 (Marlin3)

Встроенный в станцию Wi-Fi/Bluetooth модуль. Работает через SDIO интерфейс. Драйвер `sprdwl_ng` (ядро Linux) управляет обменом с firmware `wcnmodem.bin` через HIF-канал.

---

## Как это работает

### Сенсор (sensor.sh)

```
sensor.sh daemon
  ├── scan_wifi()    iw dev wlan0 scan         → logs/wifi.log
  ├── scan_ble()    btmon + bluetoothctl LE    → logs/ble.log
  ├── scan_lan()    ping sweep /proc/net/arp   → logs/lan.log
  ├── note_event()  первое появление устройства → logs/events.log
  └── cleanup()     удаление старых записей   → каждые сутки
```

Логи хранятся в `/bkupgrade/data/sensor/logs/` — это **UBIFS раздел** на NAND-флеш, **не tmpfs**. Данные **сохраняются после перезагрузки**.

Сенсор запускается автоматически — при каждой загрузке станции скрипт `S98netwatch` (защита Wi-Fi от разрывов) стартует и наш сенсор.

### Kernel module alice-tools.ko

**Зачем нужен:** для экспериментов с firmware — прямой доступ к драйверу `sprdwl_ng` из пространства пользователя.

```
/sys/kernel/debug/alice/priv     — priv-указатель, hw_priv, ops table
/sys/kernel/debug/alice/syncver  — вызов sprdwl_sync_version
/sys/kernel/debug/alice/fwcmd    — отправка команд firmware (cmd=85 raw=1)
/sys/kernel/debug/alice2/addr    — задать адрес для чтения
/sys/kernel/debug/alice2/peek    — чтение произвольной памяти ядра
```

### Реверс-инжиниринг прошивки

Файл `fw-analysis/` содержит Python-скрипты для анализа `wcnmodem.bin`:

```bash
python fw-analysis/nametbl.py     #提取 команд прошивки enum (64 команды)
python fw-analysis/jtab.py         # найти таблицу диспетчеризации команд
python fw-analysis/parse.py        # распарсить WCNE-контейнер прошивки
```

Ключевые находки:

| Команда | ID | Описание |
|---------|----|----------|
| `WIFI_CMD_GET_INFO` | 1 | Работает ✅ — MAC, версия, чипсет |
| `WIFI_CMD_SNIFFER_MODE_SWITCH` | **85** | Заблокирована драйвером |
| `WIFI_CMD_SNIFFER_TRIGGER` | **87** | Заблокирована драйвером |

Драйвер блокирует команды 85/87 через `sprdwl_api_available_check()` — в его таблице версий нет записей для этих ID. Обход (`sprdwl_cmd_send_to_ic()`) вешает Wi-Fi, потому что формат payload неизвестен.

---

## Быстрый старт

### 1. Загрузка файлов на станцию

```bash
scp -r alice-sensor/ root@192.168.1.55:/opt/alice-sensor/
ssh root@192.168.1.55 'chmod +x /opt/alice-sensor/sensor.sh'
```

### 2. Запуск

```bash
# вручную
ssh root@192.168.1.55 '/opt/alice-sensor/sensor.sh daemon'

# или автоматически при каждой загрузке
ssh root@192.168.1.55 '/etc/init.d/S98netwatch start'
```

### 3. Просмотр логов

```bash
# на станции
ssh root@192.168.1.55 '/opt/alice-sensor/report.sh'

# последние события
ssh root@192.168.1.55 'tail -f /bkupgrade/data/sensor/logs/events.log'

# конкретный канал
ssh root@192.168.1.55 'tail /bkupgrade/data/sensor/logs/wifi.log'
```

---

## Файлы

```
alice-sensor/
  sensor.sh        — основной демон (запускать через daemon)
  config           — настройки интервалов и хранения
  report.sh        — человеческий отчёт по собранным данным
  S99sensor        — init-скрипт-заглушка

kmod/
  alice-tools/     — kernel module для доступа к sprdwl_ng
  alice-peek/      — kernel module для чтения памяти ядра

fw-analysis/       — Python-скрипты реверс-инжиниринга прошивки

alice-notes.md     — полные технические заметки (для разработчиков)
```

---

## Настройка (config)

```bash
WIFI_EVERY=90      # интервал Wi-Fi сканирования (сек)
BLE_EVERY=180      # интервал BLE сканирования (сек)
LAN_EVERY=300      # интервал LAN sweep (сек)
BLE_SCAN_SEC=8     # длительность одного BLE сканирования
LOG_MAX_DAYS=3     # удалять записи старше N дней
```

---

## Автозапуск

Скрипт `S98netwatch` (защита Wi-Fi от разрывов) патчен и хранится в `/bkupgrade/etc/init.d/S98netwatch`. При каждой загрузке станции он запускает сенсор.

Если после обновления прошивки патч слетит — восстановить:
```bash
scp alice-sensor/patches/S98netwatch root@STATION:/bkupgrade/etc/init.d/
```

Также `cron @reboot` в `/etc/cron.d/alice-sensor` — работает если демон cron запущен (`/usr/sbin/crond`).

---

## Что было сделано — подробный рассказ

### День 1: Доступ

Яндекс.Станция Лайт поставляется с root-доступом по SSH. Ключ авторизации лежит в `/etc/dropbear/`. По SSH доступен port 2222. Пользователь `root`, пароля нет.

```
ssh -p 2222 root@192.168.1.55
```

### День 2: Ядро и драйвер

Ядро Linux 4.9.68, скомпилировано для aarch64 (ARM64). Драйвер Wi-Fi — `sprdwl_ng` (Unisoc/Marlin3). Это проприетарный модуль без исходников.

Через загрузку собственных kernel modules мы получили доступ к внутренностям драйвера:
- `alice-tools.ko` —-symbol resolution через `kallsyms_lookup_name()`
- Найден `g_sprdwl_priv` — указатель на главную структуру `sprdwl_priv`
- Найдены функции `sprdwl_cmd_send_recv`, `sprdwl_sync_version`, `__sprdwl_cmd_getbuf`

### День 3: Firmware command channel

Прошивка `wcnmodem.bin` — проприетарная, лежит в `/lib/firmware/`. Через kernel module мы научились слать команды напрямую в прошивку и читать ответы.

```
# GET_INFO — работает, вернул MAC и версию прошивки
echo "cmd=1" > /sys/kernel/debug/alice/fwcmd

# SNIFFER_MODE_SWITCH (id 85) — заблокирована драйвером
echo "cmd=85 data=00 raw=1" > /sys/kernel/debug/alice/fwcmd
```

Драйвер блокирует команды 85/87 через `sprdwl_api_available_check()` — проверяет таблицу версий. Мы нашли обход (`sprdwl_cmd_send_to_ic`), но без знания формата payload команда вешает Wi-Fi. Пришлось ограничиться мониторингом через `iw scan` и `bluetoothctl`.

### День 4–5: Сенсор

Собран демон на shell (BusyBox, no Python на станции):
- `iw dev wlan0 scan` — 90-секундный цикл, записывает все AP
- `bluetoothctl LE scan` — 3-минутный цикл, записывает BLE-устройства
- `ping` + ARP sweep — 5-минутный цикл, записывает хосты в сети
- Детекция новых устройств — при первом появлении пишется в `events.log`
- Автоочистка — записи старше 3 дней удаляются

Всё хранится в `/bkupgrade/` — это **UBIFS**, а не tmpfs, то есть **данные не теряются при перезагрузке**.

### День 6: Автозапуск

Станция обновляет прошивку и перезагружается автоматически. Чтобы сенсор переживал reboot:
- `S98netwatch` патчен — запускает `sensor.sh daemon` при каждой загрузке
- Патч лежит в `/bkupgrade/etc/init.d/S98netwatch` (persistent storage)

---

## Известные ограничения

| Проблема | Причина | Статус |
|---|---|---|
| Monitor mode не работает | Формат payload для команд 85/87 неизвестен | 🔴 Не решено |
| BLE RSSI иногда = -127 | Фильтруются кадры с невалидным RSSI | 🟡 Фильтруются |
| USB-порта нет | Физически отсутствует на плате | 🟢 Обойдено |

---

## Hardware hacking: что ещё можно сделать

1. **Monitor mode** — найти payload для `SNIFFER_MODE_SWITCH` (85) и `SNIFFER_TRIGGER` (87) в disassembly прошивки. Ожидаемый результат: радиокадры 802.11 с radiotap header
2. **Promiscuous mode** — проверить `iw dev wlan0 set monitor otherbss`
3. **USB-Ethernet** — USB-OTG на станции поддерживает Ethernet-адаптеры (`asix`, `r8152` модули уже загружены). Адаптер даст проводной SSH-канал, независимый от Wi-Fi
4. **Wake-on-LAN** — разбудить устройства в сети для обнаружения
5. **Telegram-бот** — отправлять уведомления о новых устройствах

---

## Сборка kernel module

```bash
# В Docker (alpine/debian с aarch64-linux-gnu-gcc):
docker run --rm -v $(pwd):/out alpine:latest sh -c \
  'apk add --no-cache make gcc musl-dev linux-headers && \
  cd /out/kmod/alice-tools && make'
```

Требует ядро 4.9.68 и device tree для aarch64. Kernel headers в `kmod/linux-4.9.337/`.

---

## Лицензия

MIT. Проприетарные файлы (`wcnmodem.bin`, `sprdwl_ng.ko`) **не включены** — они принадлежат Yandex и Unisoc.

---

## Автор

@m1lkch4n — реверс-инжиниринг, 2026.
