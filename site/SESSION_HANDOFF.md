# SmartLada — страница проекта: handoff (2026-09-24)

**Живая:** https://smartlada.saschapo.me (запущена 2026-09-24). Одностраничник, витрина проекта,
стиль cineink (темный минимализм), RU + EN. Чистый HTML/CSS, без сборки.

## Раскладка

| Путь | Что |
|---|---|
| `site/public/` | **ровно то, что лежит на сервере** в `/var/www/smartlada` |
| `site/public/index.html` | вся страница, CSS в `<style>`, JS только переключатель языка |
| `site/smartlada-text.md` | весь текст EN/RU по секциям `[#id]` — править здесь или прямо в HTML |
| `site/tools/export_images.py` | `uv run site/tools/export_images.py` → `public/img/` (фото, экраны, og, иконки) |
| `site/tools/bdf2woff2.py` | BDF → woff2 пиксель-в-пиксель (шрифт orp из `fonts/tecate/`) |
| `site/source/alice-app.jpg` | исходный скриншот приложения Алисы (экспорт режет статус-бар) |
| `site/screens/` | сырые кадры меню с прошивки (`frames.json`), см. `SmartLadaRevC/README.md` → Screen capture |
| `site/deploy.sh` | `chmod` + `rsync --delete` на сервер + пинг IndexNow; `--dry` — только показать |
| `site/deploy/nginx-smartlada.conf` | шаблон server-блока (до certbot) |

Превью: `.claude/launch.json` → `site` (`python3 -m http.server 4174 --directory site/public`).

## Секции (id в HTML)

`#top` hero (R0015169_1_16x9) · `#idea` (R0015175_cleanup) · `#light` 4 канала + эффекты
(R0015170) · `#alice` 5 устройств (скриншот приложения) · `#wifi` без умного дома ·
`#menu` экраны с прошивки (anim menu/mode x4 + 8 экранов x2) + вариант TFT (R0015184) ·
`#board` плата (R0015176) + характеристики + рендеры KiCad · `#links` GitHub, cineink · футер bio.

## Решения

- Текст сухой, без продажи; «личный проект» только в футере; «не продается» не писать; «е» вместо «ё».
- Заголовки — пиксельный orp (шрифт меню устройства). Размеры строго кратны 12px (сетка 6x12).
  **В orp Bold нет жирной кириллицы** (глифы = regular), поэтому в RU заголовки regular (400),
  в EN bold. Вариант с системным шрифтом: `?h=sys` в адресе (для сравнения, не для людей).
- Экраны меню только целым масштабом (`--sa`/`--sg`, `image-rendering: pixelated`), палитра f2f2f2 на черном.
- **Анимации меню — не анимированные картинки**, а `<canvas>` + PNG-лента кадров (`img/screens/<name>-strip.png`)
  + задержки с платы (`anims.json`). Причина: iOS замораживает GIF/WebP/APNG при выключенном
  «Автовоспроизведение анимированных изображений», а muted-autoplay видео не играет в энергосбережении.
  Отбор кадров общий с `tools/screens.py` (`anim_frames`).
- Фото из `custom_pcb/smartlada_revC/product photos v0/selected/` (в .gitignore, в git только экспорт).
  R0015166 не используется (R0015175 — тот же кадр крупнее).
- SEO как на saschapo.me: canonical, OG/twitter (og.jpg 1200x630), JSON-LD (Person `saschapo.me/#person`
  + WebSite + CreativeWork), `sitemap.xml`, `robots.txt`, `llms.txt`, `404.html` (noindex), favicons, manifest.

## Сервер

- vdsina, `ssh vps` (ключ в `~/.ssh/config`, root). nginx: `/etc/nginx/sites-available/smartlada`
  (+ symlink), certbot добавил 443 и редирект http→https. `saschapo.me` и `cineink.saschapo.me` не трогать.
- Настоящий 404, `charset utf-8`, кэш `img/` и `fonts/` 7 дней (имена без хэшей — после переэкспорта
  жесткое обновление).
- IndexNow-ключ `9894ba0138df12e13bbf102c25338e79.txt`. Первый пинг (до HTTPS) — 202, повтор после
  HTTPS — 403 (ключ был недоступен по https на момент первой проверки); `deploy.sh` пингует при каждом деплое.

## Открыто

- Описание GitHub-репо устарело («Калибровочный стенд PWM на ESP8266…») — предложено новое, менять после «да».
- Яндекс.Вебмастер / Google Search Console для поддомена не заведены.
