# RevCZigbee4EP — 4× DimmableLight на одном ESP32-C6 node

**Цель (gating-эксперимент).** Проверить, покажет ли «Дом с Алисой» **один** ESP32-C6 node
с **четырьмя** стандартными endpoint DimmableLight (EP10..13) как **четыре** независимые
лампы — каждая со своим вкл/выкл и своим диммером. Это вариант A против варианта B из
[`research/smartlada_alice_zigbee_multi_endpoint_summary.md`](../../research/smartlada_alice_zigbee_multi_endpoint_summary.md).
От результата зависит, жизнеспособен ли прямой Zigbee-путь до переноса в продуктовую
прошивку `SmartLadaRevC`.

Результаты фиксируем в живом чеклисте:
[`research/zigbee_alice_test_results.md`](../../research/zigbee_alice_test_results.md).

## Стенд

Плата **RevA** + девборда **MuseLab nanoESP32-C6** (wuxx, N16 = 16 МБ флеш).
Пины каналов взяты по **Rev C** — чтобы маппинг совпадал с продуктом.

| Что | Пин | Примечание |
|-----|-----|-----------|
| Канал OUT0 | GPIO1 | EP10, low-side D4184, active-high (больше duty = ярче) |
| Канал OUT1 | GPIO0 | EP11 (OUT0/OUT1 перекрещены относительно Rev A) |
| Канал OUT2 | GPIO2 | EP12 |
| Канал OUT3 | GPIO3 | EP13 |
| NeoPixel-статус | GPIO8 | onboard WS2812 девборды |
| Кнопка BOOT | GPIO9 | active-low; удержание 3 c = factory reset + rejoin |
| USB | GPIO12/13 | native USB-Serial-JTAG, лог 115200, `CDCOnBoot=cdc` |

Индикация NeoPixel: **синий мигает** — ищет сеть; **зелёный** — присоединился;
**белый мигает** — identify («Алиса ищет устройство»); **красный** — factory reset.

Маппинг endpoint → лампа (назначается проводкой + именем в приложении):
`EP10 поворотник · EP11 стоп · EP12 габарит · EP13 задний ход`.

> **[CRITICAL]** С подключёнными лампами и 12 В: общая земля плата ↔ БП, предохранитель
> на месте. Каналы принудительно OUTPUT+LOW до любой инициализации.

## Сборка и заливка

Девборда N16, поэтому `FlashSize=16M`; готового `zigbee_16MB` в ядре нет — используем
`zigbee_8MB` (app-раздел 3.4 МБ, скетч занимает ~18 %).

Компиляция (проверено, собирается чисто):

```bash
arduino-cli compile -b "esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M" sketches/RevCZigbee4EP
```

Заливка (порт из `arduino-cli board list`, обычно `/dev/cu.usbmodemXXXX`):

```bash
arduino-cli upload -b "esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M" -p /dev/cu.usbmodemXXXX sketches/RevCZigbee4EP
```

Лог:

```bash
arduino-cli monitor -p /dev/cu.usbmodemXXXX -c baudrate=115200
```

В Arduino IDE эквивалент: Tools → Zigbee mode = **Zigbee ED (end device)**,
Partition Scheme = **Zigbee 8MB with spiffs**, USB CDC On Boot = **Enabled**,
Flash Size = **16MB**.

## Порядок спаривания

1. Залить скетч, открыть монитор порта — увидеть `Joining network...` (синее мигание).
2. «Дом с Алисой» → добавить устройство → Зигби-устройство (открыть сопряжение на Midi).
3. NeoPixel станет зелёным, в логе — `Joined Zigbee network`.
4. Пройти чеклист T1.1..T1.7 из
   [`research/zigbee_alice_test_results.md`](../../research/zigbee_alice_test_results.md):
   сколько появилось карточек, независимые ли имена / on-off / яркость, что понимает голос,
   переживает ли reboot/rejoin.

## Логи: что мы вытаскиваем (reverse engineering)

Опросить NLP Алисы по Zigbee нельзя. Но можно логировать **всё, что Алиса шлёт нам** —
и по этому восстановить, как она мапит фразы на ZCL. Два уровня:

**Уровень 1 — декодированный ZCL (всегда включён).** На каждую команду хаба две строки:

```text
[12345] EP11 SET Level(0x0008) attr=0x0000 type=0x20 size=1 val=76 raw=4c
[12345] EP11 ch1 APPLY ON level=76 -> duty=...
```

- `[12345]` — millis()-таймстамп, чтобы **соотнести с произнесённой фразой** (говоришь —
  смотришь время в логе).
- `EP11` — на какой endpoint пришло (ключ к вопросу multi-endpoint: адресует ли Алиса
  каналы раздельно).
- `SET Level(0x0008) attr=0x0000` — кластер и атрибут (тут — CurrentLevel).
- `type/size/val/raw` — ZCL-тип, размер, декодированное значение и сырые байты.

Так видно: какой шкалой Алиса шлёт яркость (0..254 vs 0..100), в каком порядке идут
On/Off и Level, шлёт ли transition, что делает при «включи» без яркости и т.п. Логируются
и кластеры, которые обычная лампа игнорит (Groups/Scenes/Identify) — полезно для группы «Фара».

**Уровень 2 — сырой APSDE-фаерхоз (по требованию, отдельная сборка).** `setDebugMode(true)`
уже в коде, но APSDE-логи (пофреймовые src/dst endpoint, cluster, profile, LQI) печатаются
только при сборке с core-debug. Собрать «capture»-версию:

```bash
arduino-cli compile -b "esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M,DebugLevel=verbose" sketches/RevCZigbee4EP
```

Даёт полный стек-трейс Zigbee (каждый фрейм, ZDO-сигналы, bind/unbind). Шумно —
включать только на сессию захвата непонятного поведения.

### Что прислать мне

Скинь из Arduino IDE лог со **штатной сборки** (Уровень 1), и рядом припиши **что говорил и
когда** («сказал „яркость поворотника 30“ на ~12.3s»). По таймстампам сопоставлю фразы с ZCL.
Если что-то ведёт себя странно (не адресует endpoint, игнорит команду) — тогда сними
Уровень 2 (verbose) на этот сценарий.

При старте после join печатается контекст сети:

```text
[..] NET short=0x1a2b pan=0x1234 channel=15
[NET] ieee (little-endian) = ...
```

## Переменная теста: DISTINCT_MODEL

Если Алиса **схлопнула** четыре endpoint в одну карточку (вариант B), это может быть из-за
одинакового `ModelIdentifier`. В `RevCZigbee4EP.ino` поставить `#define DISTINCT_MODEL 1`
(даст каждому endpoint своё имя модели `RevC-Lamp10..13`), перезалить, сделать factory
reset (BOOT 3 c) и спарить заново. Результат обеих попыток занести в чеклист (T1.1).

## Честные оговорки

- Zigbee-хаб Яндекса исторически придирчив к нестандартным устройствам. Профиль
  DimmableLight стандартный, шанс хороший, но multi-endpoint → отдельные entities —
  **эмпирический** вопрос, ровно ради него этот тест.
- Если не присоединяется вовсе — снять лог с `ZigbeeMode=ed_debug` + `DebugLevel=verbose`.
- Следующая фаза (цвет = селектор эффекта, ColorControl/HSV) — отдельным скетчем после
  того, как закрыт вопрос multi-endpoint.
