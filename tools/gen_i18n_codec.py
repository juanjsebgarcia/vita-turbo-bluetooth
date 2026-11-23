"""The 0.2.0 texts of the codec help page, the list-style hints of Codec Settings and the codec fallback notification, in the 20 system languages.

Imported by gen_i18n.py (`install`), which owns the table, the checks and the output. English is authoritative; the rest are machine translations
open to native-speaker fixes. Kept apart so the generator itself stays readable.

The hints of SBC Audio Quality, AAC Bit Rate, LDAC Bit Rate, aptX HD When Available and Hardware Accelerated AAC are one line per choice ("X = Y") and
one closing paragraph. `HINT_LINES` in gen_i18n.py says how the lines are joined: 'newline' (the default; a newline in a hint has not yet been seen
on a console) or 'semicolon' (one run of text, the lines separated by semicolons: the fallback if a console check shows the line breaks do not work).
"""
import re

CJK = ('ja', 'zhT', 'zhS')

# ---- the codec help page: the row, and six read-only lines each with a "?" ------------------------------------------------------------------
ROW_CODECHELP = {
    'ja': 'Bluetoothコーデックのヘルプ', 'en': 'Bluetooth Codec Help', 'fr': 'Aide du codec Bluetooth', 'es': 'Ayuda del códec Bluetooth',
    'de': 'Bluetooth-Codec-Hilfe', 'it': 'Guida al codec Bluetooth', 'nl': 'Bluetooth-codechulp', 'ptPT': 'Ajuda do codec Bluetooth',
    'ru': 'Справка по кодеку Bluetooth', 'ko': 'Bluetooth 코덱 도움말', 'zhT': 'Bluetooth 編解碼器說明', 'zhS': 'Bluetooth 编解码器说明',
    'fi': 'Bluetooth-koodekin ohje', 'sv': 'Bluetooth-codec-hjälp', 'da': 'Bluetooth-codec-hjælp', 'no': 'Bluetooth-kodekhjelp',
    'pl': 'Pomoc: kodek Bluetooth', 'ptBR': 'Ajuda do codec Bluetooth', 'tr': 'Bluetooth codec yardımı'}
HINT_CODECHELP = {
    'ja': 'コーデックを選ぶための簡単なガイドです。各行の「?」で詳細を確認できます。',
    'en': 'A short guide to choosing a codec. Each line has a ? with more detail.',
    'fr': 'Un court guide pour choisir un codec. Chaque ligne a un ? avec plus de détails.',
    'es': 'Una guía breve para elegir un códec. Cada línea tiene un ? con más detalle.',
    'de': 'Eine kurze Anleitung zur Codec-Wahl. Jede Zeile hat ein ? mit mehr Details.',
    'it': 'Una breve guida per scegliere un codec. Ogni riga ha un ? con più dettagli.',
    'nl': 'Een korte gids voor het kiezen van een codec. Elke regel heeft een ? met meer uitleg.',
    'ptPT': 'Um guia breve para escolher um codec. Cada linha tem um ? com mais detalhes.',
    'ru': 'Краткое руководство по выбору кодека. У каждой строки есть значок ? с подробностями.',
    'ko': '코덱을 고르는 데 도움이 되는 짧은 안내입니다. 각 줄의 ?에서 자세한 내용을 볼 수 있습니다.',
    'zhT': '挑選編解碼器的簡短指南。每一行的 ? 都有更多說明。',
    'zhS': '挑选编解码器的简短指南。每一行的 ? 都有更多说明。',
    'fi': 'Lyhyt opas koodekin valintaan. Jokaisella rivillä on ? lisätietoja varten.',
    'sv': 'En kort guide till att välja codec. Varje rad har ett ? med mer information.',
    'da': 'En kort guide til at vælge codec. Hver linje har et ? med flere detaljer.',
    'no': 'En kort veiledning for å velge kodek. Hver linje har et ? med mer informasjon.',
    'pl': 'Krótki przewodnik wyboru kodeka. Każdy wiersz ma znak ? z dodatkowymi szczegółami.',
    'ptBR': 'Um guia curto para escolher um codec. Cada linha tem um ? com mais detalhes.',
    'tr': 'Codec seçmek için kısa bir rehber. Her satırın ayrıntı için bir ? işareti vardır.'}

HELP_TITLE = [
    {   # 1
        'ja': 'まず、ヘッドホンのコーデックを確認', 'en': "Check your headphones' codecs first", 'fr': "D'abord, les codecs des écouteurs",
        'es': 'Primero, los códecs de tus auriculares', 'de': 'Erst die Kopfhörer-Codecs prüfen', 'it': 'Prima, controlla i codec delle cuffie',
        'nl': 'Eerst de codecs van je koptelefoon', 'ptPT': 'Primeiro, os codecs dos auscultadores', 'ru': 'Сначала проверьте кодеки наушников',
        'ko': '먼저 헤드폰의 코덱을 확인하세요', 'zhT': '先確認耳機支援的編解碼器', 'zhS': '先确认耳机支持的编解码器', 'fi': 'Tarkista ensin kuulokkeiden koodekit',
        'sv': 'Kolla först hörlurarnas codecs', 'da': 'Tjek først hovedtelefonernes codecs', 'no': 'Sjekk først kodekene til hodetelefonene',
        'pl': 'Najpierw sprawdź kodeki słuchawek', 'ptBR': 'Primeiro, os codecs dos fones de ouvido', 'tr': "Önce kulaklığınızın codec'lerine bakın"},
    {   # 2
        'ja': 'SBC：Vitaの標準', 'en': 'SBC: Vita default', 'fr': 'SBC : par défaut sur la Vita',
        'es': 'SBC: el predeterminado de la Vita', 'de': 'SBC: Vita-Standard', 'it': 'SBC: predefinito della Vita',
        'nl': 'SBC: Vita-standaard', 'ptPT': 'SBC: o predefinido da Vita', 'ru': 'SBC: по умолчанию на Vita',
        'ko': 'SBC: Vita 기본값', 'zhT': 'SBC：Vita 預設', 'zhS': 'SBC：Vita 默认', 'fi': 'SBC: Vitan oletus',
        'sv': 'SBC: Vitans standard', 'da': 'SBC: Vitas standard', 'no': 'SBC: Vitas standard',
        'pl': 'SBC: domyślny dla Vity', 'ptBR': 'SBC: padrão do Vita', 'tr': 'SBC: Vita varsayılanı'},
    {   # 3
        'ja': 'AAC：とても良い音質', 'en': 'AAC: very good quality', 'fr': 'AAC : très bonne qualité', 'es': 'AAC: muy buena calidad', 'de': 'AAC: sehr gute Qualität',
        'it': 'AAC: qualità molto buona', 'nl': 'AAC: zeer goede kwaliteit', 'ptPT': 'AAC: muito boa qualidade', 'ru': 'AAC: очень хорошее качество',
        'ko': 'AAC: 매우 좋은 음질', 'zhT': 'AAC：音質非常好', 'zhS': 'AAC：音质非常好', 'fi': 'AAC: erittäin hyvä laatu', 'sv': 'AAC: mycket bra kvalitet',
        'da': 'AAC: meget god kvalitet', 'no': 'AAC: svært god kvalitet', 'pl': 'AAC: bardzo dobra jakość', 'ptBR': 'AAC: qualidade muito boa', 'tr': 'AAC: çok iyi kalite'},
    {   # 4
        'ja': 'aptX™：遅延が最小', 'en': 'aptX™: lowest delay', 'fr': 'aptX™ : délai minimal',
        'es': 'aptX™: menor retardo', 'de': 'aptX™: geringste Verzögerung', 'it': 'aptX™: ritardo minimo',
        'nl': 'aptX™: minste vertraging', 'ptPT': 'aptX™: menor atraso', 'ru': 'aptX™: наименьшая задержка',
        'ko': 'aptX™: 지연 최소', 'zhT': 'aptX™：延遲最低', 'zhS': 'aptX™：延迟最低', 'fi': 'aptX™: pienin viive',
        'sv': 'aptX™: minst fördröjning', 'da': 'aptX™: mindst forsinkelse', 'no': 'aptX™: minst forsinkelse',
        'pl': 'aptX™: najmniejsze opóźnienie', 'ptBR': 'aptX™: menor atraso', 'tr': 'aptX™: en az gecikme'},
    {   # 5
        'ja': 'LDAC™：最高の音質', 'en': 'LDAC™: highest quality', 'fr': 'LDAC™ : qualité maximale',
        'es': 'LDAC™: la mejor calidad', 'de': 'LDAC™: beste Qualität', 'it': 'LDAC™: qualità massima',
        'nl': 'LDAC™: hoogste kwaliteit', 'ptPT': 'LDAC™: a melhor qualidade', 'ru': 'LDAC™: лучшее качество',
        'ko': 'LDAC™: 최고 음질', 'zhT': 'LDAC™：音質最高', 'zhS': 'LDAC™：音质最高', 'fi': 'LDAC™: paras laatu',
        'sv': 'LDAC™: bäst kvalitet', 'da': 'LDAC™: højeste kvalitet', 'no': 'LDAC™: best kvalitet',
        'pl': 'LDAC™: najwyższa jakość', 'ptBR': 'LDAC™: a melhor qualidade', 'tr': 'LDAC™: en yüksek kalite'},
    {   # 6
        'ja': '音が途切れる？ビットレートを下げる', 'en': 'Sound breaking up? Lower the bit rate', 'fr': 'Le son saccade ? Baissez le débit',
        'es': '¿Sonido cortado? Baja la tasa de bits', 'de': 'Ton setzt aus? Bitrate senken', 'it': 'Audio a scatti? Abbassa il bit rate',
        'nl': 'Haperend geluid? Verlaag de bitrate', 'ptPT': 'O som falha? Baixe o débito', 'ru': 'Звук прерывается? Снизьте битрейт',
        'ko': '소리가 끊기나요? 비트레이트를 낮추세요', 'zhT': '聲音斷續？請降低位元率', 'zhS': '声音断续？请降低码率', 'fi': 'Ääni katkeilee? Pienennä bittinopeutta',
        'sv': 'Hackar ljudet? Sänk bithastigheten', 'da': 'Hakker lyden? Sæt bithastigheden ned', 'no': 'Hakker lyden? Senk bithastigheten',
        'pl': 'Dźwięk się urywa? Obniż przepływność', 'ptBR': 'O som falha? Reduza a taxa de bits', 'tr': 'Ses kesiliyor mu? Bit hızını düşürün'},
]

