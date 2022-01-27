**[Оглавление](index)	>	Платные возможности**

Платные возможности доступны только когда приложение установлено [через Google Play](https://play.google.com/store/apps/details?id=com.emanuelef.remote_capture). Если Вы пользователь F-Droid и у вас есть какие-либо предложения про возможность приема платежей по всему миру через сборки для F-Droid, пожалуйста [свяжитесь со мной по e-mail](mailto:black.silver@hotmail.it?subject=PCAPdroid)

## 5.1 Обнаружение вредоносных соединений

**Дисклеймер**: *Функционал обнаружения вредоносных соединений в PCAPdroid не является комплесным решением для обеспечения безопасности устройства. Автор не предоставляет никаких гарантий относительно возможностей обнаружения вредоносных соединений или их точности в PCAPdroid, и не несет ответственности за любой прямой или косвенный ущерб причиненный использованием данной возможности.*

Функция обнаружения вредоносов позволяет PCAPdroid обнаруживать соединения с вредоносными хостами благодаря сторонним черным спискам. Обнаружение активно только во время захвата трафика. Начиная с версии **1.4.5** при работе в стандартном (VPN) режиме PCAPdroid так же будет блокировать весь траффик направляемый на вредоносные хосты и получаемый с них. Блокировка *не производится* при работе [в root режиме](advanced_features#44-захват-траффика-с-правами-root).

Сегодня перед нашими устройствами стоит множество угроз: фишинг, онлайн мошенничество, вымогатели и слежка, а так же множество других. Когда заходит вопрос о безопасности, осторожности никогда не бывает достаточно и ни одно решение не покроет все возможные случаи. Возможность обнаружения вредоносов в PCAPdroid может помочь обнаруживать вредоносные соединения по мере появления таковых, тем самым обращая внимание пользователя на возможную угрозу, при этом предотвращая проблемы посредством блокировки соединения.

Некоторые случаи в которых эта возможность применима:

- Пользователь посещает известный опасный сайт (например фишинговый или мошеннический)
- Пользователь устанавливает некое приложение или дополнение (которое оказывается шпионит) которое соединяется с опасным доменом или IP адресом
- На устройство любым образом попадает вредонос (например шпион, вымогатель или ботнет), который соединяется с опасным доменом или IP адресом

Когда PCAPdroid обнаруживает вредоносное соединение, об этом сообщается пользователю через уведомления.

<p align="center">
<img src="images/malware-notification.png" width="250" />
</p>

Вы можете вручную проверить работает ли обнаружение, посетив веб-сайт [www.internetbadguys.com](http://www.internetbadguys.com). В уведомленнии будет указано какое приложение установило соединение и адрес (IP или домен) который был обнаружен как вредоносный. Нажав на уведомление можно увидеть список всех вредоносных соединений, сделанных приложением.

<p align="center">
<img src="images/malicious-connections.png" width="250" />
</p>

Чтобы увидеть все соединения, которые обнаружены как вредоносные, можно применить соответствующий фильтр ("Вредоносные соединения") из диалога выбора всех фильтров. **Важно:** Информация об обнаруженных вредоносных соединениях доступна только при активном захвате, при остановке захвата траффика эта информация теряется. Когда соединение блокируется, значок "блокировки" отображается вместе с пометкой о вредоносности.

<p align="center">
<img src="images/malware-blocked.png" width="300" />
</p>

В данном случае PCAPdroid заблокировал коммуникацию с вредоносным сайтом.

PCAPdroid предоставляет некоторую статистику касательно обнаружения вредоносных соединений, которая доступна в разделе "Обнаружение вредоносов" в боковом меню слева. Вкладка "Состояние" отображает данные касательно текущего захвата траффика.

<p align="center">
<img src="images/malware-detection-status.png" width="250" />
</p>

Зеленая галочка означает, что в текущем сеансе захвата траффика не было обнаружено вредоносных соединений. При обнаружении вредоносного траффика она заменяется красным значком опасности.
Несколько карточек под значком содержат информацию связанную с обнаружением вредоносов:

- Карточка "Обновлено черных списков" сообщает количество успешно обновленных списков из общего количества. Для поддержания списков в актуальном состоянии и уверенности в возможности предотвратить актуальные угрозы, PCAPdroid обновляет их раз в день. Если с загрузкой возникают проблемы - их можно обновить вручную с вкладки "Черный список".
- Карточка "Последнее обновление" время последнего обновления списков.
- Карточки "Правил по IP" и "Правил по доменам" отображает сколько всего уникальных правил на текущий момент используется для обнаружения вредоносных соединений.

Вкладка "Черный список" отображает статус каждого списка, используемого приложением.

<p align="center">
<img src="images/blacklists-status.png" width="250" />
</p>

Черные списки используемые в PCAPdroid содержат список доменов и IP адресов с плохой репутацией. Они включают в себя сканнеры, брутфорсеров и известные источники вредоносного ПО. Списки обычно содержат статические правила, основанные на информации о заражениях в прошлом и некоторые динамические правила, обычно добытые с помощью "приманок".

У каждого черного списка указано сколько уникальных правил он содержит. Дубликаты учитываюся только один раз, поэтому некоторые списки могут отображать 0 правил, если они являются частью другого списка. "Последнее обновление" отображает время последнего обновления данного списка. Если нажать на заголовок списка, вы откроете в браузере адрес, с которого этот список берется и сможете вручную просмотреть его содержимое. Нажатие кнопки обновления в верхней панели позволяет вручную обновить все списки.

В свою очередь вкладка "Белый список" отображает созданные вручную правила. Если у вас случается ложное срабатываение, вы через долгое удержание вредоносного соединения можете добавить в белый список IP адрес, домен или даже приложение в целом.