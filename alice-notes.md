# Колонка: факты о железе (снято 2026-11-18, root)

## Доступ
- IP `192.168.1.55` (в DHCP видна как «Intertech Services AG», без имени)
- SSH: `ssh -p 2222 root@192.168.1.55`, вход по ключу, без пароля
- `scp` не работает (нет sftp-server) — файлы лить через `ssh 'cat > файл'`
- adb на 5555 не поднят, и он не нужен: есть root-shell

## Железо
- `Yandex Station Micro v1.2`, hostname `yandex-station-lite`
- SoC Amlogic + Wi-Fi/BT комбо Unisoc **UWE5621** (`sprdwl_ng`, `sprdbt_tty`)
- CPU: 2 x Cortex-A53 (0xd03), RAM 238 МБ, swap 128 МБ
- Kernel `4.9.68 #1 SMP PREEMPT aarch64`, собран Linaro GCC 6.3.1 (2026-10-13)
- **vermagic = `4.9.y SMP preempt mod_unload aarch64`**
- Buildroot 2020.02.1, не Android

## Разделы
- `/` — ubi0:rootfs, **ro**, 64.8M, свободно 12.2M
- `/bkupgrade` — ubi1:rootfs, **rw**, 87.5M, свободно 50.7M
- `/opt` -> `/bkupgrade/data/opt/entware` (Entware, rw, переживает ребут)
- `/tmp` — tmpfs 119M, всё пропадает после ребута

## Гейты для стороннего модуля ядра — все пройдены
| Проверка | Значение | Вывод |
|---|---|---|
| `/proc/sys/kernel/modules_disabled` | `0` | insmod разрешён |
| `CONFIG_MODULES` | `y` | модули поддерживаются |
| `CONFIG_MODVERSIONS` | **не задан** | CRC символов не проверяются |
| `CONFIG_MODULE_SIG` / `SIG_FORCE` | **не заданы** | подпись не требуется |
| `CONFIG_MODULE_FORCE_LOAD` | не задан | — |
| `CONFIG_MAC80211` / `CFG80211` | `y` (=y, вкомпилированы) | экспортируют символы (`ieee80211_register_hw` и 671 символ `ieee80211*`) |
| USB host | `CONFIG_USB_XHCI_HCD=y`, `USB_DWC3_HOST=y`, `udc` пуст | **настоящий host**, не gadget |
| USB на практике | хаб `1a40:0101` + флешка `090c:1000` + RTL8152 `0bda:8152`, флешка смонтирована в `/tmp/usb/Ventoy` | порт живой и питает устройства |

## Wi-Fi (встроенный)
- `iw dev`: `wlan0`, managed, `b8:87:6e:7f:dc:80`, SSID Padavan_2.4GHz, -15 dBm, 108 Мбит/с
- Режимы: IBSS, managed, **AP**, P2P-client, P2P-GO, P2P-device
- **monitor отсутствует** — `sprdwl_ng` не умеет, перехват на встроенном чипе закрыт
- `managed` и `AP` взаимоисключающие (`#{ managed, AP } <= 1`)
- Диапазоны: 2.4 ГГц (1-14) и 5 ГГц (36-165), 20 dBm

## Bluetooth (встроенный)
- `hci0`, UART, Spreadtrum, **BT 5.0**, имя `Колонка`, BD_ADDR `B8:87:6E:7F:DC:81`
- BlueZ 5.70: `bluetoothctl`, `hciconfig`, `hcitool`, `btmon`, `btmgmt`
- BLE-скан работает: уже видно чужие устройства с RSSI и manufacturer data
- `bluealsa` в `/tmp` — звук по BT идёт через него

## Что стоит на устройстве
- `/opt/bin/iw` — **iw 6.9, статический, 1.7 МБ** (собран в `C:\Temp\iwbuild`, залит 2026-11-18)
- `/opt/bin/tcpdump` (Entware, динамический), `wget`, `opkg`, `bzip2`, `xz`, GNU grep/find/tar
- Нет: python3, sqlite3, nc, curl, hostapd, monitor-режима
- `/opt/bin` нет в PATH у non-login shell — звать по полному пути или `export PATH=/opt/bin:$PATH`

## Сниффер в прошивке (важное)

В `wcnmodem.bin` есть строки-имена команд firmware (идут в порядке enum):