# Settings > Devices > Bluetooth Devices > the headphones, in each language (the page names as the hint of Read Headset Battery already names them).
PATH = {
    'ja': '設定 > 周辺機器 > Bluetooth機器 > お使いのヘッドホン', 'en': 'Settings > Devices > Bluetooth Devices > your headphones',
    'fr': 'Paramètres > Périphériques > Périphériques Bluetooth > vos écouteurs', 'es': 'Ajustes > Dispositivos > Dispositivos Bluetooth > tus auriculares',
    'de': 'Einstellungen > Geräte > Bluetooth-Geräte > deine Kopfhörer', 'it': 'Impostazioni > Periferiche > Dispositivi Bluetooth > le tue cuffie',
    'nl': 'Instellingen > Randapparatuur > Bluetooth-apparaten > jouw koptelefoon', 'ptPT': 'Definições > Dispositivos > Dispositivos Bluetooth > os seus auscultadores',
    'ru': 'Настройки > Устройства > Устройства Bluetooth > ваши наушники', 'ko': '설정 > 주변기기 > Bluetooth 기기 > 사용 중인 헤드폰',
    'zhT': '設定 > 周邊裝置 > Bluetooth 裝置 > 你的耳機', 'zhS': '设置 > 外围设备 > Bluetooth 设备 > 你的耳机',
    'fi': 'Asetukset > Laitteet > Bluetooth-laitteet > kuulokkeesi', 'sv': 'Inställningar > Enheter > Bluetooth-enheter > dina hörlurar',
    'da': 'Indstillinger > Enheder > Bluetooth-enheder > dine hovedtelefoner', 'no': 'Innstillinger > Enheter > Bluetooth-enheter > hodetelefonene dine',
    'pl': 'Ustawienia > Urządzenia > Urządzenia Bluetooth > twoje słuchawki', 'ptBR': 'Configurações > Dispositivos > Dispositivos Bluetooth > seus fones de ouvido',
    'tr': 'Ayarlar > Aygıtlar > Bluetooth Cihazları > kulaklığınız'}
# {p} is the path, {h} the name of the Headset Codecs row.
HELP_HINT_1 = {
    'ja': '{p}に「{h}」が表示されます。対応しているコーデックと、アドオンが必要なものがわかります。対応しているものを選んでください。',
    'en': '{p} shows {h}: the codecs they offer, and which need an add-on. Choose one they offer.',
    'fr': "{p} affiche « {h} » : les codecs qu'ils proposent et ceux qui demandent un module complémentaire. Choisissez-en un qu'ils proposent.",
    'es': '{p} muestra «{h}»: los códecs que ofrecen y cuáles necesitan un complemento. Elige uno que ofrezcan.',
    'de': '{p} zeigt „{h}“: die Codecs, die sie anbieten, und welche ein Zusatzmodul brauchen. Wähle einen, den sie anbieten.',
    'it': '{p} mostra «{h}»: i codec che offrono e quali richiedono un componente aggiuntivo. Scegline uno che offrono.',
    'nl': "{p} toont '{h}': de codecs die ze aanbieden en welke een uitbreiding nodig hebben. Kies er een die ze aanbieden.",
    'ptPT': '{p} mostra «{h}»: os codecs que oferecem e quais precisam de um extra. Escolha um que ofereçam.',
    'ru': '{p} показывает «{h}»: кодеки, которые они поддерживают, и какие из них требуют дополнения. Выберите один из поддерживаемых.',
    'ko': "{p}에서 '{h}'을 확인하세요. 헤드폰이 지원하는 코덱과 추가 모듈이 필요한 코덱이 표시됩니다. 지원하는 코덱을 고르세요.",
    'zhT': '{p}會顯示「{h}」：耳機提供的編解碼器，以及哪些需要附加模組。請選擇耳機提供的一種。',
    'zhS': '{p}会显示“{h}”：耳机提供的编解码器，以及哪些需要附加模块。请选择耳机提供的一种。',
    'fi': '{p} näyttää ”{h}”: koodekit, joita ne tarjoavat, ja mitkä tarvitsevat lisäosan. Valitse jokin niiden tarjoamista.',
    'sv': '{p} visar ”{h}”: de codecs de erbjuder och vilka som kräver ett tillägg. Välj en som de erbjuder.',
    'da': '{p} viser »{h}«: de codecs, de tilbyder, og hvilke der kræver et tilføjelsesmodul. Vælg en, de tilbyder.',
    'no': '{p} viser «{h}»: kodekene de tilbyr, og hvilke som trenger et tillegg. Velg en de tilbyr.',
    'pl': '{p} pokazuje „{h}”: kodeki, które oferują, i które wymagają dodatku. Wybierz jeden z oferowanych.',
    'ptBR': '{p} mostra "{h}": os codecs que oferecem e quais precisam de um complemento. Escolha um que ofereçam.',
    'tr': '{p} bölümünde "{h}" görünür: sundukları codec\'ler ve hangilerinin eklenti gerektirdiği. Sundukları birini seçin.'}
HELP_HINT_2 = {
    'ja': 'すべてのヘッドホンで使えます。ほかのコーデックが使えないときの代わりにもなります。',
    'en': 'Works with all headphones. It is also the fallback when another codec cannot be used.',
    'fr': "Fonctionne avec tous les écouteurs. C'est aussi le repli quand un autre codec ne peut pas être utilisé.",
    'es': 'Funciona con todos los auriculares. También es la alternativa cuando no se puede usar otro códec.',
    'de': 'Funktioniert mit allen Kopfhörern. Auch die Ausweichlösung, wenn ein anderer Codec nicht genutzt werden kann.',
    'it': 'Funziona con tutte le cuffie. È anche il ripiego quando un altro codec non può essere usato.',
    'nl': 'Werkt met alle koptelefoons. Ook de terugval als een andere codec niet kan worden gebruikt.',
    'ptPT': 'Funciona com todos os auscultadores. É também a alternativa quando outro codec não pode ser usado.',
    'ru': 'Работает со всеми наушниками. Это же запасной вариант, когда другой кодек использовать нельзя.',
    'ko': '모든 헤드폰에서 작동합니다. 다른 코덱을 사용할 수 없을 때의 대체이기도 합니다.',
    'zhT': '適用於所有耳機。其他編解碼器無法使用時，也會退回使用它。',
    'zhS': '适用于所有耳机。其他编解码器无法使用时，也会回退到它。',
    'fi': 'Toimii kaikkien kuulokkeiden kanssa. Se on myös varakoodekki, kun toista ei voi käyttää.',
    'sv': 'Fungerar med alla hörlurar. Det är också reserven när en annan codec inte kan användas.',
    'da': 'Virker med alle hovedtelefoner. Det er også reserven, når en anden codec ikke kan bruges.',
    'no': 'Fungerer med alle hodetelefoner. Det er også reserven når en annen kodek ikke kan brukes.',
    'pl': 'Działa ze wszystkimi słuchawkami. Jest też rozwiązaniem zastępczym, gdy innego kodeka nie można użyć.',
    'ptBR': 'Funciona com todos os fones de ouvido. É também a alternativa quando outro codec não pode ser usado.',
    'tr': 'Tüm kulaklıklarla çalışır. Başka bir codec kullanılamadığında yedek de odur.'}
