# Сrawler
Поисковой робот, написанный с использованием библиотеки [libevent]

Если нужен DOM parser советую использовать [Gumbo]
### build
С ключем `-lpthread -levent_pthreads -levent`

#### P.S.
Как выяснилось библиотека libevent не очень производительная, потому что является комбайном с узкими местами реализации.

Существуют и другие библиотеки например libev и libuv, которые по бенчмаркам её превосходят, а для парсинга HTTP заголовоков рекомендую ознакомится с [PicoHTTPParser] и [qrintf]

[libevent]: http://libevent.org
[Gumbo]: https://github.com/google/gumbo-parser
[PicoHTTPParser]: https://github.com/h2o/picohttpparser
[qrintf]: https://github.com/h2o/qrintf