```
... WIFI_CMD_SET_WOWLAN
    WIFI_CMD_SNIFFER_MODE_SWITCH
    WIFI_CMD_SNIFFER_TRIGGER
    WIFI_CMD_MAX
```

Сопоставление с enum драйвера (`unisocwifi/cmdevt.h`):

| Команда | FW | Драйвер |
|---|---|---|
| `WIFI_CMD_VOWIFI_DATA_PROTECT` | 80 | 80 |
| `WIFI_CMD_REPEATER_ROUTE_LIST` | 81 | нет |
| `WIFI_CMD_SET_WFD_FLAG` | 82 | нет |
| `WIFI_CMD_SET_WOWLAN` | 83 | 83 |
| **`WIFI_CMD_SNIFFER_MODE_SWITCH`** | **84** | нет (драйвер трактует 84 как `PACKET_OFFLOAD`) |
| **`WIFI_CMD_SNIFFER_TRIGGER`** | **85** | нет |

Вывод: **сниффер в железе есть, в драйвере не реализован.** Публичные исходники
(uwe5621ds-aml, sprdwl_ng, uwe5622) тоже не содержат ни `sniffer`, ни
`IFTYPE_MONITOR`, ни `radiotap`.

Debugfs на устройстве: `/sys/kernel/debug/sprdwl_debug/{log_level,txrx_dbg}` —
интерфейса инъекции команд нет.

Путь «без свистка» = пересобрать `sprdwl_ng.ko` из исходников, добавить
`IFTYPE_MONITOR`, отправить команду 84, принять кадры и собрать radiotap.
Payload команды и формат доставки кадров неизвестны — реверс.

## Инструмент alice-tools (в работе)

Модуль `alice-tools.ko` собран, грузится, символы драйвера находятся через
`kallsyms_lookup_name` (`__sprdwl_cmd_getbuf`, `sprdwl_cmd_send_recv`,
`sprdwl_sync_version`). Debugfs: `/sys/kernel/debug/alice/{priv,syncver,fwcmd}`.

**Важно: заводской драйвер собран из другого дерева.** По дизассемблеру
`/lib/modules/4.9.68/kernel/uwe5621/sprdwl_ng.ko`:

| Что | Значение |
|---|---|
| `wiphy_new_nm(&ops, privsize)` | `privsize = 0x1a70` |
| `priv` | `wiphy + 0x480` (≠ смещение из наших заголовков) |
| `priv->wiphy` | `priv + 0` (самоссылка) |
| `priv->hw_priv` | `priv + 184` |
| `priv->hw_offset` | `priv + 192` |
| `sprdwl_cmd_send_recv` | `(priv, msg, timeout, rbuf, rlen)` — совпадает |
| `struct sprdwl_msg_buf` | `data` по `+24`, `tran_data` по `+32`, `len` по `+42` |

Поэтому `wiphy_priv()` использовать нельзя. В модуле сделан поиск `priv`
самопроверкой: скан области wiphy на слово, равное самому wiphy, с проверкой
`hw_priv`/`hw_offset`. Раскладка структур при этом не нужна.

**Инцидент 2026-11-18:** первый вызов `sync_version` упал в Oops
(`__sprdwl_cmd_getbuf+0x28`, адрес `0x1718`, `priv->hw_priv = 0`) — ожидаемо,
это была проверка старого (неверного) способа получить `priv`. Wi-Fi выжил.
После этого `rmmod alice_tools` завис в состоянии `Unloading` (refcount -1),
а `reboot` не завершился — колонка ушла в неответ. Лечение: физическое
передёргивание питания. Вывод: после Oops перезагрузка не гарантирована,
`reboot` при зависшем модуле не спасает.

**Инцидент 2026-11-18 (второй):** команда `cmd=62 data=00 raw=1` (обход API-гейта
через `sprdwl_cmd_send_to_ic`) подвесила Wi-Fi: SSH-сессия зависла на записи в
fwcmd, колонка пропала из сети (в ARP роутера её нет). Коробка при этом жива.
Вывод: отправка неизвестной команды в прошивку блокирует командный путь
драйвера, а вместе с ним и весь Wi-Fi. Нужен независимый канал управления
(USB-Ethernet — в ядре уже загружены `asix`, `ax88179_178a`, `r8152`, `cdc_ncm`).