HELP_HINT_3 = {
    'ja': 'スマートフォン向けのイヤホンによく採用されています。約55 msの遅延が加わります。',
    'en': 'Common on earbuds made for phones. Adds about 55 ms of delay.',
    'fr': 'Courant sur les modèles intra-auriculaires conçus pour les téléphones. Ajoute environ 55 ms de délai.',
    'es': 'Habitual en los auriculares pensados para teléfonos. Añade unos 55 ms de retardo.',
    'de': 'Verbreitet bei Ohrhörern für Smartphones. Fügt etwa 55 ms Verzögerung hinzu.',
    'it': 'Diffuso sugli auricolari pensati per i telefoni. Aggiunge circa 55 ms di ritardo.',
    'nl': 'Gebruikelijk bij oordopjes die voor telefoons zijn gemaakt. Voegt ongeveer 55 ms vertraging toe.',
    'ptPT': 'Comum em auriculares feitos para telemóveis. Acrescenta cerca de 55 ms de atraso.',
    'ru': 'Часто встречается у наушников, сделанных для телефонов. Добавляет около 55 ms задержки.',
    'ko': '휴대전화용으로 만든 이어폰에 흔합니다. 약 55 ms의 지연이 더해집니다.',
    'zhT': '常見於為手機設計的耳機。約增加 55 ms 的延遲。',
    'zhS': '常见于为手机设计的耳机。约增加 55 ms 的延迟。',
    'fi': 'Yleinen puhelimille tehdyissä nappikuulokkeissa. Lisää noin 55 ms viivettä.',
    'sv': 'Vanligt i öronsnäckor gjorda för telefoner. Lägger till cirka 55 ms fördröjning.',
    'da': 'Almindeligt i ørepropper lavet til telefoner. Lægger cirka 55 ms forsinkelse til.',
    'no': 'Vanlig i øreplugger laget for telefoner. Legger til cirka 55 ms forsinkelse.',
    'pl': 'Częste w słuchawkach dousznych robionych do telefonów. Dodaje około 55 ms opóźnienia.',
    'ptBR': 'Comum em fones de ouvido feitos para celulares. Acrescenta cerca de 55 ms de atraso.',
    'tr': 'Telefonlar için yapılmış kulakiçi kulaklıklarda yaygındır. Yaklaşık 55 ms gecikme ekler.'}
# {a} is the name of the aptX HD When Available row, {c} that of the Codec Settings page.
HELP_HINT_4 = {
    'ja': 'aptX HDに対応したヘッドホンでは、代わりにaptX HDを使います（試験的）。「{c}」の「{a}」をオフにすると使いません。',
    'en': 'Headphones that offer aptX HD get that instead (experimental); {a}, in {c}, turns this off.',
    'fr': 'Les écouteurs qui proposent aptX HD l\'utilisent à la place (expérimental) ; « {a} », dans « {c} », le désactive.',
    'es': 'Los auriculares que ofrecen aptX HD usan ese en su lugar (experimental); «{a}», en «{c}», lo desactiva.',
    'de': 'Kopfhörer mit aptX HD bekommen stattdessen dieses (experimentell); „{a}“ unter „{c}“ schaltet das ab.',
    'it': 'Le cuffie che offrono aptX HD usano invece quello (sperimentale); «{a}», in «{c}», lo disattiva.',
    'nl': "Koptelefoons met aptX HD krijgen dat in plaats daarvan (experimenteel); '{a}' onder '{c}' zet dit uit.",
    'ptPT': 'Os auscultadores que oferecem aptX HD usam esse em vez dele (experimental); «{a}», em «{c}», desativa isto.',
    'ru': 'Наушники, которые предлагают aptX HD, получают его вместо этого (экспериментально); пункт «{a}» в «{c}» это отключает.',
    'ko': "aptX HD를 지원하는 헤드폰에서는 대신 그것을 사용합니다(실험적). '{c}'의 '{a}'을 끄면 사용하지 않습니다.",
    'zhT': '支援 aptX HD 的耳機會改用它（實驗性）；在「{c}」中關閉「{a}」即可停用。',
    'zhS': '支持 aptX HD 的耳机会改用它（实验性）；在“{c}”中关闭“{a}”即可停用。',
    'fi': 'Kuulokkeille, jotka tarjoavat aptX HD:tä, käytetään sen sijaan sitä (kokeellinen); ”{a}” kohdassa ”{c}” poistaa sen käytöstä.',
    'sv': 'Hörlurar som erbjuder aptX HD får det i stället (experimentellt); ”{a}” under ”{c}” stänger av det.',
    'da': 'Hovedtelefoner, der tilbyder aptX HD, får det i stedet (eksperimentelt); »{a}« under »{c}« slår det fra.',
    'no': 'Hodetelefoner som tilbyr aptX HD får det i stedet (eksperimentelt); «{a}» under «{c}» slår det av.',
    'pl': 'Słuchawki oferujące aptX HD dostają go zamiast tego (eksperymentalnie); „{a}” w „{c}” to wyłącza.',
    'ptBR': 'Fones de ouvido que oferecem aptX HD usam esse em vez dele (experimental); "{a}", em "{c}", desliga isso.',
    'tr': 'aptX HD sunan kulaklıklarda bunun yerine o kullanılır (deneysel); "{c}" içindeki "{a}" bunu kapatır.'}
HELP_HINT_5 = {
    'ja': '試験的です。660 kb/sは、Wi-Fiが混んでいるときや距離があると途切れることがあります。330 kb/sのほうが安定します。',
    'en': 'Experimental. 660 kb/s may break up with Wi-Fi busy or at range; 330 kb/s is steadier.',
    'fr': 'Expérimental. 660 kb/s peut saccader quand le Wi-Fi est chargé ou à distance ; 330 kb/s est plus stable.',
    'es': 'Experimental. 660 kb/s puede cortarse con el Wi-Fi saturado o a distancia; 330 kb/s es más estable.',
    'de': 'Experimentell. 660 kb/s kann aussetzen, wenn das WLAN ausgelastet oder die Entfernung groß ist; 330 kb/s ist stabiler.',
    'it': 'Sperimentale. 660 kb/s può interrompersi con il Wi-Fi molto usato o a distanza; 330 kb/s è più stabile.',
    'nl': 'Experimenteel. 660 kb/s kan haperen als wifi druk is of op afstand; 330 kb/s is stabieler.',
    'ptPT': 'Experimental. 660 kb/s pode falhar com o Wi-Fi ocupado ou à distância; 330 kb/s é mais estável.',
    'ru': 'Экспериментально. 660 kb/s может прерываться при загруженном Wi-Fi или на расстоянии; 330 kb/s стабильнее.',
    'ko': '실험적입니다. 660 kb/s는 Wi-Fi가 붐비거나 거리가 멀면 끊길 수 있으며, 330 kb/s가 더 안정적입니다.',
    'zhT': '實驗性。Wi-Fi 繁忙或距離較遠時，660 kb/s 可能斷續；330 kb/s 較穩定。',
    'zhS': '实验性。Wi-Fi 繁忙或距离较远时，660 kb/s 可能断续；330 kb/s 更稳定。',
    'fi': 'Kokeellinen. 660 kb/s voi katkeilla, kun Wi-Fi on kuormitettu tai etäisyys on suuri; 330 kb/s on vakaampi.',
    'sv': 'Experimentellt. 660 kb/s kan hacka när Wi-Fi är hårt belastat eller på avstånd; 330 kb/s är stabilare.',
    'da': 'Eksperimentelt. 660 kb/s kan hakke, når Wi-Fi er travlt eller på afstand; 330 kb/s er mere stabilt.',
    'no': 'Eksperimentelt. 660 kb/s kan hakke når Wi-Fi er travelt eller på avstand; 330 kb/s er stabilere.',
    'pl': 'Eksperymentalne. 660 kb/s może się urywać, gdy Wi-Fi jest zajęte lub z daleka; 330 kb/s jest stabilniejsze.',
    'ptBR': 'Experimental. 660 kb/s pode falhar com o Wi-Fi ocupado ou à distância; 330 kb/s é mais estável.',
    'tr': 'Deneysel. 660 kb/s, Wi-Fi yoğunken veya uzakta kesilebilir; 330 kb/s daha kararlıdır.'}
