AlertMap для GeekMagic SmallTV (ESP8266)

Сборка:
1. VS Code + расширение PlatformIO -> File > Open Folder -> эта папка (alertmap)
2. Кнопка "Build" (или в терминале: pio run)
3. Готовый файл: .pio/build/smalltv/firmware.bin
4. Загрузить: http://IP-часов/update (страница родной прошивки GeekMagic)
   Дальше обновления - через http://IP-часов/update уже этой прошивки.

Первый запуск: если Wi-Fi не задан, часы поднимут сеть AlertMap-Setup,
настройки на http://192.168.4.1
