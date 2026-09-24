# SmartLada — текст страницы

Весь текст `site/public/index.html` блоками EN/RU. Правь здесь (или прямо в HTML) — перенесу.
Метка `[#id]` = секция в HTML (`<section id="...">`). Буква «е» вместо «ё» — везде.

## `<head>` (SEO)

- title EN: SmartLada — a VAZ-2106 tail light as a smart lamp
- title RU: SmartLada — задний фонарь ВАЗ-2106 как умный светильник
- description EN: A personal project: a VAZ-2106 tail light with four 12 V car bulbs, a custom ESP32-C6 board, USB-C power and Zigbee control from Yandex Alice.
- description RU: Личный проект: задний фонарь ВАЗ-2106 с четырьмя автомобильными лампами 12 В, своей платой на ESP32-C6, питанием по USB-C и управлением по Zigbee из Алисы.

## [#top] Hero

- h1 EN: a VAZ-2106 tail light, now a smart lamp
- h1 RU: задний фонарь ВАЗ-2106 как умный светильник
- EN: A stock VAZ-2106 tail light with four 12 V car bulbs. A custom board inside dims each bulb on its own. Powered from a USB-C charger, set up from its own menu, controlled from Yandex Alice over Zigbee.
- RU: Штатный задний фонарь ВАЗ-2106 с четырьмя автомобильными лампами 12 В. Внутри своя плата: каждая лампа регулируется отдельно. Питание от USB-C зарядки, настройка через меню на устройстве, управление из Алисы по Zigbee.
- Теги (без перевода): VAZ-2106 · ESP32-C6 · Zigbee · Wi-Fi · USB-C PD

## [#idea] Идея (фото R0015175)

- h2 EN: a car part as a room light
- h2 RU: автомобильная деталь как домашний свет
- EN: The tail light has four sections: turn signal, marker, reverse, brake. Here each one is a separate channel. The light can be a single steady glow, one section on its own, or a slow animation.
- RU: В фонаре четыре секции: поворот, габарит, задний ход, стоп. Здесь каждая — отдельный канал. Можно оставить ровный свет, одну секцию или медленную анимацию.
- EN: The board fits inside the housing. The bulbs are ordinary incandescent car bulbs, so the light is warm and fades in and out rather than switching.
- RU: Плата помещается внутри корпуса. Лампы — обычные автомобильные лампы накаливания: свет теплый, включается и гаснет плавно, а не щелчком.

## [#light] Свет (фото R0015170, макро)

- h2 EN: four bulbs, four channels
- h2 RU: четыре лампы, четыре канала
- EN: Each bulb has its own PWM channel. Per bulb: brightness, minimum and maximum level, gamma correction and a soft start.
- RU: У каждой лампы свой ШИМ-канал. Для каждой настраиваются яркость, минимальный и максимальный уровень, гамма-коррекция и плавный старт.
- Список EN / RU:
  - Static — fixed brightness per bulb / Static — постоянная яркость каждой лампы
  - Breathe — a slow pulse / Breathe — медленная пульсация
  - Turn — the turn signal blinking / Turn — мигает поворотник
  - Chase — bulbs light one after another, overlapping / Chase — лампы зажигаются по очереди, с наложением
  - Fade — a smooth crossfade around all four / Fade — плавный перелив по кругу
  - Drive — a random but plausible drive: cruising, braking, turns, reverse, hazard lights / Drive — случайная, но правдоподобная поездка: движение, торможение, повороты, задний ход, аварийка

## [#alice] Алиса (скриншот приложения)

- h2 EN: five devices in Alice
- h2 RU: пять устройств в Алисе
- EN: The light joins a Yandex Station over Zigbee and shows up in the app as five devices: the four bulbs and "Fara". Bulbs switch and dim one by one or as a group. "Fara" runs the effects: the colour picked in the app selects the effect, the brightness slider sets its brightness.
- RU: Фонарь подключается к Яндекс Станции по Zigbee и появляется в приложении как пять устройств: четыре лампы и «Фара». Лампы включаются и регулируются по одной или группой. «Фара» управляет эффектами: цвет в приложении выбирает эффект, яркость задает его яркость.
- EN: The on-device menu and Alice change the same state; the last command wins.
- RU: Меню на устройстве и Алиса меняют одно и то же состояние; действует последняя команда.
- Подпись к скриншоту EN: The Alice app: four bulbs and the "Fara" group.
- Подпись RU: Приложение Алисы: четыре лампы и группа «Фара».

## [#wifi] Без умного дома

- h2 EN: without a smart home
- h2 RU: без умного дома
- EN: Without a smart home, the light switches to Wi-Fi. It starts its own access point and serves a web page with the same settings as the menu. The QR code to join is on the screen.
- RU: Без умного дома фонарь переключается на Wi-Fi: поднимает свою точку доступа и открывает веб-страницу с теми же настройками, что и в меню. QR-код для подключения — на экране.
- EN: The ESP32-C6 has one radio, so it runs either Zigbee or Wi-Fi. The switch is in the menu. Firmware updates go over Wi-Fi or Bluetooth, without a cable.
- RU: Радио в ESP32-C6 одно, поэтому работает либо Zigbee, либо Wi-Fi; переключение — в меню. Прошивка обновляется по Wi-Fi или Bluetooth, без кабеля.