# {c} is the name of the Codec Settings page.
HELP_HINT_6 = {
    'ja': '「{c}」で設定します。低いビットレートほど遠くまで届き、音が安定します。',
    'en': 'In {c}. Lower bit rates reach further and stay steadier.',
    'fr': 'Dans « {c} ». Un débit plus bas porte plus loin et reste plus stable.',
    'es': 'En «{c}». Las tasas de bits más bajas llegan más lejos y son más estables.',
    'de': 'Unter „{c}“. Niedrigere Bitraten reichen weiter und bleiben stabiler.',
    'it': 'In «{c}». I bit rate più bassi arrivano più lontano e restano più stabili.',
    'nl': "Onder '{c}'. Lagere bitrates reiken verder en blijven stabieler.",
    'ptPT': 'Em «{c}». Débitos mais baixos chegam mais longe e são mais estáveis.',
    'ru': 'В «{c}». Чем ниже битрейт, тем лучше дальность и стабильнее звук.',
    'ko': "'{c}'에서 설정합니다. 비트레이트가 낮을수록 더 멀리 닿고 더 안정적입니다.",
    'zhT': '在「{c}」中設定。位元率越低，傳送越遠、聲音越穩定。',
    'zhS': '在“{c}”中设置。码率越低，传输越远、声音越稳定。',
    'fi': 'Kohdassa ”{c}”. Pienemmät bittinopeudet kantavat kauemmas ja pysyvät vakaampina.',
    'sv': 'Under ”{c}”. Lägre bithastigheter når längre och är stabilare.',
    'da': 'Under »{c}«. Lavere bithastigheder rækker længere og er mere stabile.',
    'no': 'Under «{c}». Lavere bithastigheter rekker lenger og er stabilere.',
    'pl': 'W „{c}”. Niższa przepływność sięga dalej i jest stabilniejsza.',
    'ptBR': 'Em "{c}". Taxas de bits mais baixas alcançam mais longe e são mais estáveis.',
    'tr': '"{c}" içinde. Düşük bit hızları daha uzağa ulaşır ve daha kararlıdır.'}
HELP_HINTS = [HELP_HINT_1, HELP_HINT_2, HELP_HINT_3, HELP_HINT_4, HELP_HINT_5, HELP_HINT_6]

# ---- the fallback notification: shown by the Home screen module's thread through the low battery notification's path, text kept in the kernel module ------------------
# {c} is the codec's name (AAC, aptX, aptX HD, LDAC). At most 63 UTF-16 units with the longest name (the system's limit for a notification).
FALLBACK = {   # lang: (the headphones do not offer it, the headphones could not use it)
    'ja': ('お使いのヘッドホンは{c}に対応していません。SBCを使います。', 'お使いのヘッドホンで{c}を使えませんでした。SBCを使います。'),
    'en': ("Your headphones don't support {c}. Using SBC.", "Your headphones couldn't use {c}. Using SBC."),
    'fr': ('Vos écouteurs ne gèrent pas {c}. SBC utilisé.', "Vos écouteurs n'ont pas pu utiliser {c}. SBC utilisé."),
    'es': ('Tus auriculares no admiten {c}. Se usa SBC.', 'Tus auriculares no pudieron usar {c}. Se usa SBC.'),
    'de': ('Deine Kopfhörer unterstützen {c} nicht. Es gilt SBC.', 'Deine Kopfhörer konnten {c} nicht nutzen. Es gilt SBC.'),
    'it': ('Le tue cuffie non supportano {c}. Uso SBC.', 'Le tue cuffie non hanno potuto usare {c}. Uso SBC.'),
    'nl': ('Je koptelefoon ondersteunt {c} niet. SBC wordt gebruikt.', 'Je koptelefoon kon {c} niet gebruiken. SBC wordt gebruikt.'),
    'ptPT': ('Os seus auscultadores não suportam {c}. A usar SBC.', 'Os seus auscultadores não conseguiram usar {c}. A usar SBC.'),
    'ru': ('Ваши наушники не поддерживают {c}. Используется SBC.', 'Наушники не смогли использовать {c}. Используется SBC.'),
    'ko': ('헤드폰이 {c} 코덱을 지원하지 않습니다. SBC를 사용합니다.', '헤드폰에서 {c} 코덱을 사용할 수 없었습니다. SBC를 사용합니다.'),
    'zhT': ('你的耳機不支援 {c}，改用 SBC。', '你的耳機無法使用 {c}，改用 SBC。'),
    'zhS': ('你的耳机不支持 {c}，改用 SBC。', '你的耳机无法使用 {c}，改用 SBC。'),
    'fi': ('Kuulokkeesi eivät tue koodekkia {c}. Käytetään SBC:tä.', '{c} ei toiminut kuulokkeillasi. Käytetään SBC:tä.'),
    'sv': ('Dina hörlurar stöder inte {c}. Använder SBC.', 'Dina hörlurar kunde inte använda {c}. Använder SBC.'),
    'da': ('Dine hovedtelefoner understøtter ikke {c}. Bruger SBC.', 'Dine hovedtelefoner kunne ikke bruge {c}. Bruger SBC.'),
    'no': ('Hodetelefonene dine støtter ikke {c}. Bruker SBC.', 'Hodetelefonene dine kunne ikke bruke {c}. Bruker SBC.'),
    'pl': ('Twoje słuchawki nie obsługują {c}. Używam SBC.', 'Twoje słuchawki nie mogły użyć {c}. Używam SBC.'),
    'ptBR': ('Seus fones de ouvido não suportam {c}. Usando SBC.', 'Seus fones de ouvido não conseguiram usar {c}. Usando SBC.'),
    'tr': ("Kulaklığınız {c} codec'ini desteklemiyor. SBC kullanılıyor.", "Kulaklığınız {c} codec'ini kullanamadı. SBC kullanılıyor."),
}

