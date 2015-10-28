# Сrawler
Поисковой робот, написанный с использованием библиотеки [libevent]

### usage

Usage: crawler [KEY]... DOMAIN-LIST

	-t	таймауты подключения и операций чтения-записи
	-n	количество одновременных запросов (подбирайте сами по % ошибок)
	-o	домены в которых было найдено необохдимое совпадение
	-e	домены проверка которых завершилась с сетевой ошибкой
	-c	продолжить поиск по списку

В данной реализации список доменов должен представлять из себя CSV файл, где первым значением идет имя домена.

### build
С ключем `-lpthread -levent_pthreads -levent`

#### P.S.
Существуют и другие библиотеки более производительные библиотеки, например libev, libuv...
Для парсинга HTTP заголовоков рекомендую ознакомится с [PicoHTTPParser] и [qrintf].
Если нужен DOM parser советую использовать [Gumbo].

[libevent]: http://libevent.org
[Gumbo]: https://github.com/google/gumbo-parser
[PicoHTTPParser]: https://github.com/h2o/picohttpparser
[qrintf]: https://github.com/h2o/qrintf