## [#menu] Меню

- h2 EN: the menu on the device
- h2 RU: меню на устройстве
- EN: An OLED display, 128×64, and four buttons. All screens below are captured from the firmware: the board streams its frame buffer over USB and a script walks the menu. These are not mock-ups.
- RU: OLED-дисплей 128×64 и четыре кнопки. Все экраны ниже сняты с прошивки: плата отдает кадровый буфер по USB, скрипт проходит по меню. Это не макеты.
- Подписи к анимациям: main menu / главное меню · effect select / выбор эффекта
- Подписи к экранам (EN / RU):
  - splash / заставка
  - idle — main screen / главный экран
  - brightness — per bulb and master / яркость ламп и общая
  - lamp setup — gamma, soft start, levels, PWM frequency / настройка ламп: гамма, плавный старт, уровни, частота ШИМ
  - settings / настройки
  - display — brightness and timeouts / дисплей: яркость и таймауты
  - zigbee / zigbee
  - wifi / wifi

### Другой дисплей (фото R0015184)

- h3 EN: another display option
- h3 RU: другой вариант дисплея
- EN: A version with a different display module: a 2.4" colour TFT and a rotary encoder instead of the OLED and buttons. Same firmware, same menu.
- RU: Вариант исполнения с другим дисплейным модулем: цветной TFT 2,4" и энкодер вместо OLED и кнопок. Прошивка и меню те же.

## [#board] Плата (фото R0015176 + рендеры KiCad)

- h2 EN: the board
- h2 RU: плата
- EN: A custom two-layer board, designed in KiCad. Rev C is the third revision; the first two tested the circuit and the layout.
- RU: Своя двухслойная плата, спроектирована в KiCad. Rev C — третья ревизия; первые две проверяли схему и разводку.
- Подпись к рендерам EN: KiCad render, top and bottom. The bottom silkscreen is the tail light's exploded view from the VAZ-2106 parts catalogue.
- Подпись RU: Рендер KiCad, верх и низ. На нижней шелкографии — схема фонаря из каталога запчастей ВАЗ-2106.

### Характеристики (EN / RU)

| EN | RU | значение EN | значение RU |
|---|---|---|---|
| Bulbs | Лампы | 4 car bulbs, 12 V, 5 W | 4 автомобильные лампы, 12 В, 5 Вт |
| Power | Питание | USB-C Power Delivery, 30 W charger or more | USB-C Power Delivery, блок питания от 30 Вт |
| Controller | Контроллер | ESP32-C6 | ESP32-C6 |
| Control | Управление | Zigbee (Yandex Alice) or Wi-Fi and a web page, without a smart home | Zigbee (Алиса) или Wi-Fi и веб-страница, без умного дома |
| On device | На устройстве | OLED 128×64, 4 buttons | OLED 128×64, 4 кнопки |
| Updates | Обновление | Over Wi-Fi or Bluetooth | По Wi-Fi или Bluetooth |
| Board | Плата | Custom, Rev C, 2 layers, 100×59 mm | Своя, Rev C, 2 слоя, 100×59 мм |

## [#links] Ссылки

- h2 EN: source / RU: исходники
- EN: Schematics, board and firmware are on GitHub.
- RU: Схема, плата и прошивка — на GitHub.
- EN: Another project: cineink, an e-ink display for the film set.
- RU: Другой проект: cineink, e-ink дисплей для съемочной площадки.

## Футер (bio, из cineink)

- EN: A personal project by / RU: Личный проект
- Alexander Ponomarev / Александр Пономарев
- Director of Photography / Оператор-постановщик
- EN: Alexander Ponomarev is a Moscow-based director of photography working worldwide since 2014, when he also co-founded LAM production studio with Yaroslav Dementiev; he works primarily in feature film and series, with a parallel body of work in commercials and music videos.
- RU: Александр Пономарев — оператор-постановщик из Москвы, работает по миру с 2014 года, тогда же вместе с Ярославом Дементьевым основал продакшен LAM; снимает прежде всего в полнометражном кино и сериалах, параллельно — в рекламе и клипах.
- EN: He shot Sonya Raizman's *Pictures of Friendly Ties*, winner of two main awards at Mayak Festival 2025; alongside cinematography, he also takes on projects as a director-DP and composer.
- RU: Снял дебютный фильм Сони Райзман «Картины дружеских связей» — две главных награды фестиваля «Маяк» 2025; помимо операторской работы выступает на проектах как режиссер-оператор (dir/dp) и композитор.
- Ссылки: Instagram · Website · Telegram
