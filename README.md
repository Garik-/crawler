# Сrawler
Поисковой робот, написанный с использованием библиотеки [libevent]
```bash
Usage: crawler [KEY]... DOMAIN-LIST

	-t	таймауты подключения и операций чтения-записи
	-n	количество одновременных запросов (подбирайте сами по % ошибок)
	-o	файл доменов в которых было найдено необохдимое совпадение
	-e	файл доменов проверка которых завершилась с сетевой ошибкой
	-c	продолжить поиск по списку

Example:
$ crawler -n 1000 -o found.csv domains.csv
```

В данной реализации список доменов должен представлять из себя CSV файл, где первым значением идет имя домена.
```
c0dedgarik.blogspot.ru;
```

Если вы хотите запустить процесс, как демон не привязанный к терминалу:
```bash
$ setsid cmd >/dev/null 2>&1
```
##Installation
```bash
$ make
```
#### P.S.
Существуют и другие библиотеки более производительные библиотеки, например libev, libuv...
Для парсинга HTTP заголовоков рекомендую ознакомится с [PicoHTTPParser] и [qrintf].
Если нужен DOM parser советую использовать [Gumbo].

[libevent]: http://libevent.org
[Gumbo]: https://github.com/google/gumbo-parser
[PicoHTTPParser]: https://github.com/h2o/picohttpparser
[qrintf]: https://github.com/h2o/qrintf