# ---- the hints of Codec Settings, one line per choice -------------------------------------------------------------------------------------------
# Per language: ab / up: "about" / "up to about" (with the space that follows it, if the language has one); own: the Vita's default profile (in brackets after Standard);
# ear: each ear sent separately; hp: depending on the headphones (experimental); then for each hint the lines and the closing paragraph.
# SBC levels' names are the list items' (without "(Experimental)"), so a line reads as the list does.
H = {
    'ja': dict(ab='約', up='最大約', own='Vitaの標準プロファイル', ear='左右を別々に送信', hp='ヘッドホンにより異なる（試験的）',
               sbc_t='SBCを使うときは常に有効で、AAC、aptX、LDACがSBCに切り替わった場合も同様です。低いビットレートほど遠くまで届き、音が安定します。高いビットレートほど細かな音になります。最高では音が出ないヘッドホンもあります。ヘッドホンは変更を反映するため、一時的に再接続されます。',
               a1='最も遠くまで届く、細かさは最小', a2='中間', a3='初期設定', a4='最も細かい（ヘッドホンが受け付ける上限は超えません）',
               a_t='低いビットレートほど遠くまで届き、音が安定します。AACを使うときだけ有効です。ヘッドホンは変更を反映するため、一時的に再接続されます。',
               l1='初期設定、無線の使用時間はSBCとほぼ同じ', l2='より細かい音、無線時間は2倍で、Wi-Fiが混んでいると途切れることがあります', l3='無線の状況に合わせて330〜660 kb/sの間で切り替え（試験的）',
               l_t='LDACを使うときだけ有効です。変更はすぐに反映され、再接続はありません。',
               x_on='aptX HD、576 kb/s（試験的）、対応するヘッドホンで使います', x_off='標準のaptX、384 kb/s', x_t='ヘッドホンは変更を反映するため、一時的に再接続されます。',
               c_on='より速いエンコード（1フレームあたり3.3 ms）。自己診断に合格した場合', c_off='通常のエンコード（1フレームあたり4.4 ms）',
               c_t='どちらでも音は同じです。ヘッドホンは変更を反映するため、一時的に再接続されます。', on='オン', off='オフ'),
    'en': dict(ab='about ', up='up to about ', own="the Vita's default profile", ear='each ear sent separately', hp='depending on the headphones (experimental)',
               sbc_t='Applies whenever SBC is used, also as the fallback. Lower rates reach further and stay steadier; higher rates carry more detail. Some headphones play nothing at Highest. The headphones reconnect briefly to apply a change.',
               a1='furthest range, least detail', a2='in between', a3='default', a4="most detail (never above what the headphones accept)",
               a_t='Lower rates reach further and stay steadier. Used only with AAC. The headphones reconnect briefly to apply a change.',
               l1='default, about the radio time of SBC', l2='more detail, twice the radio time, may break up with Wi-Fi busy', l3='moves between 330 and 660 kb/s to suit the radio (experimental)',
               l_t='Used only with LDAC. Applies at once, with no reconnect.',
               x_on='aptX HD, 576 kb/s (experimental), with headphones that offer it', x_off='standard aptX, 384 kb/s', x_t='The headphones reconnect briefly to apply a change.',
               c_on='faster encoding (3.3 ms per frame), if its self-test passed', c_off='plain encoding (4.4 ms per frame)',
               c_t='The sound is the same either way. The headphones reconnect briefly to apply a change.', on='On', off='Off'),
    'fr': dict(ab='environ ', up="jusqu'à environ ", own='le profil par défaut de la Vita', ear='chaque oreille séparément', hp='selon les écouteurs (expérimental)',
               sbc_t="S'applique chaque fois que SBC est utilisé, y compris en repli. Un débit plus bas porte plus loin et reste plus stable ; un débit plus haut donne plus de détail. Certains écouteurs ne jouent rien à « La plus élevée ». Les écouteurs se reconnectent brièvement pour appliquer le changement.",
               a1='portée maximale, détail minimal', a2='intermédiaire', a3='par défaut', a4="détail maximal (jamais au-delà de ce que les écouteurs acceptent)",
               a_t="Un débit plus bas porte plus loin et reste plus stable. Ne sert qu'avec AAC. Les écouteurs se reconnectent brièvement pour appliquer le changement.",
               l1='par défaut, à peu près le temps radio de SBC', l2='plus de détail, le double de temps radio, peut saccader si le Wi-Fi est chargé', l3='alterne entre 330 et 660 kb/s selon la radio (expérimental)',
               l_t="Ne sert qu'avec LDAC. S'applique immédiatement, sans reconnexion.",
               x_on='aptX HD, 576 kb/s (expérimental), avec les écouteurs qui le proposent', x_off='aptX standard, 384 kb/s', x_t='Les écouteurs se reconnectent brièvement pour appliquer le changement.',
               c_on='encodage plus rapide (3,3 ms par trame), si son test automatique a réussi', c_off='encodage ordinaire (4,4 ms par trame)',
               c_t='Le son est le même dans les deux cas. Les écouteurs se reconnectent brièvement pour appliquer le changement.', on='Activé', off='Désactivé'),
    'es': dict(ab='unos ', up='hasta unos ', own='el perfil predeterminado de la Vita', ear='cada oído por separado', hp='según los auriculares (experimental)',
               sbc_t='Se aplica siempre que se usa SBC, también como alternativa. Las tasas más bajas llegan más lejos y son más estables; las más altas dan más detalle. Algunos auriculares no reproducen nada en La más alta. Los auriculares se reconectan brevemente para aplicar el cambio.',
               a1='máximo alcance, menos detalle', a2='intermedio', a3='predeterminado', a4='máximo detalle (nunca por encima de lo que aceptan los auriculares)',
               a_t='Las tasas más bajas llegan más lejos y son más estables. Solo se usa con AAC. Los auriculares se reconectan brevemente para aplicar el cambio.',
               l1='predeterminado, casi el mismo tiempo de radio que SBC', l2='más detalle, el doble de tiempo de radio, puede cortarse si el Wi-Fi está saturado', l3='alterna entre 330 y 660 kb/s según la radio (experimental)',
               l_t='Solo se usa con LDAC. Se aplica al instante, sin reconexión.',
               x_on='aptX HD, 576 kb/s (experimental), con los auriculares que lo ofrecen', x_off='aptX estándar, 384 kb/s', x_t='Los auriculares se reconectan brevemente para aplicar el cambio.',
               c_on='codificación más rápida (3,3 ms por trama), si superó su autocomprobación', c_off='codificación normal (4,4 ms por trama)',
               c_t='El sonido es el mismo en ambos casos. Los auriculares se reconectan brevemente para aplicar el cambio.', on='Activado', off='Desactivado'),
    'de': dict(ab='etwa ', up='bis zu etwa ', own='das Standardprofil der Vita', ear='jedes Ohr getrennt', hp='je nach Kopfhörer (experimentell)',
               sbc_t='Gilt immer, wenn SBC verwendet wird, auch als Ausweichlösung. Niedrigere Bitraten reichen weiter und bleiben stabiler; höhere liefern mehr Details. Manche Kopfhörer spielen bei Am höchsten nichts ab. Die Kopfhörer verbinden sich kurz neu, um die Änderung zu übernehmen.',
               a1='größte Reichweite, am wenigsten Details', a2='dazwischen', a3='Vorgabe', a4='die meisten Details (nie über dem, was die Kopfhörer annehmen)',
               a_t='Niedrigere Bitraten reichen weiter und bleiben stabiler. Gilt nur bei AAC. Die Kopfhörer verbinden sich kurz neu, um die Änderung zu übernehmen.',
               l1='Vorgabe, etwa die Funkzeit von SBC', l2='mehr Details, doppelte Funkzeit, kann aussetzen, wenn das WLAN ausgelastet ist', l3='wechselt je nach Funkverbindung zwischen 330 und 660 kb/s (experimentell)',
               l_t='Gilt nur bei LDAC. Wirkt sofort, ohne erneute Verbindung.',
               x_on='aptX HD, 576 kb/s (experimentell), bei Kopfhörern, die es anbieten', x_off='normales aptX, 384 kb/s', x_t='Die Kopfhörer verbinden sich kurz neu, um die Änderung zu übernehmen.',
               c_on='schnelleres Kodieren (3,3 ms pro Frame), wenn der Selbsttest bestanden wurde', c_off='normales Kodieren (4,4 ms pro Frame)',
               c_t='Der Ton ist in beiden Fällen derselbe. Die Kopfhörer verbinden sich kurz neu, um die Änderung zu übernehmen.', on='An', off='Aus'),
    'it': dict(ab='circa ', up='fino a circa ', own='il profilo predefinito della Vita', ear='ogni orecchio separatamente', hp='a seconda delle cuffie (sperimentale)',
               sbc_t='Vale ogni volta che si usa SBC, anche come ripiego. I bit rate più bassi arrivano più lontano e restano più stabili; quelli più alti danno più dettaglio. Alcune cuffie non riproducono nulla con La più alta. Le cuffie si riconnettono brevemente per applicare la modifica.',
               a1='portata massima, meno dettaglio', a2='intermedio', a3='predefinito', a4='massimo dettaglio (mai oltre quanto accettano le cuffie)',
               a_t='I bit rate più bassi arrivano più lontano e restano più stabili. Vale solo con AAC. Le cuffie si riconnettono brevemente per applicare la modifica.',
               l1='predefinito, circa lo stesso tempo radio di SBC', l2='più dettaglio, il doppio del tempo radio, può interrompersi con il Wi-Fi molto usato', l3='passa tra 330 e 660 kb/s in base alla radio (sperimentale)',
               l_t='Vale solo con LDAC. Si applica subito, senza riconnessione.',
               x_on='aptX HD, 576 kb/s (sperimentale), con le cuffie che lo offrono', x_off='aptX standard, 384 kb/s', x_t='Le cuffie si riconnettono brevemente per applicare la modifica.',
               c_on='codifica più veloce (3,3 ms per frame), se il suo test automatico è riuscito', c_off='codifica normale (4,4 ms per frame)',
               c_t='Il suono è lo stesso nei due casi. Le cuffie si riconnettono brevemente per applicare la modifica.', on='Attivo', off='Disattivato'),
    'nl': dict(ab='ongeveer ', up='tot ongeveer ', own='het standaardprofiel van de Vita', ear='elk oor apart', hp='afhankelijk van de koptelefoon (experimenteel)',
               sbc_t='Geldt altijd wanneer SBC wordt gebruikt, ook als terugval. Lagere bitrates reiken verder en blijven stabieler; hogere geven meer detail. Sommige koptelefoons spelen niets af bij Hoogst. De koptelefoons maken kort opnieuw verbinding om de wijziging toe te passen.',
               a1='grootste bereik, minste detail', a2='tussenin', a3='standaard', a4='meeste detail (nooit hoger dan de koptelefoon accepteert)',
               a_t='Lagere bitrates reiken verder en blijven stabieler. Alleen van belang bij AAC. De koptelefoons maken kort opnieuw verbinding om de wijziging toe te passen.',
               l1='standaard, ongeveer de zendtijd van SBC', l2='meer detail, dubbele zendtijd, kan haperen als wifi druk is', l3='wisselt tussen 330 en 660 kb/s naargelang de radio (experimenteel)',
               l_t='Alleen van belang bij LDAC. Werkt direct, zonder opnieuw verbinden.',
               x_on='aptX HD, 576 kb/s (experimenteel), bij koptelefoons die het aanbieden', x_off='standaard aptX, 384 kb/s', x_t='De koptelefoons maken kort opnieuw verbinding om de wijziging toe te passen.',
               c_on='sneller coderen (3,3 ms per frame), als de zelftest is geslaagd', c_off='gewoon coderen (4,4 ms per frame)',
               c_t='Het geluid is in beide gevallen hetzelfde. De koptelefoons maken kort opnieuw verbinding om de wijziging toe te passen.', on='Aan', off='Uit'),
    'ptPT': dict(ab='cerca de ', up='até cerca de ', own='o perfil predefinido da Vita', ear='cada ouvido em separado', hp='consoante os auscultadores (experimental)',
               sbc_t='Aplica-se sempre que o SBC é usado, também como alternativa. Débitos mais baixos chegam mais longe e são mais estáveis; os mais altos dão mais detalhe. Alguns auscultadores não reproduzem nada em A mais alta. Os auscultadores voltam a ligar-se brevemente para aplicar a alteração.',
               a1='maior alcance, menos detalhe', a2='intermédio', a3='predefinido', a4='mais detalhe (nunca acima do que os auscultadores aceitam)',
               a_t='Débitos mais baixos chegam mais longe e são mais estáveis. Só conta com o AAC. Os auscultadores voltam a ligar-se brevemente para aplicar a alteração.',
               l1='predefinido, cerca do mesmo tempo de rádio que o SBC', l2='mais detalhe, o dobro do tempo de rádio, pode falhar com o Wi-Fi ocupado', l3='alterna entre 330 e 660 kb/s conforme o rádio (experimental)',
               l_t='Só conta com o LDAC. Aplica-se de imediato, sem nova ligação.',
               x_on='aptX HD, 576 kb/s (experimental), com os auscultadores que o oferecem', x_off='aptX normal, 384 kb/s', x_t='Os auscultadores voltam a ligar-se brevemente para aplicar a alteração.',
               c_on='codificação mais rápida (3,3 ms por trama), se o teste automático passou', c_off='codificação normal (4,4 ms por trama)',
               c_t='O som é o mesmo nos dois casos. Os auscultadores voltam a ligar-se brevemente para aplicar a alteração.', on='Ligado', off='Desligado'),
    'ru': dict(ab='около ', up='до примерно ', own='профиль Vita по умолчанию', ear='каждое ухо отдельно', hp='зависит от наушников (экспериментально)',
               sbc_t='Действует всегда, когда используется SBC, в том числе как запасной вариант. Чем ниже битрейт, тем лучше дальность и стабильнее звук; чем выше, тем больше деталей. Некоторые наушники не воспроизводят ничего на уровне «Максимальное». Наушники ненадолго переподключаются, чтобы применить изменение.',
               a1='максимальная дальность, меньше всего деталей', a2='посередине', a3='по умолчанию', a4='больше всего деталей (не выше, чем принимают наушники)',
               a_t='Чем ниже битрейт, тем лучше дальность и стабильнее звук. Действует только при кодеке AAC. Наушники ненадолго переподключаются, чтобы применить изменение.',
               l1='по умолчанию, примерно столько же эфира, сколько у SBC', l2='больше деталей, вдвое больше эфира, возможны сбои при загруженном Wi-Fi', l3='переключается между 330 и 660 kb/s в зависимости от радиоканала (экспериментально)',
               l_t='Действует только при кодеке LDAC. Применяется сразу, без переподключения.',
               x_on='aptX HD, 576 kb/s (экспериментально), с наушниками, которые его предлагают', x_off='обычный aptX, 384 kb/s', x_t='Наушники ненадолго переподключаются, чтобы применить изменение.',
               c_on='более быстрое кодирование (3,3 ms на кадр), если пройдена самопроверка', c_off='обычное кодирование (4,4 ms на кадр)',
               c_t='Звук в обоих случаях одинаков. Наушники ненадолго переподключаются, чтобы применить изменение.', on='Вкл.', off='Выкл.'),
    'ko': dict(ab='약 ', up='최대 약 ', own='Vita의 기본 프로필', ear='좌우를 따로 전송', hp='헤드폰에 따라 다름(실험적)',
               sbc_t='SBC를 사용할 때는 항상 적용되며, 대체 코덱으로 쓰일 때도 마찬가지입니다. 비트레이트가 낮을수록 더 멀리 닿고 더 안정적이며, 높을수록 더 세밀합니다. 가장 높음에서는 소리가 나지 않는 헤드폰도 있습니다. 변경 사항을 적용하기 위해 헤드폰이 잠시 다시 연결됩니다.',
               a1='가장 멀리 닿음, 가장 덜 세밀함', a2='중간', a3='기본값', a4='가장 세밀함(헤드폰이 받는 한도를 넘지 않음)',
               a_t='비트레이트가 낮을수록 더 멀리 닿고 더 안정적입니다. 코덱이 AAC일 때만 적용됩니다. 변경 사항을 적용하기 위해 헤드폰이 잠시 다시 연결됩니다.',
               l1='기본값, 무선 사용 시간은 SBC와 비슷', l2='더 세밀함, 무선 시간 두 배, Wi-Fi가 붐비면 끊길 수 있음', l3='무선 상태에 맞춰 330~660 kb/s 사이에서 전환(실험적)',
               l_t='LDAC를 사용할 때만 적용됩니다. 바로 적용되며 다시 연결되지 않습니다.',
               x_on='aptX HD, 576 kb/s(실험적), 지원하는 헤드폰에서 사용', x_off='표준 aptX, 384 kb/s', x_t='변경 사항을 적용하기 위해 헤드폰이 잠시 다시 연결됩니다.',
               c_on='더 빠른 인코딩(프레임당 3.3 ms), 자체 점검을 통과한 경우', c_off='일반 인코딩(프레임당 4.4 ms)',
               c_t='어느 쪽이든 소리는 같습니다. 변경 사항을 적용하기 위해 헤드폰이 잠시 다시 연결됩니다.', on='켬', off='끔'),
    'zhT': dict(ab='約 ', up='最高約 ', own='Vita 的預設設定檔', ear='左右聲道分開傳送', hp='依耳機而定（實驗性）',
               sbc_t='只要使用 SBC 就會套用，作為備用時也一樣。位元率越低，傳送越遠、聲音越穩定；越高，細節越多。部分耳機在「最高」下完全沒有聲音。耳機會短暫重新連線以套用變更。',
               a1='距離最遠，細節最少', a2='介於兩者之間', a3='預設值', a4='細節最多（不會超過耳機能接受的上限）',
               a_t='位元率越低，傳送越遠、聲音越穩定。僅在編解碼器為 AAC 時使用。耳機會短暫重新連線以套用變更。',
               l1='預設值，佔用的無線時間與 SBC 相近', l2='細節更多，無線時間為兩倍，Wi-Fi 繁忙時可能斷續', l3='依無線狀況在 330 與 660 kb/s 之間切換（實驗性）',
               l_t='僅在編解碼器為 LDAC 時使用。立即套用，不需重新連線。',
               x_on='aptX HD，576 kb/s（實驗性），適用於支援它的耳機', x_off='標準 aptX，384 kb/s', x_t='耳機會短暫重新連線以套用變更。',
               c_on='較快的編碼（每幀 3.3 ms），需通過其自我檢查', c_off='一般編碼（每幀 4.4 ms）',
               c_t='兩種方式的聲音相同。耳機會短暫重新連線以套用變更。', on='開啟', off='關閉'),
    'zhS': dict(ab='约 ', up='最高约 ', own='Vita 的默认配置文件', ear='左右声道分开发送', hp='视耳机而定（实验性）',
               sbc_t='只要使用 SBC 就会应用，作为备用时也一样。码率越低，传输越远、声音越稳定；越高，细节越多。部分耳机在“最高”下完全没有声音。耳机会短暂重新连接以应用更改。',
               a1='距离最远，细节最少', a2='介于两者之间', a3='默认值', a4='细节最多（不会超过耳机能接受的上限）',
               a_t='码率越低，传输越远、声音越稳定。仅在编解码器为 AAC 时使用。耳机会短暂重新连接以应用更改。',
               l1='默认值，占用的无线时间与 SBC 相近', l2='细节更多，无线时间为两倍，Wi-Fi 繁忙时可能断续', l3='根据无线状况在 330 与 660 kb/s 之间切换（实验性）',
               l_t='仅在编解码器为 LDAC 时使用。立即应用，无需重新连接。',
               x_on='aptX HD，576 kb/s（实验性），适用于支持它的耳机', x_off='标准 aptX，384 kb/s', x_t='耳机会短暂重新连接以应用更改。',
               c_on='更快的编码（每帧 3.3 ms），需通过其自检', c_off='普通编码（每帧 4.4 ms）',
               c_t='两种方式的声音相同。耳机会短暂重新连接以应用更改。', on='开启', off='关闭'),
    'fi': dict(ab='noin ', up='enintään noin ', own='Vitan oletusprofiili', ear='kumpikin korva erikseen', hp='kuulokkeista riippuen (kokeellinen)',
               sbc_t='Vaikuttaa aina, kun SBC on käytössä, myös varakoodekkina. Pienemmät bittinopeudet kantavat kauemmas ja pysyvät vakaampina; suuremmat antavat enemmän yksityiskohtia. Jotkin kuulokkeet eivät toista mitään tasolla Korkein. Kuulokkeet yhdistyvät hetkeksi uudelleen muutoksen käyttöön ottamiseksi.',
               a1='pisin kantama, vähiten yksityiskohtia', a2='siltä väliltä', a3='oletus', a4='eniten yksityiskohtia (ei koskaan yli sen, mitä kuulokkeet hyväksyvät)',
               a_t='Pienemmät bittinopeudet kantavat kauemmas ja pysyvät vakaampina. Käytössä vain AAC:n kanssa. Kuulokkeet yhdistyvät hetkeksi uudelleen muutoksen käyttöön ottamiseksi.',
               l1='oletus, suunnilleen saman verran radioaikaa kuin SBC', l2='enemmän yksityiskohtia, kaksinkertainen radioaika, voi katkeilla, kun Wi-Fi on kuormitettu', l3='vaihtaa 330 ja 660 kb/s välillä radion mukaan (kokeellinen)',
               l_t='Käytössä vain LDAC:n kanssa. Tulee voimaan heti, ilman uudelleenyhdistämistä.',
               x_on='aptX HD, 576 kb/s (kokeellinen), kuulokkeille, jotka tarjoavat sitä', x_off='tavallinen aptX, 384 kb/s', x_t='Kuulokkeet yhdistyvät hetkeksi uudelleen muutoksen käyttöön ottamiseksi.',
               c_on='nopeampi koodaus (3,3 ms kehystä kohti), jos sen itsetarkastus meni läpi', c_off='tavallinen koodaus (4,4 ms kehystä kohti)',
               c_t='Ääni on molemmilla sama. Kuulokkeet yhdistyvät hetkeksi uudelleen muutoksen käyttöön ottamiseksi.', on='Päällä', off='Pois'),
    'sv': dict(ab='cirka ', up='upp till cirka ', own='Vitans standardprofil', ear='varje öra för sig', hp='beroende på hörlurarna (experimentellt)',
               sbc_t='Gäller så fort SBC används, även som reserv. Lägre bithastigheter når längre och är stabilare; högre ger mer detaljer. Vissa hörlurar spelar inget alls på Högst. Hörlurarna ansluter kort på nytt för att tillämpa ändringen.',
               a1='längst räckvidd, minst detaljer', a2='däremellan', a3='standard', a4='mest detaljer (aldrig över vad hörlurarna tar emot)',
               a_t='Lägre bithastigheter når längre och är stabilare. Gäller bara med AAC. Hörlurarna ansluter kort på nytt för att tillämpa ändringen.',
               l1='standard, ungefär samma radiotid som SBC', l2='mer detaljer, dubbel radiotid, kan hacka när Wi-Fi är hårt belastat', l3='växlar mellan 330 och 660 kb/s efter radion (experimentellt)',
               l_t='Gäller bara med LDAC. Ändringen slår igenom direkt, utan ny anslutning.',
               x_on='aptX HD, 576 kb/s (experimentellt), med hörlurar som erbjuder det', x_off='vanlig aptX, 384 kb/s', x_t='Hörlurarna ansluter kort på nytt för att tillämpa ändringen.',
               c_on='snabbare kodning (3,3 ms per ram), om dess självtest godkändes', c_off='vanlig kodning (4,4 ms per ram)',
               c_t='Ljudet är detsamma i båda fallen. Hörlurarna ansluter kort på nytt för att tillämpa ändringen.', on='På', off='Av'),
    'da': dict(ab='cirka ', up='op til cirka ', own='Vitas standardprofil', ear='hvert øre for sig', hp='afhængigt af hovedtelefonerne (eksperimentelt)',
               sbc_t='Gælder, hver gang SBC bruges, også som reserve. Lavere bithastigheder rækker længere og er mere stabile; højere giver flere detaljer. Nogle hovedtelefoner afspiller intet på Højest. Hovedtelefonerne forbindes kort igen for at anvende ændringen.',
               a1='længst rækkevidde, færrest detaljer', a2='midt imellem', a3='standard', a4='flest detaljer (aldrig over, hvad hovedtelefonerne accepterer)',
               a_t='Lavere bithastigheder rækker længere og er mere stabile. Bruges kun med AAC. Hovedtelefonerne forbindes kort igen for at anvende ændringen.',
               l1='standard, omtrent samme radiotid som SBC', l2='flere detaljer, dobbelt radiotid, kan hakke, hvor Wi-Fi er travlt', l3='skifter mellem 330 og 660 kb/s efter radioen (eksperimentelt)',
               l_t='Bruges kun med LDAC. Træder i kraft med det samme, uden ny forbindelse.',
               x_on='aptX HD, 576 kb/s (eksperimentelt), med hovedtelefoner, der tilbyder det', x_off='almindelig aptX, 384 kb/s', x_t='Hovedtelefonerne forbindes kort igen for at anvende ændringen.',
               c_on='hurtigere kodning (3,3 ms pr. ramme), hvis dens selvtest er bestået', c_off='almindelig kodning (4,4 ms pr. ramme)',
               c_t='Lyden er den samme i begge tilfælde. Hovedtelefonerne forbindes kort igen for at anvende ændringen.', on='Til', off='Fra'),
    'no': dict(ab='cirka ', up='opptil cirka ', own='Vitas standardprofil', ear='hvert øre for seg', hp='avhengig av hodetelefonene (eksperimentelt)',
               sbc_t='Gjelder hver gang SBC brukes, også som reserve. Lavere bithastigheter rekker lenger og er stabilere; høyere gir flere detaljer. Noen hodetelefoner spiller ingenting på Høyest. Hodetelefonene kobles kort til igjen for å bruke endringen.',
               a1='lengst rekkevidde, færrest detaljer', a2='midt imellom', a3='standard', a4='flest detaljer (aldri over det hodetelefonene godtar)',
               a_t='Lavere bithastigheter rekker lenger og er stabilere. Brukes bare med AAC. Hodetelefonene kobles kort til igjen for å bruke endringen.',
               l1='standard, omtrent samme radiotid som SBC', l2='flere detaljer, dobbel radiotid, kan hakke der Wi-Fi er travelt', l3='bytter mellom 330 og 660 kb/s etter radioen (eksperimentelt)',
               l_t='Brukes bare med LDAC. Gjelder med en gang, uten ny tilkobling.',
               x_on='aptX HD, 576 kb/s (eksperimentelt), med hodetelefoner som tilbyr det', x_off='vanlig aptX, 384 kb/s', x_t='Hodetelefonene kobles kort til igjen for å bruke endringen.',
               c_on='raskere koding (3,3 ms per ramme), hvis selvtesten ble bestått', c_off='vanlig koding (4,4 ms per ramme)',
               c_t='Lyden er den samme i begge tilfeller. Hodetelefonene kobles kort til igjen for å bruke endringen.', on='På', off='Av'),
    'pl': dict(ab='około ', up='do około ', own='domyślny profil konsoli Vita', ear='każde ucho osobno', hp='zależnie od słuchawek (eksperymentalnie)',
               sbc_t='Działa zawsze, gdy używany jest SBC, także jako rozwiązanie zastępcze. Niższa przepływność sięga dalej i jest stabilniejsza; wyższa daje więcej szczegółów. Niektóre słuchawki nie odtwarzają niczego na poziomie Najwyższa. Słuchawki na chwilę łączą się ponownie, aby zastosować zmianę.',
               a1='największy zasięg, najmniej szczegółów', a2='pośrednia', a3='domyślnie', a4='najwięcej szczegółów (nigdy powyżej tego, co przyjmują słuchawki)',
               a_t='Niższa przepływność sięga dalej i jest stabilniejsza. Działa tylko z AAC. Słuchawki na chwilę łączą się ponownie, aby zastosować zmianę.',
               l1='domyślnie, mniej więcej tyle czasu radiowego co SBC', l2='więcej szczegółów, dwa razy więcej czasu radiowego, może się urywać, gdy Wi-Fi jest zajęte', l3='przełącza między 330 a 660 kb/s zależnie od łącza (eksperymentalnie)',
               l_t='Działa tylko z LDAC. Zmiana działa od razu, bez ponownego łączenia.',
               x_on='aptX HD, 576 kb/s (eksperymentalnie), ze słuchawkami, które go oferują', x_off='standardowy aptX, 384 kb/s', x_t='Słuchawki na chwilę łączą się ponownie, aby zastosować zmianę.',
               c_on='szybsze kodowanie (3,3 ms na ramkę), jeśli przeszło samokontrolę', c_off='zwykłe kodowanie (4,4 ms na ramkę)',
               c_t='Dźwięk jest taki sam w obu przypadkach. Słuchawki na chwilę łączą się ponownie, aby zastosować zmianę.', on='Włączony', off='Wyłączony'),
    'ptBR': dict(ab='cerca de ', up='até cerca de ', own='o perfil padrão do Vita', ear='cada ouvido separadamente', hp='conforme os fones de ouvido (experimental)',
               sbc_t='Vale sempre que o SBC é usado, também como alternativa. Taxas de bits mais baixas alcançam mais longe e são mais estáveis; as mais altas dão mais detalhe. Alguns fones de ouvido não reproduzem nada em A mais alta. Os fones de ouvido se reconectam brevemente para aplicar a mudança.',
               a1='maior alcance, menos detalhe', a2='intermediário', a3='padrão', a4='mais detalhe (nunca acima do que os fones de ouvido aceitam)',
               a_t='Taxas de bits mais baixas alcançam mais longe e são mais estáveis. Só vale com o AAC. Os fones de ouvido se reconectam brevemente para aplicar a mudança.',
               l1='padrão, cerca do mesmo tempo de rádio que o SBC', l2='mais detalhe, o dobro do tempo de rádio, pode falhar com o Wi-Fi ocupado', l3='alterna entre 330 e 660 kb/s conforme o rádio (experimental)',
               l_t='Só vale com o LDAC. Aplica na hora, sem reconexão.',
               x_on='aptX HD, 576 kb/s (experimental), com fones de ouvido que o oferecem', x_off='aptX padrão, 384 kb/s', x_t='Os fones de ouvido se reconectam brevemente para aplicar a mudança.',
               c_on='codificação mais rápida (3,3 ms por quadro), se o teste automático passou', c_off='codificação normal (4,4 ms por quadro)',
               c_t='O som é o mesmo nos dois casos. Os fones de ouvido se reconectam brevemente para aplicar a mudança.', on='Ligado', off='Desligado'),
    'tr': dict(ab='yaklaşık ', up='en fazla yaklaşık ', own="Vita'nın varsayılan profili", ear='her kulak ayrı', hp='kulaklığa göre değişir (deneysel)',
               sbc_t='SBC kullanıldığında her zaman geçerlidir, yedek olarak kullanıldığında da. Düşük bit hızları daha uzağa ulaşır ve daha kararlıdır; yüksek bit hızları daha fazla ayrıntı taşır. Bazı kulaklıklar En yüksek düzeyinde hiç ses çalmaz. Değişikliği uygulamak için kulaklıklar kısa süreliğine yeniden bağlanır.',
               a1='en uzak menzil, en az ayrıntı', a2='arası', a3='varsayılan', a4='en fazla ayrıntı (kulaklığın kabul ettiğinin asla üstünde değil)',
               a_t='Düşük bit hızları daha uzağa ulaşır ve daha kararlıdır. Yalnızca AAC ile kullanılır. Değişikliği uygulamak için kulaklıklar kısa süreliğine yeniden bağlanır.',
               l1='varsayılan, yaklaşık SBC kadar radyo süresi', l2='daha fazla ayrıntı, iki kat radyo süresi, Wi-Fi yoğunken kesilebilir', l3='radyo koşullarına göre 330 ile 660 kb/s arasında geçiş yapar (deneysel)',
               l_t='Yalnızca LDAC ile kullanılır. Hemen uygulanır, yeniden bağlanmaz.',
               x_on='aptX HD, 576 kb/s (deneysel), sunan kulaklıklarla', x_off='standart aptX, 384 kb/s', x_t='Değişikliği uygulamak için kulaklıklar kısa süreliğine yeniden bağlanır.',
               c_on='daha hızlı kodlama (kare başına 3,3 ms), öz denetimi geçtiyse', c_off='sıradan kodlama (kare başına 4,4 ms)',
               c_t='Ses iki durumda da aynıdır. Değişikliği uygulamak için kulaklıklar kısa süreliğine yeniden bağlanır.', on='Açık', off='Kapalı'),
}