**Нумерация команд:** таблица имён в `wcnmodem.bin` — это enum прошивки,
64 записи (1..63, `WIFI_CMD_MAX`=64). `SNIFFER_MODE_SWITCH` = **62**,
`SNIFFER_TRIGGER` = 63 (не 84/85 — те из более полного enum открытого драйвера).
`GET_INFO`=1 совпадает; `send[WIFI_CMD_...]` в логе драйвера подтверждает.

**API-гейт драйвера:** `sprdwl_cmd_send_recv` вызывает `sprdwl_api_available_check`,
и для id, которого нет в таблице версий, пишет
`CMD ID:62,drv ver:1, fw ver:0 drop it!!` и возвращает -1. Обход — прямой
`sprdwl_cmd_send_to_ic` (есть в kallsyms), но именно он и подвесил Wi-Fi.

## План «монитор без свистка»

**Smoke-тест пройден (2026-11-18):** `hello.ko`, собранный под `4.9.y SMP preempt
mod_unload aarch64`, загрузился и выгрузился без ошибок:
`alice-hello: module loaded, vermagic check passed` → `module unloaded`.
Значит свои модули в это ядро грузить можно.

Важные символы драйвера (есть в `/proc/kallsyms`, доступны через `kallsyms_lookup_name`):
`sprdwl_send_cmd`, `sprdwl_cmd_send_to_ic`, `sprdwl_alloc_msg_buf`,
`sprdwl_free_msg_buf`, `sprdwl_sync_version`, `sprdwl_rx_process`, `sprdwl_rx_work_queue`.

`priv` достаётся чисто, без знания layout: `dev_get_by_name("wlan0")` →
`dev->ieee80211_ptr` → `wdev->wiphy` → `wiphy_priv(wiphy)` (драйвер делает
`wiphy_new(&ops, sizeof(*priv))`, проверено в исходнике).


Согласовано с владельцем: колонка доступна физически, питание можно передёрнуть.

Порядок (каждый шаг проверяем отдельно):

1. **Smoke-тест модулей.** Собрать `hello.ko` под `.config` с самой колонки, вермagика
   `4.9.y SMP preempt mod_unload aarch64`, `insmod` — рабочий Wi-Fi не трогаем.
2. **Пересборка драйвера.** `unisocwifi` + `unisocwcn` из krypton/uwe5621ds-aml,
   собрать `sprdwl_ng.ko`, сравнить размер/строки с заводским, загрузить **вместо**
   заводского только по схеме с откатом.
3. **Откат.** Скрипт-сторож: `rmmod sprdwl_ng uwe5621_bsp_sdio`, `insmod` новый,
   ждём 15 с появления `wlan0` и линка; если нет — `insmod` заводского, `reboot`.
   Запускать через `setsid`, чтобы пережил обрыв SSH.
4. **Эксперимент со сниффером.** Патч драйвера:
   - `WIFI_CMD_SNIFFER_MODE_SWITCH = 84`, `WIFI_CMD_SNIFFER_TRIGGER = 85`;
   - модульные параметры `sniff_hex` (payload в hex) и `sniff_channel`, чтобы
     перебирать формат **без пересборки**;
   - логировать в dmesg все RX-кадры с `SPRDWL_TYPE_DATA_SPECIAL` и
     `SPRDWL_TYPE_PKT_LOG` — если прошивка начнёт сыпать кадры, увидим их там;
   - добавить `BIT(NL80211_IFTYPE_MONITOR)` и case в `add_vif` только после того,
     как поймём, что прошивка действительно отдаёт кадры.
5. **radiotap** — собрать из RX-дескриптора (RSSI/канал/rate), отдать в `mon0`.

Ключевой риск шага 4: payload команды 84 неизвестен, неверный может уронить
прошивку Wi-Fi до перезагрузки. Поэтому параметры — из userspace, а не хардкод.

## Итог по перехвату
Встроенный Wi-Fi монитором быть не может. Но USB-host + загружаемые модули без подписи и без MODVERSIONS
означают: **USB-свисток с mainline-драйвером mac80211 заведётся**, нужен `.ko` с vermagic
`4.9.y SMP preempt mod_unload aarch64`.

Кандидаты (mainline, monitor + injection):
- **RT5370** (`rt2800usb`) — только 2.4 ГГц, самый дешёвый и совместимый
- **AR9271** (`ath9k_htc`) — 2.4 ГГц, классика для aircrack
- **MT7612U** (`mt76x2u`) — 2.4+5 ГГц, AC

Не брать RTL8812AU/8821AU — драйвер вне mainline, сборка и ABI больнее.