def _strip(name):
    """A list item's name without its bracket ("Highest (Experimental)" -> "Highest")."""
    return re.sub(r'\s*[（(][^）)]*[）)]\s*$', '', name)


def join_lines(lang, lines, tail, mode):
    """The hint: one line per choice and a closing paragraph. 'newline': the lines on lines of their own. 'semicolon': one run of text."""
    if mode == 'newline':
        return '\n'.join(lines + [tail])
    if lang in CJK:
        return '；'.join(lines) + '。' + tail
    sep = ' ; ' if lang == 'fr' else '; '
    return sep.join(lines) + '. ' + tail


def _brackets(lang, s):
    return f'（{s}）' if lang in CJK else f' ({s})'


def hints(S, lang, mode):
    """The five list-style hints of Codec Settings for one language: {id: text}."""
    h = H[lang]
    n = lambda sid: S[sid][lang]
    comma = '、' if lang == 'ja' else '，' if lang in ('zhT', 'zhS') else ', '
    sbc = [
        f"{_strip(n('ITEM_Q_STABLE'))} = {h['ab']}260 kb/s",
        f"{_strip(n('ITEM_Q_BALANCED'))} = {h['ab']}310 kb/s",
        f"{n('ITEM_STANDARD')} = {h['up']}357 kb/s{_brackets(lang, h['own'])}",
        f"{_strip(n('ITEM_Q_HIGH'))} = {h['ab']}490 kb/s{comma}{h['ear']}",
        f"{_strip(n('ITEM_Q_HIGHEST'))} = {h['ab']}670–740 kb/s{comma}{h['hp']}",
    ]
    aac = [f"128 kb/s = {h['a1']}", f"192 kb/s = {h['a2']}", f"256 kb/s = {h['a3']}", f"320 kb/s = {h['a4']}"]
    ldac = [f"330 kb/s = {h['l1']}", f"660 kb/s = {h['l2']}", f"{n('ITEM_LDAC_ABR')} = {h['l3']}"]
    apt = [f"{h['on']} = {h['x_on']}", f"{h['off']} = {h['x_off']}"]
    acc = [f"{h['on']} = {h['c_on']}", f"{h['off']} = {h['c_off']}"]
    return {
        'HINT_QUALITY': join_lines(lang, sbc, h['sbc_t'], mode),
        'HINT_AACRATE': join_lines(lang, aac, h['a_t'], mode),
        'HINT_LDACRATE': join_lines(lang, ldac, h['l_t'], mode),
        'HINT_APTXHD': join_lines(lang, apt, h['x_t'], mode),
        'HINT_AACACCEL': join_lines(lang, acc, h['c_t'], mode),
    }


def install(S, LANGS, mode):
    """Adds the codec help page and the fallback's strings to S, and the list-style hints (HINT_QUALITY, HINT_AACRATE, HINT_LDACRATE, HINT_APTXHD, HINT_AACACCEL)."""
    def lang_texts(d):
        return {l: d[l] for l in LANGS if l != 'enGB'}

    S['ROW_CODECHELP'] = lang_texts(ROW_CODECHELP)
    S['HINT_CODECHELP'] = lang_texts(HINT_CODECHELP)
    for i in range(6):
        S[f'HELP_TITLE_{i + 1}'] = lang_texts(HELP_TITLE[i])
    for i, d in enumerate(HELP_HINTS):
        S[f'HELP_HINT_{i + 1}'] = {l: d[l].format(p=PATH[l], h=S['ROW_DV_HCODECS'][l], a=S['ROW_APTXHD'][l], c=S['ROW_CODECSET'][l])
                                   for l in LANGS if l != 'enGB'}
    for l in LANGS:
        if l == 'enGB':
            continue
        for sid, text in hints(S, l, mode).items():
            S.setdefault(sid, {})[l] = text
    return {l: FALLBACK[l] for l in LANGS if l != 'enGB'}
