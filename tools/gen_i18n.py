"""Generate the Settings page translations: settings_app/bluetoothturbo_i18n_ids.h
(the string ids) and settings_app/bluetoothturbo_i18n_table.h (the strings).

    python3 tools/gen_i18n.py settings_app user kernel   write the two headers, the Home screen module's notice table and the
                                                      kernel module's fallback notification table
    python3 tools/gen_i18n.py --check settings_app user kernel   fail if the headers on disk differ, or if a
                                                      language is missing or a row differs in
                                                      its placeholders (make test)

Columns follow the Vita system-language index (enum btt_lang_id). Only English
is authoritative; the rest are machine translations, open to native-speaker fixes.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_i18n_codec

# The one switch for the list-style hints of Codec Settings (SBC Audio Quality, AAC Bit Rate, LDAC Bit Rate, aptX HD When Available, Hardware Accelerated AAC):
# 'newline' puts each choice on a line of its own; 'semicolon' is the fallback form, one run of text with the choices separated by semicolons. To use it, set this to 'semicolon' and run the generator
# (python3 tools/gen_i18n.py settings_app user kernel), then rebuild. `--check` and `make test` follow whichever is set, and test both.
HINT_LINES = 'newline'

LANGS = ['ja', 'en', 'fr', 'es', 'de', 'it', 'nl', 'ptPT', 'ru', 'ko', 'zhT', 'zhS',
         'fi', 'sv', 'da', 'no', 'pl', 'ptBR', 'enGB', 'tr']

S = {}  # id -> {lang: text}; enGB defaults to en


def add(sid, **t):
    S[sid] = t


add('TITLE', **{l: 'Bluetooth® Turbo' for l in LANGS})
add('ROW_ENABLE',
    ja='Bluetooth Turboを有効にする',
    en='Enable Bluetooth Turbo',
    fr='Activer Bluetooth Turbo',
    es='Activar Bluetooth Turbo',
    de='Bluetooth Turbo aktivieren',
    it='Attiva Bluetooth Turbo',
    nl='Bluetooth Turbo inschakelen',
    ptPT='Ativar o Bluetooth Turbo',
    ru='Включить Bluetooth Turbo',
    ko='Bluetooth Turbo 사용',
    zhT='啟用 Bluetooth Turbo',
    zhS='启用 Bluetooth Turbo',
    fi='Ota Bluetooth Turbo käyttöön',
    sv='Aktivera Bluetooth Turbo',
    da='Aktivér Bluetooth Turbo',
    no='Aktiver Bluetooth Turbo',
    pl='Włącz Bluetooth Turbo',
    ptBR='Ativar o Bluetooth Turbo',
    tr="Bluetooth Turbo'yu etkinleştir")
add('HINT_ENABLE',
    ja='Bluetooth Turboをオンにします。Bluetoothヘッドホンに送る音のタイミングを修正し（これがないと無音になるイヤホンがあります）、以下の設定を適用します。オフにすると本体本来の動作に戻ります。この設定をオンにした状態でデバイスをペアリングし直すと、最も高い互換性が得られます。',
    en='Turns Bluetooth Turbo on: it fixes the timing of the sound sent to Bluetooth headphones (some earbuds stay silent without it) and applies the settings below. When off, the console works as it did originally. Re-pair your devices with this turned on for maximum compatibility.',
    fr="Active Bluetooth Turbo : corrige le timing du son envoyé aux écouteurs Bluetooth (certains modèles intra-auriculaires restent muets sans cela) et applique les réglages ci-dessous. Désactivé, la console fonctionne comme à l'origine. Pour une compatibilité maximale, associez de nouveau vos appareils avec ce réglage activé.",
    es='Activa Bluetooth Turbo: corrige la sincronización del sonido enviado a los auriculares Bluetooth (algunos auriculares no suenan sin esto) y aplica los ajustes de abajo. Desactivado, la consola funciona como de origen. Para una compatibilidad máxima, vuelve a emparejar tus dispositivos con esto activado.',
    de='Schaltet Bluetooth Turbo ein: korrigiert das Timing des an Bluetooth-Kopfhörer gesendeten Tons (manche Ohrhörer bleiben sonst stumm) und wendet die Einstellungen darunter an. Aus bedeutet das ursprüngliche Verhalten der Konsole. Für maximale Kompatibilität Geräte bei eingeschalteter Option neu koppeln.',
    it='Attiva Bluetooth Turbo: corregge la temporizzazione del suono inviato alle cuffie Bluetooth (alcuni auricolari restano muti senza questa correzione) e applica le impostazioni qui sotto. Se disattivato, la console funziona come in origine. Per la massima compatibilità, abbina di nuovo i dispositivi con questa opzione attiva.',
    nl='Schakelt Bluetooth Turbo in: corrigeert de timing van het geluid dat naar Bluetooth-koptelefoons wordt gestuurd (sommige oordopjes blijven anders stil) en past de instellingen hieronder toe. Uit geeft het oorspronkelijke gedrag van de console. Koppel je apparaten opnieuw terwijl dit aan staat, voor de beste compatibiliteit.',
    ptPT='Ativa o Bluetooth Turbo: corrige a temporização do som enviado para os auscultadores Bluetooth (alguns auriculares ficam mudos sem isto) e aplica as definições abaixo. Desativado, a consola funciona como de origem. Para máxima compatibilidade, volte a emparelhar os seus dispositivos com isto ativado.',
    ru='Включает Bluetooth Turbo: исправляет тайминг звука, передаваемого на Bluetooth-наушники (без этого некоторые наушники молчат), и применяет настройки ниже. Если выключено, консоль работает как раньше. Для наилучшей совместимости выполните сопряжение устройств заново, когда эта функция включена.',
    ko='Bluetooth Turbo를 켭니다. Bluetooth 헤드폰으로 보내는 소리의 타이밍을 바로잡고(일부 이어폰은 이것이 없으면 소리가 나지 않습니다) 아래 설정을 적용합니다. 끄면 본체 본래의 동작으로 돌아갑니다. 최대한의 호환성을 위해 이 설정을 켠 상태에서 기기를 다시 페어링하세요.',
    zhT='開啟 Bluetooth Turbo：修正傳送到 Bluetooth 耳機的聲音時序（部分耳機沒有此修正會沒有聲音），並套用下方設定。關閉時主機維持原本的運作方式。為獲得最佳相容性，請在開啟此設定的狀態下重新配對裝置。',
    zhS='开启 Bluetooth Turbo：修正发送到 Bluetooth 耳机的声音时序（部分耳机没有此修正会没有声音），并应用下方设置。关闭时主机保持原本的运行方式。为获得最佳兼容性，请在开启此设置的状态下重新配对设备。',
    fi='Ottaa Bluetooth Turbon käyttöön: korjaa Bluetooth-kuulokkeille lähetettävän äänen ajoituksen (jotkin nappikuulokkeet pysyvät muuten hiljaa) ja ottaa alla olevat asetukset käyttöön. Pois päältä konsoli toimii kuten alun perin. Parhaan yhteensopivuuden saat, kun muodostat laiteparit uudelleen tämän ollessa päällä.',
    sv='Slår på Bluetooth Turbo: rättar timingen på ljudet som skickas till Bluetooth-hörlurar (vissa öronsnäckor förblir annars tysta) och tillämpar inställningarna nedan. Av ger konsolens ursprungliga beteende. Parkoppla dina enheter igen med detta påslaget för bästa kompatibilitet.',
    da='Slår Bluetooth Turbo til: retter timingen på lyden, der sendes til Bluetooth-hovedtelefoner (nogle ørepropper forbliver ellers tavse), og anvender indstillingerne nedenfor. Slået fra opfører konsollen sig som oprindeligt. Par dine enheder igen, mens dette er slået til, for den bedste kompatibilitet.',
    no='Slår på Bluetooth Turbo: retter timingen på lyden som sendes til Bluetooth-hodetelefoner (noen øreplugger forblir ellers stille) og bruker innstillingene nedenfor. Av gir konsollens opprinnelige oppførsel. Par enhetene dine på nytt mens dette er slått på, for best mulig kompatibilitet.',
    pl='Włącza Bluetooth Turbo: poprawia synchronizację dźwięku wysyłanego do słuchawek Bluetooth (niektóre słuchawki douszne bez tego milczą) i stosuje poniższe ustawienia. Po wyłączeniu konsola działa tak jak pierwotnie. Aby uzyskać maksymalną zgodność, sparuj urządzenia ponownie przy włączonej tej opcji.',
    ptBR='Ativa o Bluetooth Turbo: corrige a temporização do som enviado para os fones de ouvido Bluetooth (alguns fones de ouvido ficam mudos sem isso) e aplica as configurações abaixo. Desativado, o console funciona como originalmente. Para máxima compatibilidade, pareie seus dispositivos de novo com isto ativado.',
    tr="Bluetooth Turbo'yu açar: Bluetooth kulaklıklara gönderilen sesin zamanlamasını düzeltir (bazı kulakiçi kulaklıklar bu olmadan sessiz kalır) ve aşağıdaki ayarları uygular. Kapalıyken konsol ilk haliyle çalışır. En iyi uyumluluk için cihazlarınızı bu açıkken yeniden eşleştirin.")
add('ROW_SOUND',
    ja='Bluetoothのサウンド',
    en='Bluetooth Sound',
    fr='Son Bluetooth',
    es='Sonido Bluetooth',
    de='Bluetooth-Klang',
    it='Suono Bluetooth',
    nl='Bluetooth-geluid',
    ptPT='Som Bluetooth',
    ru='Звук Bluetooth',
    ko='Bluetooth 사운드',
    zhT='Bluetooth 音效',
    zhS='Bluetooth 音效',
    fi='Bluetooth-ääni',
    sv='Bluetooth-ljud',
    da='Bluetooth-lyd',
    no='Bluetooth-lyd',
    pl='Dźwięk Bluetooth',
    ptBR='Som Bluetooth',
    tr='Bluetooth sesi')
add('HINT_SOUND',
    ja='Bluetoothヘッドホンのサウンドです。処理は本体のプロセッサーが行いますが、バッテリーの消費はごくわずかです。「オリジナル」では音に手を加えません。',
    en="Sound for Bluetooth headphones. The console's processor does the work, at a very small cost in battery. Original leaves the sound untouched.",
    fr="Son pour les écouteurs Bluetooth. C'est le processeur de la console qui fait le travail, pour un très faible coût en autonomie. « Original » laisse le son tel quel.",
    es='Sonido para auriculares Bluetooth. Lo procesa la CPU de la consola, con un coste de batería muy pequeño. «Original» deja el sonido tal cual.',
    de='Klang für Bluetooth-Kopfhörer. Die Verarbeitung übernimmt der Prozessor der Konsole und kostet sehr wenig Akku. „Original“ lässt den Klang unverändert.',
    it='Suono per le cuffie Bluetooth. Lo elabora il processore della console, con un costo molto piccolo in batteria. «Originale» lascia il suono invariato.',
    nl="Geluid voor Bluetooth-koptelefoons. De processor van de console doet het werk, tegen een zeer kleine prijs in batterij. 'Origineel' laat het geluid ongewijzigd.",
    ptPT='Som para auscultadores Bluetooth. O processador da consola faz o trabalho, com um custo de bateria muito pequeno. «Original» deixa o som como está.',
    ru='Звук для Bluetooth-наушников. Обработку выполняет процессор консоли, это совсем немного расходует заряд. «Исходный» оставляет звук без изменений.',
    ko="Bluetooth 헤드폰의 사운드입니다. 본체의 프로세서가 처리하므로 배터리를 아주 조금 소모합니다. '기본'은 소리를 그대로 둡니다.",
    zhT='設定 Bluetooth 耳機的音效。由主機的處理器負責處理，只會極輕微地消耗電池。選擇「原始」則不改變聲音。',
    zhS='设置 Bluetooth 耳机的音效。由主机的处理器负责处理，只会极轻微地消耗电池。选择“原始”则不改变声音。',
    fi='Bluetooth-kuulokkeiden ääni. Konsolin suoritin tekee käsittelyn, mikä kuluttaa akkua vain hyvin vähän. Alkuperäinen jättää äänen koskemattomaksi.',
    sv='Ljud för Bluetooth-hörlurar. Konsolens processor gör jobbet, till en mycket liten kostnad i batteri. Original lämnar ljudet orört.',
    da='Lyd til Bluetooth-hovedtelefoner. Konsollens processor klarer arbejdet, til en meget lille pris i batteri. Original lader lyden være uændret.',
    no='Lyd for Bluetooth-hodetelefoner. Konsollens prosessor gjør jobben, til en veldig liten pris i batteri. Original lar lyden være uendret.',
    pl='Dźwięk dla słuchawek Bluetooth. Przetwarzaniem zajmuje się procesor konsoli, kosztem bardzo niewielkiego zużycia baterii. „Oryginalny” zostawia dźwięk bez zmian.',
    ptBR='Som para fones de ouvido Bluetooth. O processador do console faz o trabalho, com um custo de bateria muito pequeno. "Original" deixa o som como está.',
    tr='Bluetooth kulaklıklar için ses. İşi konsolun işlemcisi yapar, pilden çok küçük bir bedel alır. Orijinal, sesi olduğu gibi bırakır.')
add('ROW_CUSTOM',
    ja='カスタムイコライザー',
    en='Custom Equalizer',
    fr='Égaliseur personnalisé',
    es='Ecualizador personalizado',
    de='Benutzerdefinierter Equalizer',
    it='Equalizzatore personalizzato',
    nl='Aangepaste equalizer',
    ptPT='Equalizador personalizado',
    ru='Пользовательский эквалайзер',
    ko='사용자 이퀄라이저',
    zhT='自訂等化器',
    zhS='自定义均衡器',
    fi='Oma taajuuskorjain',
    sv='Egen utjämnare',
    da='Brugerdefineret equalizer',
    no='Egendefinert utjevning',
    pl='Własny korektor',
    ptBR='Equalizador personalizado',
    enGB='Custom Equaliser',
    tr='Özel ekolayzer')
add('HINT_CUSTOM',
    ja='5つの周波数帯の音量を自由に設定できます。Bluetoothのサウンドで「カスタム」を選んだときに使われます。',
    en='Set your own level for five frequency bands. Used when Custom is selected for Bluetooth Sound.',
    fr='Réglez vous-même le niveau de cinq bandes de fréquences. Utilisé lorsque « Personnalisé » est choisi pour le son Bluetooth.',
    es='Ajusta tú mismo el nivel de cinco bandas de frecuencia. Se usa al elegir «Personalizado» para el sonido Bluetooth.',
    de='Lege den Pegel von fünf Frequenzbändern selbst fest. Wird verwendet, wenn für den Bluetooth-Klang „Benutzerdefiniert“ gewählt ist.',
    it='Imposta tu il livello di cinque bande di frequenza. Usato quando si sceglie «Personalizzato» per il suono Bluetooth.',
    nl="Stel zelf het niveau van vijf frequentiebanden in. Wordt gebruikt als 'Aangepast' is gekozen voor het Bluetooth-geluid.",
    ptPT='Defina o nível de cinco bandas de frequência. Usado quando «Personalizado» está selecionado para o som Bluetooth.',
    ru='Настройте уровень пяти частотных полос. Используется, если для звука Bluetooth выбрано «Свой».',
    ko="다섯 개 주파수 대역의 레벨을 직접 설정합니다. Bluetooth 사운드에서 '사용자 지정'을 선택하면 사용됩니다.",
    zhT='自行設定五個頻段的音量。在 Bluetooth 音效選擇「自訂」時使用。',
    zhS='自行设置五个频段的音量。在 Bluetooth 音效选择“自定义”时使用。',
    fi='Säädä viiden taajuuskaistan tasot itse. Käytössä, kun Bluetooth-äänelle on valittu Oma.',
    sv='Ställ in nivån för fem frekvensband själv. Används när Egen är vald för Bluetooth-ljudet.',
    da='Indstil selv niveauet for fem frekvensbånd. Bruges, når Brugerdefineret er valgt til Bluetooth-lyden.',
    no='Still inn nivået for fem frekvensbånd selv. Brukes når Egendefinert er valgt for Bluetooth-lyden.',
    pl='Ustaw własny poziom pięciu pasm częstotliwości. Używane, gdy dla dźwięku Bluetooth wybrano „Własny”.',
    ptBR='Defina o nível de cinco faixas de frequência. Usado quando "Personalizado" está selecionado para o som Bluetooth.',
    tr='Beş frekans bandının seviyesini kendiniz ayarlayın. Bluetooth sesi için Özel seçildiğinde kullanılır.')
add('ROW_B1',
    ja='低音',
    en='Bass',
    fr='Graves',
    es='Graves',
    de='Bass',
    it='Bassi',
    nl='Bas',
    ptPT='Graves',
    ru='Низкие',
    ko='저음',
    zhT='低音',
    zhS='低音',
    fi='Basso',
    sv='Bas',
    da='Bas',
    no='Bass',
    pl='Niskie tony',
    ptBR='Graves',
    tr='Bas')
add('ROW_B2',
    ja='中低音',
    en='Low Mids',
    fr='Bas-médiums',
    es='Medios graves',
    de='Untere Mitten',
    it='Medio-bassi',
    nl='Laag-midden',
    ptPT='Médios-graves',
    ru='Нижняя середина',
    ko='중저음',
    zhT='中低音',
    zhS='中低音',
    fi='Alakeskiääni',
    sv='Lågmellan',
    da='Lav mellemtone',
    no='Lav mellomtone',
    pl='Niskie średnie',
    ptBR='Médios-graves',
    tr='Alt orta')
add('ROW_B3',
    ja='中音',
    en='Mids',
    fr='Médiums',
    es='Medios',
    de='Mitten',
    it='Medi',
    nl='Midden',
    ptPT='Médios',
    ru='Середина',
    ko='중음',
    zhT='中音',
    zhS='中音',
    fi='Keskiääni',
    sv='Mellanregister',
    da='Mellemtone',
    no='Mellomtone',
    pl='Średnie tony',
    ptBR='Médios',
    tr='Orta')
add('ROW_B4',
    ja='中高音',
    en='Upper Mids',
    fr='Haut-médiums',
    es='Medios agudos',
    de='Obere Mitten',
    it='Medio-alti',
    nl='Hoog-midden',
    ptPT='Médios-agudos',
    ru='Верхняя середина',
    ko='중고음',
    zhT='中高音',
    zhS='中高音',
    fi='Yläkeskiääni',
    sv='Högmellan',
    da='Høj mellemtone',
    no='Høy mellomtone',
    pl='Wysokie średnie',
    ptBR='Médios-agudos',
    tr='Üst orta')
add('ROW_B5',
    ja='高音',
    en='Treble',
    fr='Aigus',
    es='Agudos',
    de='Höhen',
    it='Alti',
    nl='Hoog',
    ptPT='Agudos',
    ru='Высокие',
    ko='고음',
    zhT='高音',
    zhS='高音',
    fi='Diskantti',
    sv='Diskant',
    da='Diskant',
    no='Diskant',
    pl='Wysokie tony',
    ptBR='Agudos',
    tr='Tiz')
add('HINT_BAND',
    ja='この帯域を上げ下げします。どれかの帯域が0 dBを超えると、音がひずまないよう全体の音量がその分下がります。',
    en="Raise or lower this band. While any band is above 0 dB, the overall level is lowered to match, so the sound doesn't distort.",
    fr='Monte ou baisse cette bande. Si une bande dépasse 0 dB, le niveau global est réduit d’autant pour éviter la distorsion.',
    es='Sube o baja esta banda. Si alguna banda supera 0 dB, el nivel general baja en la misma medida para que el sonido no se distorsione.',
    de='Hebt oder senkt dieses Band. Liegt ein Band über 0 dB, wird der Gesamtpegel entsprechend gesenkt, damit der Klang nicht verzerrt.',
    it='Alza o abbassa questa banda. Se una banda supera 0 dB, il livello generale viene ridotto di conseguenza per evitare distorsioni.',
    nl='Verhoog of verlaag deze band. Als een band boven 0 dB staat, wordt het totale niveau evenveel verlaagd, zodat het geluid niet vervormt.',
    ptPT='Aumenta ou diminui esta banda. Se alguma banda passar de 0 dB, o nível geral desce na mesma medida para o som não distorcer.',
    ru='Поднимает или опускает эту полосу. Если какая-либо полоса выше 0 dB, общий уровень снижается на ту же величину, чтобы звук не искажался.',
    ko='이 대역을 올리거나 내립니다. 어느 대역이든 0 dB를 넘으면 소리가 찌그러지지 않도록 전체 레벨이 그만큼 낮아집니다.',
    zhT='調高或調低此頻段。任一頻段高於 0 dB 時，整體音量會相應降低，避免聲音失真。',
    zhS='调高或调低此频段。任一频段高于 0 dB 时，整体音量会相应降低，避免声音失真。',
    fi='Nostaa tai laskee tätä kaistaa. Jos jokin kaista on yli 0 dB, kokonaistasoa lasketaan vastaavasti, jotta ääni ei säröydy.',
    sv='Höj eller sänk det här bandet. Om något band ligger över 0 dB sänks den totala nivån lika mycket, så att ljudet inte förvrängs.',
    da='Hæv eller sænk dette bånd. Hvis et bånd er over 0 dB, sænkes det samlede niveau tilsvarende, så lyden ikke forvrænges.',
    no='Hev eller senk dette båndet. Hvis et bånd er over 0 dB, senkes det totale nivået tilsvarende, så lyden ikke forvrenges.',
    pl='Podnieś lub obniż to pasmo. Gdy któreś pasmo przekracza 0 dB, ogólny poziom jest odpowiednio obniżany, aby dźwięk nie był zniekształcony.',
    ptBR='Aumenta ou diminui esta faixa. Se alguma faixa passar de 0 dB, o nível geral é reduzido na mesma medida para o som não distorcer.',
    tr="Bu bandı yükseltir veya alçaltır. Herhangi bir bant 0 dB'nin üzerindeyken ses bozulmasın diye genel seviye aynı oranda düşürülür.")
add('ITEM_ORIGINAL',
    ja='オリジナル',
    en='Original',
    fr='Original',
    es='Original',
    de='Original',
    it='Originale',
    nl='Origineel',
    ptPT='Original',
    ru='Исходный',
    ko='기본',
    zhT='原始',
    zhS='原始',
    fi='Alkuperäinen',
    sv='Original',
    da='Original',
    no='Original',
    pl='Oryginalny',
    ptBR='Original',
    tr='Orijinal')
add('ITEM_WARM',
    ja='ウォーム',
    en='Warm',
    fr='Chaleureux',
    es='Cálido',
    de='Warm',
    it='Caldo',
    nl='Warm',
    ptPT='Quente',
    ru='Тёплый',
    ko='따뜻하게',
    zhT='溫暖',
    zhS='温暖',
    fi='Lämmin',
    sv='Varm',
    da='Varm',
    no='Varm',
    pl='Ciepły',
    ptBR='Quente',
    tr='Sıcak')
add('ITEM_BRIGHT',
    ja='ブライト',
    en='Bright',
    fr='Brillant',
    es='Brillante',
    de='Hell',
    it='Brillante',
    nl='Helder',
    ptPT='Brilhante',
    ru='Яркий',
    ko='밝게',
    zhT='明亮',
    zhS='明亮',
    fi='Kirkas',
    sv='Ljus',
    da='Lys',
    no='Lys',
    pl='Jasny',
    ptBR='Brilhante',
    tr='Parlak')
add('ITEM_BASS',
    ja='低音強調',
    en='Bass Boost',
    fr='Basses renforcées',
    es='Refuerzo de graves',
    de='Bassverstärkung',
    it='Bassi potenziati',
    nl='Basversterking',
    ptPT='Reforço de graves',
    ru='Усиление басов',
    ko='저음 강화',
    zhT='低音增強',
    zhS='低音增强',
    fi='Bassokorostus',
    sv='Basförstärkning',
    da='Basforstærkning',
    no='Bassforsterkning',
    pl='Wzmocnienie basów',
    ptBR='Reforço de graves',
    tr='Bas güçlendirme')
add('ITEM_LIVELY',
    ja='メリハリ',
    en='Lively',
    fr='Dynamique',
    es='Dinámico',
    de='Lebendig',
    it='Vivace',
    nl='Levendig',
    ptPT='Vivo',
    ru='Живой',
    ko='생동감',
    zhT='生動',
    zhS='生动',
    fi='Eloisa',
    sv='Livlig',
    da='Livlig',
    no='Livlig',
    pl='Żywy',
    ptBR='Vivo',
    tr='Canlı')
add('ITEM_CUSTOM',
    ja='カスタム',
    en='Custom',
    fr='Personnalisé',
    es='Personalizado',
    de='Benutzerdefiniert',
    it='Personalizzato',
    nl='Aangepast',
    ptPT='Personalizado',
    ru='Свой',
    ko='사용자 지정',
    zhT='自訂',
    zhS='自定义',
    fi='Oma',
    sv='Egen',
    da='Brugerdefineret',
    no='Egendefinert',
    pl='Własny',
    ptBR='Personalizado',
    tr='Özel')


# The percentage texts follow the number style: "85 %" in fr/de/fi/sv/da/no,
# "%85" in tr, "85%" everywhere else.
def pct_text(lang, n):
    if lang in ('fr', 'de', 'fi', 'sv', 'da', 'no'):
        return '%d %%' % n
    if lang == 'tr':
        return '%%%d' % n
    return '%d%%' % n


add('ROW_CHANNELS',
    ja='Bluetoothのチャンネル',
    en='Bluetooth Channels',
    fr='Canaux Bluetooth',
    es='Canales Bluetooth',
    de='Bluetooth-Kanäle',
    it='Canali Bluetooth',
    nl='Bluetooth-kanalen',
    ptPT='Canais Bluetooth',
    ru='Каналы Bluetooth',
    ko='Bluetooth 채널',
    zhT='Bluetooth 聲道',
    zhS='Bluetooth 声道',
    fi='Bluetooth-kanavat',
    sv='Bluetooth-kanaler',
    da='Bluetooth-kanaler',
    no='Bluetooth-kanaler',
    pl='Kanały Bluetooth',
    ptBR='Canais Bluetooth',
    tr='Bluetooth kanalları')
add('HINT_CHANNELS',
    ja='「モノラル」は左右から同じ音を出します。イヤホンを片耳だけで使うときや、片方の耳が聞こえにくいときに便利です。「左右を入れ替え」は左右を入れ替えます。Bluetoothヘッドホンに送る音だけが変わります。',
    en='Mono plays the same sound on both sides, which helps if you wear one earbud or hear better on one side. Swap Left and Right exchanges the two sides. Only the sound sent to Bluetooth headphones changes.',
    fr="« Mono » joue le même son des deux côtés, ce qui aide si vous portez un seul écouteur ou entendez mieux d'un côté. « Inverser gauche et droite » échange les deux côtés. Seul le son envoyé aux écouteurs Bluetooth change.",
    es='«Mono» reproduce el mismo sonido por ambos lados, lo que ayuda si usas un solo auricular o oyes mejor por un lado. «Intercambiar izquierda y derecha» intercambia los dos lados. Solo cambia el sonido enviado a los auriculares Bluetooth.',
    de='„Mono“ gibt auf beiden Seiten denselben Ton aus, was hilft, wenn du nur einen Ohrhörer trägst oder auf einer Seite besser hörst. „Links und rechts tauschen“ vertauscht die beiden Seiten. Nur der an Bluetooth-Kopfhörer gesendete Ton ändert sich.',
    it='«Mono» riproduce lo stesso suono da entrambi i lati, utile se usi un solo auricolare o senti meglio da un lato. «Scambia sinistra e destra» scambia i due lati. Cambia solo il suono inviato alle cuffie Bluetooth.',
    nl="'Mono' speelt aan beide kanten hetzelfde geluid af, handig als je één oordopje draagt of aan één kant beter hoort. 'Links en rechts omwisselen' wisselt de twee kanten om. Alleen het geluid dat naar Bluetooth-koptelefoons wordt gestuurd verandert.",
    ptPT='«Mono» reproduz o mesmo som nos dois lados, o que ajuda se usar só um auricular ou ouvir melhor de um lado. «Trocar esquerda e direita» troca os dois lados. Só muda o som enviado para os auscultadores Bluetooth.',
    ru='«Моно» воспроизводит одинаковый звук с обеих сторон: удобно, если вы носите один наушник или лучше слышите одним ухом. «Поменять левый и правый» меняет стороны местами. Меняется только звук, передаваемый на Bluetooth-наушники.',
    ko="'모노'는 양쪽에서 같은 소리를 재생합니다. 이어폰을 한쪽만 끼거나 한쪽 귀가 더 잘 들릴 때 유용합니다. '좌우 바꾸기'는 좌우를 서로 바꿉니다. Bluetooth 헤드폰으로 보내는 소리만 바뀝니다.",
    zhT='「單聲道」會從左右兩側播放相同的聲音，適合只戴一邊耳機或單側聽力較好時使用。「左右對調」會交換左右兩側。只會改變傳送到 Bluetooth 耳機的聲音。',
    zhS='“单声道”会从左右两侧播放相同的声音，适合只戴一边耳机或单侧听力较好时使用。“左右互换”会交换左右两侧。只会改变发送到 Bluetooth 耳机的声音。',
    fi='Mono toistaa saman äänen molemmilla puolilla, mikä auttaa, jos käytät vain yhtä nappikuuloketta tai kuulet paremmin toisella puolella. Vaihda vasen ja oikea vaihtaa puolet keskenään. Vain Bluetooth-kuulokkeille lähetettävä ääni muuttuu.',
    sv='Mono spelar samma ljud på båda sidor, vilket hjälper om du bär en enda öronsnäcka eller hör bättre på ena sidan. Byt vänster och höger byter sida på ljudet. Bara ljudet som skickas till Bluetooth-hörlurar ändras.',
    da='Mono afspiller den samme lyd på begge sider, hvilket hjælper, hvis du kun bruger én ørepropp eller hører bedst på den ene side. Byt venstre og højre bytter de to sider. Kun lyden, der sendes til Bluetooth-hovedtelefoner, ændres.',
    no='Mono spiller samme lyd på begge sider, noe som hjelper hvis du bruker bare én ørepropp eller hører bedre på den ene siden. Bytt venstre og høyre bytter de to sidene. Bare lyden som sendes til Bluetooth-hodetelefoner endres.',
    pl='„Mono” odtwarza ten sam dźwięk po obu stronach, co pomaga, gdy nosisz jedną słuchawkę lub lepiej słyszysz na jedno ucho. „Zamień lewy i prawy” zamienia strony miejscami. Zmienia się tylko dźwięk wysyłany do słuchawek Bluetooth.',
    ptBR='"Mono" reproduz o mesmo som nos dois lados, o que ajuda se você usa só um fone de ouvido ou ouve melhor de um lado. "Trocar esquerda e direita" troca os dois lados. Só muda o som enviado para os fones de ouvido Bluetooth.',
    tr='Mono, her iki tarafta aynı sesi çalar; tek kulaklık takıyorsanız ya da bir kulağınız daha iyi duyuyorsa işe yarar. Sağ ve solu değiştir iki tarafı birbiriyle değiştirir. Yalnızca Bluetooth kulaklıklara gönderilen ses değişir.')
add('ITEM_STEREO',
    ja='ステレオ', en='Stereo', fr='Stéréo', es='Estéreo', de='Stereo', it='Stereo', nl='Stereo',
    ptPT='Estéreo', ru='Стерео', ko='스테레오', zhT='立體聲', zhS='立体声', fi='Stereo', sv='Stereo',
    da='Stereo', no='Stereo', pl='Stereo', ptBR='Estéreo', tr='Stereo')
add('ITEM_MONO',
    ja='モノラル', en='Mono', fr='Mono', es='Mono', de='Mono', it='Mono', nl='Mono',
    ptPT='Mono', ru='Моно', ko='모노', zhT='單聲道', zhS='单声道', fi='Mono', sv='Mono',
    da='Mono', no='Mono', pl='Mono', ptBR='Mono', tr='Mono')
add('ITEM_SWAP',
    ja='左右を入れ替え',
    en='Swap Left and Right',
    fr='Inverser gauche et droite',
    es='Intercambiar izquierda y derecha',
    de='Links und rechts tauschen',
    it='Scambia sinistra e destra',
    nl='Links en rechts omwisselen',
    ptPT='Trocar esquerda e direita',
    ru='Поменять левый и правый',
    ko='좌우 바꾸기',
    zhT='左右對調',
    zhS='左右互换',
    fi='Vaihda vasen ja oikea',
    sv='Byt vänster och höger',
    da='Byt venstre og højre',
    no='Bytt venstre og høyre',
    pl='Zamień lewy i prawy',
    ptBR='Trocar esquerda e direita',
    tr='Sağ ve solu değiştir')
add('ROW_BALANCE',
    ja='Bluetoothのバランス',
    en='Bluetooth Balance',
    fr='Balance Bluetooth',
    es='Balance Bluetooth',
    de='Bluetooth-Balance',
    it='Bilanciamento Bluetooth',
    nl='Bluetooth-balans',
    ptPT='Equilíbrio Bluetooth',
    ru='Баланс Bluetooth',
    ko='Bluetooth 밸런스',
    zhT='Bluetooth 平衡',
    zhS='Bluetooth 平衡',
    fi='Bluetooth-tasapaino',
    sv='Bluetooth-balans',
    da='Bluetooth-balance',
    no='Bluetooth-balanse',
    pl='Balans Bluetooth',
    ptBR='Balanço Bluetooth',
    tr='Bluetooth dengesi')
add('HINT_BALANCE',
    ja='片側を小さくして、音を左右どちらかに寄せます。「中央」では音に手を加えません。L4とR4では片側だけから音が出ます。',
    en='Shifts the sound toward one side by lowering the other. Center leaves it as it is; L4 and R4 play one side only.',
    enGB='Shifts the sound toward one side by lowering the other. Centre leaves it as it is; L4 and R4 play one side only.',
    fr="Déplace le son vers un côté en baissant l'autre. « Centre » laisse le son tel quel ; L4 et R4 ne jouent que d'un côté.",
    es='Desplaza el sonido hacia un lado bajando el otro. «Centro» deja el sonido tal cual; L4 y R4 reproducen solo por un lado.',
    de='Verschiebt den Ton zu einer Seite, indem die andere leiser wird. „Mitte“ lässt den Ton unverändert; L4 und R4 geben nur eine Seite aus.',
    it='Sposta il suono verso un lato abbassando l\'altro. «Centro» lascia il suono invariato; L4 e R4 riproducono un solo lato.',
    nl="Verschuift het geluid naar één kant door de andere zachter te zetten. 'Midden' laat het geluid ongewijzigd; L4 en R4 spelen maar één kant af.",
    ptPT='Desloca o som para um lado baixando o outro. «Centro» deixa o som como está; L4 e R4 reproduzem só um lado.',
    ru='Смещает звук в одну сторону, уменьшая громкость другой. «По центру» оставляет звук без изменений; L4 и R4 воспроизводят звук только с одной стороны.',
    ko="한쪽 소리를 줄여서 소리를 다른 쪽으로 치우치게 합니다. '중앙'은 소리를 그대로 둡니다. L4와 R4는 한쪽에서만 소리가 납니다.",
    zhT='降低一側的音量，讓聲音偏向另一側。「中央」不改變聲音；L4 和 R4 只會從單側播放。',
    zhS='降低一侧的音量，让声音偏向另一侧。“中央”不改变声音；L4 和 R4 只会从单侧播放。',
    fi='Siirtää äänen toiselle puolelle hiljentämällä toista. Keskellä jättää äänen ennalleen; L4 ja R4 toistavat vain toisella puolella.',
    sv='Flyttar ljudet åt ena sidan genom att sänka den andra. Mitten lämnar ljudet orört; L4 och R4 spelar bara på en sida.',
    da='Flytter lyden mod den ene side ved at sænke den anden. Midten lader lyden være uændret; L4 og R4 afspiller kun på én side.',
    no='Flytter lyden mot den ene siden ved å senke den andre. Midten lar lyden være uendret; L4 og R4 spiller bare på én side.',
    pl='Przesuwa dźwięk na jedną stronę, ściszając drugą. „Środek” zostawia dźwięk bez zmian; L4 i R4 odtwarzają dźwięk tylko po jednej stronie.',
    ptBR='Desloca o som para um lado reduzindo o outro. "Centro" deixa o som como está; L4 e R4 reproduzem só um lado.',
    tr='Diğer tarafı kısarak sesi bir yana kaydırır. Orta sesi olduğu gibi bırakır; L4 ve R4 yalnızca bir tarafı çalar.')
add('ITEM_CENTRE',
    ja='中央', en='Center', fr='Centre', es='Centro', de='Mitte', it='Centro', nl='Midden',
    ptPT='Centro', ru='По центру', ko='중앙', zhT='中央', zhS='中央', fi='Keskellä', sv='Mitten',
    da='Midten', no='Midten', pl='Środek', ptBR='Centro', enGB='Centre', tr='Orta')
add('ROW_LATENCY',
    ja='Bluetoothの遅延',
    en='Bluetooth Latency',
    fr='Latence Bluetooth',
    es='Latencia Bluetooth',
    de='Bluetooth-Latenz',
    it='Latenza Bluetooth',
    nl='Bluetooth-vertraging',
    ptPT='Latência Bluetooth',
    ru='Задержка Bluetooth',
    ko='Bluetooth 지연',
    zhT='Bluetooth 延遲',
    zhS='Bluetooth 延迟',
    fi='Bluetooth-viive',
    sv='Bluetooth-fördröjning',
    da='Bluetooth-forsinkelse',
    no='Bluetooth-forsinkelse',
    pl='Opóźnienie Bluetooth',
    ptBR='Latência Bluetooth',
    tr='Bluetooth gecikmesi')
add('HINT_LATENCY',
    ja='無線が混み合っているとき、音が映像からどれだけ遅れてよいかを決めます。「低」と「最小」はゲーム向けです。「最小」は、Wi-Fiが混み合っているときや電波の悪い場所で音が一瞬途切れることがあります。「標準」は本体本来の動作です。',
    en="How far the sound may lag behind the picture when the radio is busy. Low and Lowest suit games. Lowest may cause short gaps when Wi-Fi is busy or where reception is poor. Standard is the console's own.",
    fr="Fixe le retard que le son peut prendre sur l'image quand la liaison radio est encombrée. « Faible » et « La plus faible » conviennent aux jeux. « La plus faible » peut provoquer de brèves coupures quand le Wi-Fi est très sollicité ou si la réception est mauvaise. « Standard » est le réglage d'origine de la console.",
    es='Define cuánto puede retrasarse el sonido respecto a la imagen cuando la radio está ocupada. «Baja» y «La más baja» van bien para juegos. «La más baja» puede causar cortes breves cuando el Wi-Fi está muy usado o la recepción es mala. «Estándar» es el ajuste original de la consola.',
    de='Legt fest, wie weit der Ton hinter dem Bild zurückbleiben darf, wenn die Funkverbindung ausgelastet ist. „Niedrig“ und „Am niedrigsten“ eignen sich für Spiele. „Am niedrigsten“ kann zu kurzen Aussetzern führen, wenn das WLAN stark genutzt wird oder der Empfang schlecht ist. „Standard“ ist die ursprüngliche Einstellung der Konsole.',
    it='Stabilisce quanto il suono può restare indietro rispetto all\'immagine quando la radio è occupata. «Bassa» e «La più bassa» vanno bene per i giochi. «La più bassa» può causare brevi interruzioni quando il Wi-Fi è molto usato o la ricezione è scarsa. «Standard» è l\'impostazione originale della console.',
    nl="Bepaalt hoever het geluid achter het beeld aan mag lopen als de radio druk is. 'Laag' en 'Laagst' passen bij games. 'Laagst' kan korte onderbrekingen geven als wifi druk is of de ontvangst slecht is. 'Standaard' is de oorspronkelijke instelling van de console.",
    ptPT='Define quanto o som se pode atrasar em relação à imagem quando a ligação sem fios está ocupada. «Baixa» e «A mais baixa» servem para jogos. «A mais baixa» pode causar falhas breves quando o Wi-Fi está muito ocupado ou a receção é fraca. «Padrão» é a definição original da consola.',
    ru='Определяет, насколько звук может отставать от изображения, когда радиоканал загружен. «Низкая» и «Минимальная» подходят для игр. «Минимальная» может вызывать короткие пропадания звука, когда Wi-Fi загружен или приём плохой. «Стандарт» — исходная настройка консоли.',
    ko="무선이 혼잡할 때 소리가 화면보다 얼마나 늦어도 되는지 정합니다. '낮음'과 '가장 낮음'은 게임에 적합합니다. '가장 낮음'은 Wi-Fi가 혼잡하거나 수신이 나쁠 때 소리가 잠깐 끊길 수 있습니다. '표준'은 본체 본래의 설정입니다.",
    zhT='設定無線訊號繁忙時，聲音可落後畫面多少。「低」和「最低」適合遊戲。Wi-Fi 繁忙或收訊不佳時，「最低」可能造成聲音短暫中斷。「標準」是主機原本的設定。',
    zhS='设置无线信号繁忙时，声音可落后画面多少。“低”和“最低”适合游戏。Wi-Fi 繁忙或信号不佳时，“最低”可能造成声音短暂中断。“标准”是主机原本的设置。',
    fi='Määrittää, kuinka paljon ääni saa jäädä kuvasta jälkeen, kun radioyhteys on ruuhkainen. Pieni ja Pienin sopivat peleihin. Pienin voi aiheuttaa lyhyitä katkoksia, kun Wi-Fi on kuormitettu tai kuuluvuus on huono. Vakio on konsolin alkuperäinen asetus.',
    sv='Bestämmer hur mycket ljudet får släpa efter bilden när radion är upptagen. Låg och Lägst passar för spel. Lägst kan ge korta avbrott när Wi-Fi är hårt belastat eller mottagningen är dålig. Standard är konsolens ursprungliga inställning.',
    da='Bestemmer, hvor meget lyden må halte bagefter billedet, når radioen er travl. Lav og Lavest egner sig til spil. Lavest kan give korte udfald, når Wi-Fi er travlt, eller modtagelsen er dårlig. Standard er konsollens oprindelige indstilling.',
    no='Bestemmer hvor mye lyden får henge etter bildet når radioen er travel. Lav og Lavest passer for spill. Lavest kan gi korte avbrudd når Wi-Fi er travelt eller mottaket er dårlig. Standard er konsollens opprinnelige innstilling.',
    pl='Określa, jak bardzo dźwięk może opóźniać się względem obrazu, gdy łącze radiowe jest zajęte. „Niskie” i „Najniższe” pasują do gier. „Najniższe” może powodować krótkie przerwy, gdy Wi-Fi jest mocno obciążone lub zasięg jest słaby. „Standardowo” to pierwotne ustawienie konsoli.',
    ptBR='Define quanto o som pode atrasar em relação à imagem quando a conexão sem fio está ocupada. "Baixa" e "A mais baixa" servem para jogos. "A mais baixa" pode causar falhas curtas quando o Wi-Fi está muito ocupado ou a recepção é ruim. "Padrão" é a configuração original do console.',
    tr='Telsiz yoğunken sesin görüntünün ne kadar gerisinde kalabileceğini belirler. Düşük ve En düşük oyunlara uygundur. En düşük, Wi-Fi yoğun olduğunda veya çekim zayıf olduğunda kısa kesintilere yol açabilir. Standart, konsolun özgün ayarıdır.')
add('ITEM_STANDARD',
    ja='標準', en='Standard', fr='Standard', es='Estándar', de='Standard', it='Standard', nl='Standaard',
    ptPT='Padrão', ru='Стандарт', ko='표준', zhT='標準', zhS='标准', fi='Vakio', sv='Standard',
    da='Standard', no='Standard', pl='Standardowo', ptBR='Padrão', tr='Standart')
add('ITEM_LOW',
    ja='低', en='Low', fr='Faible', es='Baja', de='Niedrig', it='Bassa', nl='Laag',
    ptPT='Baixa', ru='Низкая', ko='낮음', zhT='低', zhS='低', fi='Pieni', sv='Låg',
    da='Lav', no='Lav', pl='Niskie', ptBR='Baixa', tr='Düşük')
add('ITEM_LOWEST',
    ja='最小', en='Lowest', fr='La plus faible', es='La más baja', de='Am niedrigsten', it='La più bassa', nl='Laagst',
    ptPT='A mais baixa', ru='Минимальная', ko='가장 낮음', zhT='最低', zhS='最低', fi='Pienin', sv='Lägst',
    da='Lavest', no='Lavest', pl='Najniższe', ptBR='A mais baixa', tr='En düşük')
add('ROW_QUALITY',
    ja='Bluetoothのオーディオ品質',
    en='Bluetooth Audio Quality',
    fr='Qualité audio Bluetooth',
    es='Calidad de audio Bluetooth',
    de='Bluetooth-Audioqualität',
    it='Qualità audio Bluetooth',
    nl='Bluetooth-audiokwaliteit',
    ptPT='Qualidade de áudio Bluetooth',
    ru='Качество звука Bluetooth',
    ko='Bluetooth 오디오 품질',
    zhT='Bluetooth 音訊品質',
    zhS='Bluetooth 音频质量',
    fi='Bluetooth-äänenlaatu',
    sv='Bluetooth-ljudkvalitet',
    da='Bluetooth-lydkvalitet',
    no='Bluetooth-lydkvalitet',
    pl='Jakość dźwięku Bluetooth',
    ptBR='Qualidade de áudio Bluetooth',
    tr='Bluetooth ses kalitesi')
add('ITEM_Q_BALANCED',
    ja='バランス重視', en='Balanced', fr='Équilibrée', es='Equilibrada', de='Ausgewogen', it='Bilanciata', nl='Gebalanceerd',
    ptPT='Equilibrada', ru='Сбалансированное', ko='균형', zhT='均衡', zhS='均衡', fi='Tasapainoinen', sv='Balanserad',
    da='Afbalanceret', no='Balansert', pl='Zrównoważona', ptBR='Equilibrada', tr='Dengeli')
add('ITEM_Q_STABLE',
    ja='安定', en='Stable', fr='Stable', es='Estable', de='Stabil', it='Stabile', nl='Stabiel',
    ptPT='Estável', ru='Стабильное', ko='안정', zhT='穩定', zhS='稳定', fi='Vakaa', sv='Stabil',
    da='Stabil', no='Stabil', pl='Stabilna', ptBR='Estável', tr='Kararlı')
add('ITEM_Q_HIGH',
    ja='高', en='High', fr='Élevée', es='Alta', de='Hoch', it='Alta', nl='Hoog',
    ptPT='Alta', ru='Высокое', ko='높음', zhT='高', zhS='高', fi='Korkea', sv='Hög',
    da='Høj', no='Høy', pl='Wysoka', ptBR='Alta', tr='Yüksek')
add('ITEM_Q_HIGHEST',
    ja='最高（試験的）', en='Highest (Experimental)', fr='La plus élevée (expérimentale)', es='La más alta (experimental)',
    de='Am höchsten (experimentell)', it='La più alta (sperimentale)', nl='Hoogst (experimenteel)',
    ptPT='A mais alta (experimental)', ru='Максимальное (экспериментальное)', ko='가장 높음(실험적)', zhT='最高（實驗性）',
    zhS='最高（实验性）', fi='Korkein (kokeellinen)', sv='Högst (experimentell)', da='Højest (eksperimentel)',
    no='Høyest (eksperimentell)', pl='Najwyższa (eksperymentalna)', ptBR='A mais alta (experimental)',
    tr='En yüksek (deneysel)')
add('ROW_VOLCURVE',
    ja='Bluetoothのボリューム特性',
    en='Bluetooth Volume Curve',
    fr='Courbe de volume Bluetooth',
    es='Curva de volumen Bluetooth',
    de='Bluetooth-Lautstärkekurve',
    it='Curva del volume Bluetooth',
    nl='Bluetooth-volumecurve',
    ptPT='Curva de volume Bluetooth',
    ru='Кривая громкости Bluetooth',
    ko='Bluetooth 음량 곡선',
    zhT='Bluetooth 音量曲線',
    zhS='Bluetooth 音量曲线',
    fi='Bluetooth-äänenvoimakkuuskäyrä',
    sv='Bluetooth-volymkurva',
    da='Bluetooth-lydstyrkekurve',
    no='Bluetooth-volumkurve',
    pl='Krzywa głośności Bluetooth',
    ptBR='Curva de volume Bluetooth',
    tr='Bluetooth ses seviyesi eğrisi')
add('HINT_VOLCURVE',
    ja='本体のボリュームの段階が、Bluetoothヘッドホンにどう伝わるかを変えます。「小音量を細かく」は小さい段階を静かに、間隔を細かくし、「小音量を大きく」は小さい段階を大きくします。本体から音量を決められるヘッドホンだけに効きます。次に音量を変えたときから反映されます。',
    en="Changes how the console's volume steps are sent to Bluetooth headphones. Finer at Low Volume makes the low steps quieter and closer together; Louder at Low Volume makes them louder. Only headphones that take their volume from the console are affected. Applies from the next volume change.",
    fr="Change la façon dont les niveaux de volume de la console sont envoyés aux écouteurs Bluetooth. « Plus fin à faible volume » rend les premiers niveaux plus faibles et plus rapprochés ; « Plus fort à faible volume » les rend plus forts. Seuls les écouteurs qui prennent leur volume de la console sont concernés. S'applique à partir du prochain changement de volume.",
    es='Cambia cómo se envían a los auriculares Bluetooth los niveles de volumen de la consola. «Más fino con volumen bajo» hace los primeros niveles más suaves y más juntos; «Más alto con volumen bajo» los hace más fuertes. Solo afecta a los auriculares que toman su volumen de la consola. Se aplica desde el siguiente cambio de volumen.',
    de='Ändert, wie die Lautstärkestufen der Konsole an Bluetooth-Kopfhörer gesendet werden. „Feiner bei niedriger Lautstärke“ macht die unteren Stufen leiser und enger beieinander; „Lauter bei niedriger Lautstärke“ macht sie lauter. Betroffen sind nur Kopfhörer, die ihre Lautstärke von der Konsole übernehmen. Gilt ab der nächsten Lautstärkeänderung.',
    it='Cambia il modo in cui i livelli di volume della console vengono inviati alle cuffie Bluetooth. «Più fine a volume basso» rende i primi livelli più bassi e più ravvicinati; «Più alto a volume basso» li rende più forti. Riguarda solo le cuffie che prendono il volume dalla console. Si applica dalla prossima modifica del volume.',
    nl="Verandert hoe de volumestappen van de console naar Bluetooth-koptelefoons worden gestuurd. 'Fijner bij laag volume' maakt de lage stappen zachter en dichter bij elkaar; 'Luider bij laag volume' maakt ze luider. Alleen koptelefoons die hun volume van de console overnemen worden beïnvloed. Geldt vanaf de volgende volumewijziging.",
    ptPT='Altera a forma como os níveis de volume da consola são enviados para os auscultadores Bluetooth. «Mais fino com volume baixo» torna os primeiros níveis mais suaves e mais próximos; «Mais alto com volume baixo» torna-os mais fortes. Afeta apenas os auscultadores que recebem o volume da consola. Aplica-se a partir da próxima alteração de volume.',
    ru='Меняет то, как уровни громкости консоли передаются на Bluetooth-наушники. «Точнее на малой громкости» делает первые уровни тише и ближе друг к другу; «Громче на малой громкости» делает их громче. Влияет только на наушники, которые берут громкость с консоли. Действует со следующего изменения громкости.',
    ko="본체의 음량 단계가 Bluetooth 헤드폰에 전달되는 방식을 바꿉니다. '낮은 음량에서 세밀하게'는 낮은 단계를 더 조용하고 촘촘하게, '낮은 음량에서 크게'는 더 크게 만듭니다. 본체에서 음량을 정하는 헤드폰에만 적용됩니다. 다음에 음량을 바꿀 때부터 적용됩니다.",
    zhT='改變主機的音量階段傳送到 Bluetooth 耳機的方式。「低音量時更細緻」會讓低階段更小聲、間隔更密；「低音量時更大聲」則讓低階段更大聲。只影響由主機決定音量的耳機。從下次調整音量時生效。',
    zhS='改变主机的音量档位发送到 Bluetooth 耳机的方式。“低音量时更细致”会让低档位更小声、间隔更密；“低音量时更大声”则让低档位更大声。只影响由主机决定音量的耳机。从下次调整音量时生效。',
    fi='Muuttaa tapaa, jolla konsolin äänenvoimakkuusportaat lähetetään Bluetooth-kuulokkeille. Hienompi hiljaisella tekee alimmista portaista hiljaisempia ja tiheämpiä; Kovempi hiljaisella tekee niistä kovempia. Vaikuttaa vain kuulokkeisiin, jotka ottavat äänenvoimakkuuden konsolilta. Tulee voimaan seuraavasta äänenvoimakkuuden muutoksesta.',
    sv='Ändrar hur konsolens volymsteg skickas till Bluetooth-hörlurar. Finare vid låg volym gör de lägsta stegen tystare och tätare; Högre vid låg volym gör dem högre. Påverkar bara hörlurar som tar sin volym från konsolen. Gäller från nästa volymändring.',
    da='Ændrer, hvordan konsollens lydstyrketrin sendes til Bluetooth-hovedtelefoner. Finere ved lav lydstyrke gør de laveste trin svagere og tættere; Højere ved lav lydstyrke gør dem kraftigere. Påvirker kun hovedtelefoner, der får deres lydstyrke fra konsollen. Gælder fra næste ændring af lydstyrken.',
    no='Endrer hvordan konsollens volumtrinn sendes til Bluetooth-hodetelefoner. Finere ved lavt volum gjør de laveste trinnene svakere og tettere; Høyere ved lavt volum gjør dem sterkere. Påvirker bare hodetelefoner som henter volumet fra konsollen. Gjelder fra neste volumendring.',
    pl='Zmienia sposób wysyłania stopni głośności konsoli do słuchawek Bluetooth. „Dokładniej przy niskiej głośności” czyni najniższe stopnie cichszymi i gęstszymi; „Głośniej przy niskiej głośności” czyni je głośniejszymi. Dotyczy tylko słuchawek, które biorą głośność z konsoli. Obowiązuje od następnej zmiany głośności.',
    ptBR='Muda a forma como os níveis de volume do console são enviados para os fones de ouvido Bluetooth. "Mais fino em volume baixo" deixa os primeiros níveis mais suaves e mais próximos; "Mais alto em volume baixo" os deixa mais fortes. Afeta apenas os fones de ouvido que recebem o volume do console. Vale a partir da próxima mudança de volume.',
    tr='Konsolun ses seviyesi basamaklarının Bluetooth kulaklıklara nasıl gönderileceğini değiştirir. Düşük seste daha ince, alt basamakları daha kısık ve birbirine yakın yapar; Düşük seste daha yüksek, onları daha yüksek yapar. Yalnızca sesi konsoldan alan kulaklıkları etkiler. Bir sonraki ses değişikliğinden itibaren geçerli olur.')
add('ITEM_VC_FINER',
    ja='小音量を細かく',
    en='Finer at Low Volume',
    fr='Plus fin à faible volume',
    es='Más fino con volumen bajo',
    de='Feiner bei niedriger Lautstärke',
    it='Più fine a volume basso',
    nl='Fijner bij laag volume',
    ptPT='Mais fino com volume baixo',
    ru='Точнее на малой громкости',
    ko='낮은 음량에서 세밀하게',
    zhT='低音量時更細緻',
    zhS='低音量时更细致',
    fi='Hienompi hiljaisella',
    sv='Finare vid låg volym',
    da='Finere ved lav lydstyrke',
    no='Finere ved lavt volum',
    pl='Dokładniej przy niskiej głośności',
    ptBR='Mais fino em volume baixo',
    tr='Düşük seste daha ince')
add('ITEM_VC_LOUDER',
    ja='小音量を大きく',
    en='Louder at Low Volume',
    fr='Plus fort à faible volume',
    es='Más alto con volumen bajo',
    de='Lauter bei niedriger Lautstärke',
    it='Più alto a volume basso',
    nl='Luider bij laag volume',
    ptPT='Mais alto com volume baixo',
    ru='Громче на малой громкости',
    ko='낮은 음량에서 크게',
    zhT='低音量時更大聲',
    zhS='低音量时更大声',
    fi='Kovempi hiljaisella',
    sv='Högre vid låg volym',
    da='Højere ved lav lydstyrke',
    no='Høyere ved lavt volum',
    pl='Głośniej przy niskiej głośności',
    ptBR='Mais alto em volume baixo',
    tr='Düşük seste daha yüksek')
add('ROW_VOLMAX',
    ja='Bluetoothの最大ボリューム',
    en='Maximum Bluetooth Volume',
    fr='Volume Bluetooth maximal',
    es='Volumen máximo de Bluetooth',
    de='Maximale Bluetooth-Lautstärke',
    it='Volume massimo Bluetooth',
    nl='Maximaal Bluetooth-volume',
    ptPT='Volume máximo de Bluetooth',
    ru='Максимальная громкость Bluetooth',
    ko='Bluetooth 최대 음량',
    zhT='Bluetooth 最大音量',
    zhS='Bluetooth 最大音量',
    fi='Bluetoothin enimmäisäänenvoimakkuus',
    sv='Högsta Bluetooth-volym',
    da='Maksimal Bluetooth-lydstyrke',
    no='Maksimalt Bluetooth-volum',
    pl='Maksymalna głośność Bluetooth',
    ptBR='Volume máximo de Bluetooth',
    tr='Bluetooth en yüksek ses seviyesi')
add('HINT_VOLMAX',
    ja='本体がBluetoothヘッドホンに設定する最大のボリュームを、ヘッドホンの最大値に対する割合で指定します。本体から音量を決められるヘッドホンだけに効きます。次に音量を変えたときから反映されます。',
    en='The highest volume the console will set on Bluetooth headphones, as a share of their full volume. Only headphones that take their volume from the console are affected. Applies from the next volume change.',
    fr="Le volume le plus élevé que la console règle sur les écouteurs Bluetooth, en part de leur volume maximal. Seuls les écouteurs qui prennent leur volume de la console sont concernés. S'applique à partir du prochain changement de volume.",
    es='El volumen más alto que la consola fijará en los auriculares Bluetooth, como parte de su volumen máximo. Solo afecta a los auriculares que toman su volumen de la consola. Se aplica desde el siguiente cambio de volumen.',
    de='Die höchste Lautstärke, die die Konsole an Bluetooth-Kopfhörern einstellt, als Anteil ihrer vollen Lautstärke. Betroffen sind nur Kopfhörer, die ihre Lautstärke von der Konsole übernehmen. Gilt ab der nächsten Lautstärkeänderung.',
    it='Il volume più alto che la console imposta sulle cuffie Bluetooth, come quota del loro volume massimo. Riguarda solo le cuffie che prendono il volume dalla console. Si applica dalla prossima modifica del volume.',
    nl='Het hoogste volume dat de console op Bluetooth-koptelefoons instelt, als deel van hun maximale volume. Alleen koptelefoons die hun volume van de console overnemen worden beïnvloed. Geldt vanaf de volgende volumewijziging.',
    ptPT='O volume mais alto que a consola define nos auscultadores Bluetooth, como parte do volume máximo deles. Afeta apenas os auscultadores que recebem o volume da consola. Aplica-se a partir da próxima alteração de volume.',
    ru='Наибольшая громкость, которую консоль устанавливает на Bluetooth-наушниках, как доля от их полной громкости. Влияет только на наушники, которые берут громкость с консоли. Действует со следующего изменения громкости.',
    ko='본체가 Bluetooth 헤드폰에 설정하는 가장 높은 음량을, 헤드폰 최대 음량에 대한 비율로 정합니다. 본체에서 음량을 정하는 헤드폰에만 적용됩니다. 다음에 음량을 바꿀 때부터 적용됩니다.',
    zhT='設定主機對 Bluetooth 耳機設定的最高音量，以耳機最大音量的比例表示。只影響由主機決定音量的耳機。從下次調整音量時生效。',
    zhS='设置主机对 Bluetooth 耳机设置的最高音量，以耳机最大音量的比例表示。只影响由主机决定音量的耳机。从下次调整音量时生效。',
    fi='Suurin äänenvoimakkuus, jonka konsoli asettaa Bluetooth-kuulokkeille, osuutena niiden täydestä äänenvoimakkuudesta. Vaikuttaa vain kuulokkeisiin, jotka ottavat äänenvoimakkuuden konsolilta. Tulee voimaan seuraavasta äänenvoimakkuuden muutoksesta.',
    sv='Den högsta volym som konsolen ställer in på Bluetooth-hörlurar, som en andel av deras fulla volym. Påverkar bara hörlurar som tar sin volym från konsolen. Gäller från nästa volymändring.',
    da='Den højeste lydstyrke, konsollen sætter på Bluetooth-hovedtelefoner, som en andel af deres fulde lydstyrke. Påvirker kun hovedtelefoner, der får deres lydstyrke fra konsollen. Gælder fra næste ændring af lydstyrken.',
    no='Det høyeste volumet konsollen stiller inn på Bluetooth-hodetelefoner, som en andel av fullt volum. Påvirker bare hodetelefoner som henter volumet fra konsollen. Gjelder fra neste volumendring.',
    pl='Najwyższa głośność, jaką konsola ustawia w słuchawkach Bluetooth, jako część ich pełnej głośności. Dotyczy tylko słuchawek, które biorą głośność z konsoli. Obowiązuje od następnej zmiany głośności.',
    ptBR='O volume mais alto que o console define nos fones de ouvido Bluetooth, como parte do volume máximo deles. Afeta apenas os fones de ouvido que recebem o volume do console. Vale a partir da próxima mudança de volume.',
    tr='Konsolun Bluetooth kulaklıklarda ayarlayacağı en yüksek ses seviyesi, kulaklığın tam ses seviyesinin bir oranı olarak. Yalnızca sesi konsoldan alan kulaklıkları etkiler. Bir sonraki ses değişikliğinden itibaren geçerli olur.')
for _pct in (100, 90, 80, 70, 60, 50):
    add('ITEM_MAX_%d' % _pct, **{l: pct_text(l, _pct) for l in LANGS})
add('ROW_CTRL',
    ja='ワイヤレスコントローラーの応答性',
    en='Wireless Controller Response',
    fr='Réactivité des manettes sans fil',
    es='Respuesta de los mandos inalámbricos',
    de='Reaktion drahtloser Controller',
    it='Reattività dei controller wireless',
    nl='Reactie van draadloze controllers',
    ptPT='Resposta dos comandos sem fios',
    ru='Отклик беспроводных контроллеров',
    ko='무선 컨트롤러 반응',
    zhT='無線控制器反應速度',
    zhS='无线控制器响应速度',
    fi='Langattomien ohjainten vasteaika',
    sv='Svarstid för trådlösa handkontroller',
    da='Respons for trådløse controllere',
    no='Respons for trådløse kontrollere',
    pl='Reakcja kontrolerów bezprzewodowych',
    ptBR='Resposta dos controles sem fio',
    tr='Kablosuz kontrol cihazı yanıtı')
add('HINT_CTRL',
    ja='「高速」にすると、本体が対応しているコントローラーの入力が、より頻繁に送られます。コントローラーのバッテリーを多く消費します。次にコントローラーが接続されたときから反映されます。',
    en='Fast lets the controllers the console knows send their input more often. It uses more controller battery, and applies from the next time a controller connects.',
    fr="« Rapide » permet aux manettes que la console connaît d'envoyer leurs commandes plus souvent. Cela consomme davantage la batterie de la manette et s'applique à la prochaine connexion d'une manette.",
    es='«Rápida» hace que los mandos que la consola reconoce envíen sus datos con más frecuencia. Gasta más batería del mando y se aplica la próxima vez que se conecte un mando.',
    de='„Schnell“ lässt die Controller, die die Konsole kennt, ihre Eingaben häufiger senden. Das verbraucht mehr Akku des Controllers und gilt ab dem nächsten Verbinden eines Controllers.',
    it='«Veloce» fa sì che i controller riconosciuti dalla console inviino i comandi più spesso. Consuma più batteria del controller e si applica alla prossima connessione di un controller.',
    nl="'Snel' laat de controllers die de console kent hun invoer vaker versturen. Dit gebruikt meer batterij van de controller en geldt vanaf de volgende keer dat een controller verbinding maakt.",
    ptPT='«Rápida» faz com que os comandos que a consola reconhece enviem os dados com mais frequência. Gasta mais bateria do comando e aplica-se da próxima vez que um comando se ligar.',
    ru='«Быстрый» заставляет известные консоли контроллеры отправлять ввод чаще. Это расходует больше заряда контроллера и действует со следующего подключения контроллера.',
    ko="'빠름'으로 하면 본체가 인식하는 컨트롤러가 입력을 더 자주 보냅니다. 컨트롤러 배터리를 더 많이 소모하며, 다음에 컨트롤러를 연결할 때부터 적용됩니다.",
    zhT='選擇「快速」後，主機認得的控制器會更頻繁地傳送輸入。這會消耗更多控制器電池，並在下次連接控制器時生效。',
    zhS='选择“快速”后，主机认得的控制器会更频繁地发送输入。这会消耗更多控制器电池，并在下次连接控制器时生效。',
    fi='Nopea saa konsolin tuntemat ohjaimet lähettämään syötteensä useammin. Se kuluttaa enemmän ohjaimen akkua ja tulee voimaan, kun ohjain seuraavan kerran yhdistetään.',
    sv='Snabb låter de handkontroller som konsolen känner till skicka sin inmatning oftare. Det drar mer batteri i handkontrollen och gäller nästa gång en handkontroll ansluts.',
    da='Hurtig lader de controllere, konsollen kender, sende deres input oftere. Det bruger mere batteri i controlleren og gælder næste gang en controller tilsluttes.',
    no='Rask lar kontrollerne konsollen kjenner sende inndata oftere. Det bruker mer batteri i kontrolleren og gjelder neste gang en kontroller kobles til.',
    pl='„Szybka” sprawia, że kontrolery rozpoznawane przez konsolę wysyłają dane wejściowe częściej. Zużywa to więcej baterii kontrolera i obowiązuje od następnego połączenia kontrolera.',
    ptBR='"Rápida" faz os controles que o console reconhece enviarem os comandos com mais frequência. Gasta mais bateria do controle e vale a partir da próxima vez que um controle for conectado.',
    tr='Hızlı, konsolun tanıdığı kontrol cihazlarının girdilerini daha sık göndermesini sağlar. Kontrol cihazının pilini daha çok tüketir ve bir kontrol cihazı bir sonraki bağlanışında geçerli olur.')
add('ITEM_FAST',
    ja='高速', en='Fast', fr='Rapide', es='Rápida', de='Schnell', it='Veloce', nl='Snel',
    ptPT='Rápida', ru='Быстрый', ko='빠름', zhT='快速', zhS='快速', fi='Nopea', sv='Snabb',
    da='Hurtig', no='Rask', pl='Szybka', ptBR='Rápida', tr='Hızlı')

add('ROW_DISC',
    ja='ヘッドホンの接続が切れたとき',
    en='When Headphones Disconnect',
    fr='Lors de la déconnexion des écouteurs',
    es='Al desconectarse los auriculares',
    de='Beim Trennen der Kopfhörer',
    it='Alla disconnessione delle cuffie',
    nl='Bij verbreken koptelefoon',
    ptPT='Quando os auscultadores se desligam',
    ru='При отключении наушников',
    ko='헤드폰 연결이 끊어졌을 때',
    zhT='耳機中斷連線時',
    zhS='耳机断开连接时',
    fi='Kun kuulokkeiden yhteys katkeaa',
    sv='När hörlurarna kopplas bort',
    da='Når hovedtelefonerne mister forbindelsen',
    no='Når hodetelefonene kobles fra',
    pl='Po rozłączeniu słuchawek',
    ptBR='Quando os fones de ouvido desconectarem',
    tr='Kulaklık bağlantısı kesildiğinde')
add('HINT_DISC',
    ja='Bluetoothヘッドホンの電源が切れたり、接続が切れたりしたときの動作です。「ミュート」は本体本来の動作で、音を消します。「一時停止（音量はそのまま）」は音楽を一時停止し、ミュートしません。「スピーカーで再生」はミュートせず、そのままスピーカーで再生を続けます。次にヘッドホンの接続が切れたときから反映されます。',
    en="What the console does when Bluetooth headphones switch off or lose their connection. Mute is the console's own: it silences itself. Pause, Keep Volume pauses the music and does not mute. Play on Speaker does not mute and keeps playing, now on the speaker. Applies from the next time headphones disconnect.",
    fr="Ce que fait la console quand les écouteurs Bluetooth s'éteignent ou perdent la connexion. « Couper le son » est le comportement d'origine : la console se met en sourdine. « Pause, garder le volume » met la musique en pause sans couper le son. « Lire sur le haut-parleur » ne coupe pas le son et continue la lecture, désormais sur le haut-parleur. S'applique à la prochaine déconnexion des écouteurs.",
    es='Lo que hace la consola cuando los auriculares Bluetooth se apagan o pierden la conexión. «Silenciar» es lo habitual de la consola: se silencia sola. «Pausar, mantener volumen» pausa la música sin silenciar. «Reproducir por el altavoz» no silencia y sigue reproduciendo, ahora por el altavoz. Se aplica desde la próxima vez que se desconecten los auriculares.',
    de='Was die Konsole tut, wenn Bluetooth-Kopfhörer sich ausschalten oder die Verbindung verlieren. „Stumm“ ist das Verhalten der Konsole ab Werk: Sie schaltet sich selbst stumm. „Pause, Lautstärke beibehalten“ hält die Musik an, ohne stummzuschalten. „Über Lautsprecher wiedergeben“ schaltet nicht stumm und spielt weiter, jetzt über den Lautsprecher. Gilt ab der nächsten Trennung der Kopfhörer.',
    it="Cosa fa la console quando le cuffie Bluetooth si spengono o perdono la connessione. «Silenzia» è il comportamento originale: la console si silenzia da sola. «Pausa, mantieni volume» mette in pausa la musica senza silenziare. «Riproduci dall'altoparlante» non silenzia e continua la riproduzione, ora dall'altoparlante. Si applica dalla prossima disconnessione delle cuffie.",
    nl="Wat de console doet wanneer Bluetooth-koptelefoons uitgaan of de verbinding verliezen. 'Dempen' is het oorspronkelijke gedrag: de console dempt zichzelf. 'Pauzeren, volume behouden' pauzeert de muziek zonder te dempen. 'Afspelen via luidspreker' dempt niet en speelt door, nu via de luidspreker. Geldt vanaf de volgende keer dat koptelefoons de verbinding verbreken.",
    ptPT='O que a consola faz quando os auscultadores Bluetooth se desligam ou perdem a ligação. «Silenciar» é o comportamento original: a consola silencia-se a si própria. «Pausa, manter volume» põe a música em pausa sem silenciar. «Reproduzir no altifalante» não silencia e continua a reprodução, agora no altifalante. Aplica-se a partir da próxima vez que os auscultadores se desligarem.',
    ru='Что делает консоль, когда Bluetooth-наушники выключаются или теряют связь. «Без звука» — исходное поведение: консоль отключает звук сама. «Пауза, сохранить громкость» ставит музыку на паузу, не отключая звук. «Играть через динамик» не отключает звук и продолжает воспроизведение, теперь через динамик. Действует со следующего отключения наушников.',
    ko="Bluetooth 헤드폰의 전원이 꺼지거나 연결이 끊어졌을 때 본체가 하는 동작입니다. '음소거'는 본체 본래의 동작으로, 본체가 스스로 소리를 끕니다. '일시 정지, 음량 유지'는 음소거하지 않고 음악을 일시 정지합니다. '스피커로 재생'은 음소거하지 않고 스피커로 계속 재생합니다. 다음에 헤드폰 연결이 끊어질 때부터 적용됩니다.",
    zhT='設定 Bluetooth 耳機關機或中斷連線時主機的動作。「靜音」是主機原本的動作：主機會自行靜音。「暫停，保持音量」會暫停音樂，但不靜音。「以喇叭播放」不會靜音，並改由喇叭繼續播放。從下次耳機中斷連線時生效。',
    zhS='设置 Bluetooth 耳机关机或断开连接时主机的动作。“静音”是主机原本的动作：主机会自行静音。“暂停，保持音量”会暂停音乐，但不静音。“通过扬声器播放”不会静音，并改由扬声器继续播放。从下次耳机断开连接时生效。',
    fi='Mitä konsoli tekee, kun Bluetooth-kuulokkeet sammuvat tai yhteys katkeaa. ”Mykistä” on konsolin oma toiminta: se mykistää itsensä. ”Tauko, äänenvoimakkuus säilyy” keskeyttää musiikin mykistämättä. ”Toista kaiuttimella” ei mykistä ja jatkaa toistoa nyt kaiuttimella. Tulee voimaan, kun kuulokkeet seuraavan kerran katkaisevat yhteyden.',
    sv='Vad konsolen gör när Bluetooth-hörlurar stängs av eller tappar anslutningen. ”Tysta” är konsolens eget beteende: den tystar sig själv. ”Paus, behåll volymen” pausar musiken utan att tysta. ”Spela på högtalaren” tystar inte och fortsätter spela, nu i högtalaren. Gäller från nästa gång hörlurarna kopplas bort.',
    da='Hvad konsollen gør, når Bluetooth-hovedtelefoner slukkes eller mister forbindelsen. »Slå lyden fra« er konsollens eget: den gør sig selv lydløs. »Pause, behold lydstyrke« sætter musikken på pause uden at slå lyden fra. »Afspil på højttaleren« slår ikke lyden fra og afspiller videre, nu på højttaleren. Gælder fra næste gang hovedtelefonerne afbrydes.',
    no='Hva konsollen gjør når Bluetooth-hodetelefoner slås av eller mister forbindelsen. «Slå av lyden» er konsollens egen oppførsel: den gjør seg selv lydløs. «Pause, behold volumet» setter musikken på pause uten å slå av lyden. «Spill av på høyttaleren» slår ikke av lyden og fortsetter avspillingen, nå på høyttaleren. Gjelder fra neste gang hodetelefonene kobles fra.',
    pl='Co robi konsola, gdy słuchawki Bluetooth wyłączą się lub utracą połączenie. „Wycisz” to działanie pierwotne: konsola sama się wycisza. „Wstrzymaj, zachowaj głośność” wstrzymuje muzykę bez wyciszania. „Odtwarzaj przez głośnik” nie wycisza i odtwarza dalej, teraz przez głośnik. Obowiązuje od następnego rozłączenia słuchawek.',
    ptBR='O que o console faz quando os fones de ouvido Bluetooth são desligados ou perdem a conexão. "Silenciar" é o comportamento original: o console se silencia sozinho. "Pausar, manter volume" pausa a música sem silenciar. "Reproduzir no alto-falante" não silencia e continua a reprodução, agora no alto-falante. Vale a partir da próxima vez que os fones de ouvido desconectarem.',
    tr='Bluetooth kulaklıklar kapandığında veya bağlantı kesildiğinde konsolun ne yapacağı. "Sessiz", konsolun kendi davranışıdır: kendini sessize alır. "Duraklat, sesi koru", sessize almadan müziği duraklatır. "Hoparlörden çal", sessize almaz ve çalmaya devam eder, şimdi hoparlörden. Kulaklıklar bir sonraki kez bağlantıyı kestiğinde geçerli olur.')
add('ITEM_DISC_MUTE',
    ja='ミュート', en='Mute', fr='Couper le son', es='Silenciar', de='Stumm', it='Silenzia', nl='Dempen',
    ptPT='Silenciar', ru='Без звука', ko='음소거', zhT='靜音', zhS='静音', fi='Mykistä', sv='Tysta',
    da='Slå lyden fra', no='Slå av lyden', pl='Wycisz', ptBR='Silenciar', tr='Sessiz')
add('ITEM_DISC_PAUSE',
    ja='一時停止（音量はそのまま）', en='Pause, Keep Volume', fr='Pause, garder le volume',
    es='Pausar, mantener volumen', de='Pause, Lautstärke beibehalten', it='Pausa, mantieni volume',
    nl='Pauzeren, volume behouden', ptPT='Pausa, manter volume', ru='Пауза, сохранить громкость',
    ko='일시 정지, 음량 유지', zhT='暫停，保持音量', zhS='暂停，保持音量', fi='Tauko, äänenvoimakkuus säilyy',
    sv='Paus, behåll volymen', da='Pause, behold lydstyrke', no='Pause, behold volumet',
    pl='Wstrzymaj, zachowaj głośność', ptBR='Pausar, manter volume', tr='Duraklat, sesi koru')
add('ITEM_DISC_SPK',
    ja='スピーカーで再生', en='Play on Speaker', fr='Lire sur le haut-parleur', es='Reproducir por el altavoz',
    de='Über Lautsprecher wiedergeben', it="Riproduci dall'altoparlante", nl='Afspelen via luidspreker',
    ptPT='Reproduzir no altifalante', ru='Играть через динамик', ko='스피커로 재생', zhT='以喇叭播放',
    zhS='通过扬声器播放', fi='Toista kaiuttimella', sv='Spela på högtalaren', da='Afspil på højttaleren',
    no='Spill av på høyttaleren', pl='Odtwarzaj przez głośnik', ptBR='Reproduzir no alto-falante',
    tr='Hoparlörden çal')
add('ROW_RECONN',
    ja='スタンバイ後に再接続',
    en='Reconnect After Standby',
    fr='Reconnexion après la veille',
    es='Reconectar tras la suspensión',
    de='Nach Standby neu verbinden',
    it='Riconnetti dopo lo standby',
    nl='Opnieuw verbinden na stand-by',
    ptPT='Religar após a suspensão',
    ru='Подключать после режима ожидания',
    ko='대기 모드 후 다시 연결',
    zhT='待機後重新連線',
    zhS='待机后重新连接',
    fi='Yhdistä uudelleen valmiustilan jälkeen',
    sv='Anslut igen efter viloläge',
    da='Genopret forbindelsen efter standby',
    no='Koble til igjen etter hvilemodus',
    pl='Połącz ponownie po uśpieniu',
    ptBR='Reconectar após a suspensão',
    tr='Beklemeden sonra yeniden bağlan')
add('HINT_RECONN',
    ja='本体がスタンバイから復帰したとき、最後に音を再生したヘッドホンに接続します。ヘッドホンは、次にそのヘッドホンで音が鳴り始めたときに記憶されます。スマートフォンにも登録されているヘッドホンは、スマートフォンに接続されることがあります。オフのときは、ヘッドホンの電源を入れ直してください。',
    en='When the console wakes from standby, it connects the headphones it last played sound on. It learns them the next time sound starts on them. Headphones that are also paired with a phone may connect to the phone instead. When off, you switch the headphones off and on yourself.',
    fr="Quand la console sort de veille, elle reconnecte les écouteurs sur lesquels elle a joué du son en dernier. Elle les retient la prochaine fois que du son démarre dessus. Des écouteurs aussi associés à un téléphone peuvent se connecter au téléphone à la place. Désactivé, vous éteignez et rallumez vous-même les écouteurs.",
    es='Cuando la consola sale de la suspensión, conecta los auriculares en los que reprodujo sonido por última vez. Los recuerda la próxima vez que empiece a sonar algo en ellos. Los auriculares también vinculados a un teléfono pueden conectarse al teléfono en su lugar. Desactivado, eres tú quien apaga y enciende los auriculares.',
    de='Wenn die Konsole aus dem Standby aufwacht, verbindet sie die Kopfhörer, auf denen sie zuletzt Ton wiedergegeben hat. Sie merkt sie sich, sobald das nächste Mal Ton auf ihnen startet. Kopfhörer, die auch mit einem Smartphone gekoppelt sind, verbinden sich unter Umständen stattdessen mit dem Smartphone. Ausgeschaltet schaltest du die Kopfhörer selbst aus und wieder ein.',
    it="Quando la console esce dallo standby, collega le cuffie su cui ha riprodotto audio per ultime. Le memorizza la prossima volta che parte l'audio su di esse. Le cuffie associate anche a uno smartphone possono collegarsi invece allo smartphone. Se disattivato, spegni e riaccendi tu le cuffie.",
    nl='Wanneer de console uit stand-by komt, verbindt ze de koptelefoons waarop ze het laatst geluid afspeelde. Ze onthoudt ze zodra er de volgende keer geluid op start. Koptelefoons die ook aan een telefoon gekoppeld zijn, kunnen in plaats daarvan met de telefoon verbinden. Uit betekent dat je de koptelefoons zelf uit- en weer aanzet.',
    ptPT='Quando a consola sai da suspensão, liga os auscultadores em que reproduziu som por último. Memoriza-os da próxima vez que o som começar neles. Os auscultadores também emparelhados com um telemóvel podem ligar-se ao telemóvel em vez da consola. Desativado, tem de desligar e voltar a ligar os auscultadores.',
    ru='Когда консоль выходит из режима ожидания, она подключает наушники, на которых в последний раз воспроизводила звук. Она запоминает их, когда в следующий раз на них начинается звук. Наушники, также сопряжённые с телефоном, могут подключиться к телефону. Если выключено, наушники нужно выключить и включить самостоятельно.',
    ko='본체가 대기 모드에서 깨어나면 마지막으로 소리를 재생한 헤드폰에 연결합니다. 해당 헤드폰에서 다음에 소리가 시작될 때 기억합니다. 휴대전화에도 등록된 헤드폰은 휴대전화에 연결될 수 있습니다. 끄면 헤드폰의 전원을 직접 껐다 켜야 합니다.',
    zhT='主機從待機狀態喚醒時，會連線至最後播放聲音的耳機。主機會在下次該耳機開始播放聲音時記住它。同時與手機配對的耳機，可能會改為連線到手機。關閉時，需自行將耳機關機再開機。',
    zhS='主机从待机状态唤醒时，会连接到最后播放声音的耳机。主机会在下次该耳机开始播放声音时记住它。同时与手机配对的耳机，可能会改为连接到手机。关闭时，需自行将耳机关机再开机。',
    fi='Kun konsoli herää valmiustilasta, se yhdistää kuulokkeet, joilla se toisti ääntä viimeksi. Se muistaa ne, kun niillä seuraavan kerran alkaa ääni. Kuulokkeet, jotka on liitetty myös puhelimeen, voivat yhdistyä puhelimeen. Pois päältä kuulokkeet sammutetaan ja kytketään itse uudelleen päälle.',
    sv='När konsolen vaknar ur viloläge ansluter den hörlurarna som den senast spelade ljud i. Den kommer ihåg dem nästa gång ljud startar i dem. Hörlurar som också är kopplade till en telefon kan ansluta till telefonen i stället. Av betyder att du själv stänger av och slår på hörlurarna.',
    da='Når konsollen vågner fra standby, forbinder den de hovedtelefoner, den sidst afspillede lyd på. Den husker dem, næste gang der starter lyd på dem. Hovedtelefoner, der også er parret med en telefon, kan i stedet forbinde til telefonen. Slået fra slår du selv hovedtelefonerne fra og til igen.',
    no='Når konsollen våkner fra hvilemodus, kobler den til hodetelefonene den sist spilte lyd på. Den husker dem neste gang lyd starter på dem. Hodetelefoner som også er paret med en telefon, kan koble til telefonen i stedet. Når den er av, slår du selv hodetelefonene av og på igjen.',
    pl='Gdy konsola wybudza się z uśpienia, łączy się ze słuchawkami, na których ostatnio odtwarzała dźwięk. Zapamiętuje je, gdy następnym razem zacznie się na nich dźwięk. Słuchawki sparowane także z telefonem mogą połączyć się zamiast tego z telefonem. Po wyłączeniu samodzielnie wyłączasz i włączasz słuchawki.',
    ptBR='Quando o console sai da suspensão, ele conecta os fones de ouvido em que tocou som por último. Ele os memoriza na próxima vez que o som começar neles. Fones de ouvido também pareados com um celular podem se conectar ao celular. Desativado, você mesmo desliga e liga os fones de ouvido.',
    tr='Konsol bekleme modundan uyandığında, en son ses çaldığı kulaklıklara bağlanır. Kulaklıkları, onlarda bir sonraki ses başladığında hatırlar. Telefona da eşlenmiş kulaklıklar bunun yerine telefona bağlanabilir. Kapalıyken kulaklıkları kendiniz kapatıp açarsınız.')
add('ROW_SRCONLY',
    ja='音楽送信専用',
    en='Music Source Only',
    fr='Source musicale uniquement',
    es='Solo fuente de música',
    de='Nur Musikquelle',
    it='Solo sorgente musicale',
    nl='Alleen muziekbron',
    ptPT='Apenas fonte de música',
    ru='Только источник музыки',
    ko='음악 소스 전용',
    zhT='僅作為音樂來源',
    zhS='仅作为音乐来源',
    fi='Vain musiikin lähde',
    sv='Endast musikkälla',
    da='Kun musikkilde',
    no='Bare musikkilde',
    pl='Tylko źródło muzyki',
    ptBR='Apenas fonte de música',
    tr='Yalnızca müzik kaynağı')
add('HINT_SRCONLY',
    ja='初期設定ではオンです。Vitaは音を送る側としてのみ接続を申し出るため、スマートスピーカーのような双方向のデバイスも正しい向きで接続されます。オフにすると、本体本来のスピーカー役も提示します。変更後は、デバイスを削除してもう一度ペアリングしてください。',
    en="On by default: the Vita only offers to send sound, so two-way devices such as smart speakers connect the right way round. Turn off to also offer the console's original speaker role. Remove and pair the device again after changing it.",
    fr="Activé par défaut : la Vita propose seulement d'envoyer du son, si bien que les appareils pouvant jouer les deux rôles, comme les enceintes connectées, se connectent dans le bon sens. Désactivez pour proposer aussi le rôle de haut-parleur d'origine de la console. Après un changement, supprimez l'appareil puis associez-le de nouveau.",
    es='Activado por defecto: la Vita solo se ofrece a enviar sonido, así que los dispositivos que pueden hacer ambas cosas, como los altavoces inteligentes, se conectan en el sentido correcto. Desactívalo para ofrecer también el papel de altavoz original de la consola. Tras cambiarlo, elimina el dispositivo y vuelve a emparejarlo.',
    de='Standardmäßig an: Die Vita bietet nur an, Ton zu senden, sodass Geräte, die beide Rollen beherrschen, etwa Smart Speaker, richtig herum verbinden. Ausschalten, um zusätzlich die ursprüngliche Lautsprecherrolle der Konsole anzubieten. Nach einer Änderung das Gerät entfernen und neu koppeln.',
    it='Attivo per impostazione predefinita: la Vita si offre solo di inviare audio, così i dispositivi che possono fare entrambe le parti, come gli smart speaker, si collegano nel verso giusto. Disattiva per offrire anche il ruolo di altoparlante originale della console. Dopo la modifica, rimuovi il dispositivo e abbinalo di nuovo.',
    nl='Standaard aan: de Vita biedt alleen aan om geluid te sturen, zodat apparaten die beide kunnen, zoals slimme luidsprekers, de juiste kant op verbinden. Zet uit om ook de oorspronkelijke luidsprekerrol van de console aan te bieden. Verwijder het apparaat na een wijziging en koppel het opnieuw.',
    ptPT='Ativado por predefinição: a Vita só se oferece para enviar som, para que os dispositivos que podem fazer as duas coisas, como as colunas inteligentes, liguem no sentido certo. Desative para oferecer também a função de altifalante original da consola. Depois de alterar, remova o dispositivo e volte a emparelhá-lo.',
    ru='По умолчанию включено: Vita предлагает только отправлять звук, поэтому устройства, которые могут играть обе роли, например умные колонки, подключаются в правильном направлении. Выключите, чтобы консоль также предлагала свою исходную роль колонки. После изменения удалите устройство и выполните сопряжение заново.',
    ko='기본값은 켜짐입니다. Vita가 소리를 보내는 역할만 제안하므로 스마트 스피커처럼 양방향이 가능한 기기도 올바른 방향으로 연결됩니다. 끄면 본체 원래의 스피커 역할도 함께 제안합니다. 변경한 후에는 기기를 삭제하고 다시 페어링하세요.',
    zhT='預設為開啟：Vita 只會提出傳送聲音，因此智慧型喇叭這類可雙向運作的裝置會以正確的方向連線。關閉後，也會提供主機原有的喇叭角色。變更後，請移除該裝置並重新配對。',
    zhS='默认开启：Vita 只提出发送声音，因此智能音箱这类可双向工作的设备会以正确的方向连接。关闭后，也会提供主机原有的扬声器角色。更改后，请移除该设备并重新配对。',
    fi='Oletuksena päällä: Vita tarjoutuu vain lähettämään ääntä, joten kaksisuuntaiset laitteet, kuten älykaiuttimet, yhdistyvät oikein päin. Kytke pois päältä, jos haluat tarjota myös konsolin alkuperäisen kaiuttimen roolin. Poista laite ja muodosta laitepari uudelleen muutoksen jälkeen.',
    sv='På som standard: Vita erbjuder bara att skicka ljud, så enheter som kan båda delarna, till exempel smarta högtalare, ansluter åt rätt håll. Stäng av för att även erbjuda konsolens ursprungliga högtalarroll. Ta bort enheten och parkoppla den igen efter en ändring.',
    da='Slået til som standard: Vita tilbyder kun at sende lyd, så enheder, der kan begge dele, som smarte højttalere, forbinder den rigtige vej. Slå fra for også at tilbyde konsollens oprindelige højttalerrolle. Fjern enheden, og par den igen efter en ændring.',
    no='På som standard: Vita tilbyr bare å sende lyd, så enheter som kan begge deler, som smarthøyttalere, kobler til riktig vei. Slå av for også å tilby konsollens opprinnelige høyttalerrolle. Fjern enheten og par den på nytt etter en endring.',
    pl='Domyślnie włączone: Vita oferuje tylko wysyłanie dźwięku, więc urządzenia, które mogą pełnić obie role, na przykład inteligentne głośniki, łączą się w odpowiednią stronę. Wyłącz, aby konsola oferowała też swoją pierwotną rolę głośnika. Po zmianie usuń urządzenie i sparuj je ponownie.',
    ptBR='Ativado por padrão: o Vita só se oferece para enviar som, para que dispositivos que podem fazer as duas coisas, como alto-falantes inteligentes, se conectem no sentido certo. Desative para oferecer também a função de alto-falante original do console. Depois de alterar, remova o dispositivo e pareie-o de novo.',
    tr='Varsayılan olarak açık: Vita yalnızca ses göndermeyi önerir, böylece akıllı hoparlörler gibi iki yönlü cihazlar doğru yönde bağlanır. Konsolun özgün hoparlör rolünü de sunmak için kapatın. Değiştirdikten sonra cihazı kaldırıp yeniden eşleştirin.')
add('ROW_NAME',
    ja='Bluetooth名',
    en='Bluetooth Name',
    fr='Nom Bluetooth',
    es='Nombre Bluetooth',
    de='Bluetooth-Name',
    it='Nome Bluetooth',
    nl='Bluetooth-naam',
    ptPT='Nome Bluetooth',
    ru='Имя Bluetooth',
    ko='Bluetooth 이름',
    zhT='Bluetooth 名稱',
    zhS='Bluetooth 名称',
    fi='Bluetooth-nimi',
    sv='Bluetooth-namn',
    da='Bluetooth-navn',
    no='Bluetooth-navn',
    pl='Nazwa Bluetooth',
    ptBR='Nome do Bluetooth',
    tr='Bluetooth adı')
add('HINT_NAME',
    ja='他のデバイスがBluetoothで本体を探したときに表示される名前です。英数字と記号で16文字まで（アクセント付き文字は使えません）。空のままにすると「PlayStation Vita」になります。次にBluetoothをオンにしたときから反映されるので、Bluetoothをオフにしてからオンにしてください。すでに本体を登録しているデバイスには、以前の名前が残ることがあります。本体が受け付けなかった名前は、ページを開き直すまで入力欄に残ることがあります。',
    en='The name other devices see when they look for this console over Bluetooth: up to 16 letters, digits and symbols, no accents. Leave it empty for PlayStation Vita. Applies the next time Bluetooth is switched on, so turn Bluetooth off and on. Devices that already know the console may keep the old name. A name the console refuses may stay in this field until the page is opened again.',
    fr="Le nom que les autres appareils voient lorsqu'ils cherchent cette console en Bluetooth : 16 caractères au plus (lettres, chiffres et symboles, sans accents). Laissez vide pour PlayStation Vita. S'applique à la prochaine activation du Bluetooth : désactivez-le puis réactivez-le. Les appareils qui connaissent déjà la console peuvent garder l'ancien nom. Un nom refusé par la console peut rester affiché dans ce champ jusqu'à la prochaine ouverture de la page.",
    es='El nombre que ven otros dispositivos cuando buscan esta consola por Bluetooth: hasta 16 letras, números y símbolos, sin acentos. Déjalo vacío para PlayStation Vita. Se aplica la próxima vez que se active el Bluetooth, así que desactívalo y vuelve a activarlo. Los dispositivos que ya conocen la consola pueden mantener el nombre anterior. Un nombre que la consola rechace puede seguir visible en este campo hasta que se vuelva a abrir la página.',
    de='Der Name, den andere Geräte sehen, wenn sie diese Konsole über Bluetooth suchen: bis zu 16 Buchstaben, Ziffern und Symbole, ohne Akzente. Leer lassen für PlayStation Vita. Gilt ab dem nächsten Einschalten von Bluetooth: Schalte Bluetooth aus und wieder ein. Geräte, die die Konsole schon kennen, zeigen möglicherweise weiter den alten Namen. Ein Name, den die Konsole ablehnt, kann in diesem Feld stehen bleiben, bis die Seite erneut geöffnet wird.',
    it='Il nome che gli altri dispositivi vedono quando cercano questa console via Bluetooth: fino a 16 lettere, numeri e simboli, senza accenti. Lascialo vuoto per PlayStation Vita. Si applica alla prossima attivazione del Bluetooth: disattivalo e riattivalo. I dispositivi che già conoscono la console possono mantenere il nome precedente. Un nome rifiutato dalla console può restare visibile in questo campo finché la pagina non viene riaperta.',
    nl='De naam die andere apparaten zien wanneer ze via Bluetooth naar deze console zoeken: maximaal 16 letters, cijfers en tekens, zonder accenten. Laat leeg voor PlayStation Vita. Geldt vanaf de volgende keer dat Bluetooth wordt ingeschakeld: zet Bluetooth uit en weer aan. Apparaten die de console al kennen, kunnen de oude naam houden. Een naam die de console weigert, kan in dit veld zichtbaar blijven tot de pagina opnieuw wordt geopend.',
    ptPT='O nome que outros dispositivos veem quando procuram esta consola por Bluetooth: até 16 letras, algarismos e símbolos, sem acentos. Deixe vazio para PlayStation Vita. Aplica-se da próxima vez que o Bluetooth for ligado, por isso desligue e volte a ligar o Bluetooth. Os dispositivos que já conhecem a consola podem manter o nome anterior. Um nome que a consola recuse pode continuar visível neste campo até a página ser aberta de novo.',
    ru='Имя, которое видят другие устройства, когда ищут эту консоль по Bluetooth: до 16 букв, цифр и символов, без диакритических знаков. Оставьте пустым, чтобы использовать PlayStation Vita. Применяется при следующем включении Bluetooth: выключите и снова включите Bluetooth. Устройства, которые уже знают консоль, могут сохранить прежнее имя. Имя, которое консоль отклонила, может оставаться в этом поле, пока страница не будет открыта снова.',
    ko='다른 기기가 Bluetooth로 이 본체를 찾을 때 보이는 이름입니다. 영문, 숫자, 기호로 최대 16자까지 입력할 수 있으며 악센트 문자는 쓸 수 없습니다. 비워 두면 PlayStation Vita가 됩니다. 다음에 Bluetooth를 켤 때부터 적용되므로 Bluetooth를 껐다가 다시 켜세요. 이미 본체를 아는 기기에는 이전 이름이 남아 있을 수 있습니다. 본체가 거부한 이름은 페이지를 다시 열 때까지 이 입력란에 남아 있을 수 있습니다.',
    zhT='其他裝置透過 Bluetooth 搜尋本主機時看到的名稱：最多 16 個字元，可使用英文字母、數字和符號，不可使用重音符號。留空則使用 PlayStation Vita。下次開啟 Bluetooth 時生效，請先關閉再重新開啟 Bluetooth。已經認得本主機的裝置可能仍會顯示舊名稱。主機拒絕的名稱可能會一直留在此欄位中，直到重新開啟此頁面。',
    zhS='其他设备通过 Bluetooth 搜索本主机时看到的名称：最多 16 个字符，可使用英文字母、数字和符号，不可使用重音符号。留空则使用 PlayStation Vita。下次开启 Bluetooth 时生效，请先关闭再重新开启 Bluetooth。已经识别本主机的设备可能仍会显示旧名称。主机拒绝的名称可能会一直留在此字段中，直到重新打开此页面。',
    fi='Nimi, jonka muut laitteet näkevät etsiessään tätä konsolia Bluetoothilla: enintään 16 kirjainta, numeroa ja merkkiä, ei aksentteja. Jätä tyhjäksi, jolloin nimi on PlayStation Vita. Tulee voimaan, kun Bluetooth seuraavan kerran kytketään päälle, joten kytke Bluetooth pois ja takaisin päälle. Laitteet, jotka tuntevat konsolin jo, voivat näyttää vanhaa nimeä. Konsolin hylkäämä nimi voi jäädä näkyviin tähän kenttään, kunnes sivu avataan uudelleen.',
    sv='Namnet som andra enheter ser när de söker efter den här konsolen via Bluetooth: upp till 16 bokstäver, siffror och tecken, utan accenter. Lämna tomt för PlayStation Vita. Gäller nästa gång Bluetooth slås på, så slå av och på Bluetooth. Enheter som redan känner till konsolen kan behålla det gamla namnet. Ett namn som konsolen avvisar kan stå kvar i det här fältet tills sidan öppnas igen.',
    da='Navnet, som andre enheder ser, når de søger efter denne konsol via Bluetooth: op til 16 bogstaver, tal og tegn, uden accenter. Lad feltet være tomt for PlayStation Vita. Gælder næste gang Bluetooth slås til, så slå Bluetooth fra og til igen. Enheder, der allerede kender konsollen, kan beholde det gamle navn. Et navn, som konsollen afviser, kan blive stående i dette felt, indtil siden åbnes igen.',
    no='Navnet andre enheter ser når de søker etter denne konsollen via Bluetooth: opptil 16 bokstaver, tall og tegn, uten aksenter. La feltet stå tomt for PlayStation Vita. Gjelder neste gang Bluetooth slås på, så slå Bluetooth av og på igjen. Enheter som allerede kjenner konsollen, kan beholde det gamle navnet. Et navn som konsollen avviser, kan bli stående i dette feltet til siden åpnes på nytt.',
    pl='Nazwa, którą widzą inne urządzenia, gdy szukają tej konsoli przez Bluetooth: do 16 liter, cyfr i znaków, bez znaków diakrytycznych. Pozostaw puste, aby używać nazwy PlayStation Vita. Obowiązuje od następnego włączenia Bluetooth, więc wyłącz i ponownie włącz Bluetooth. Urządzenia, które już znają konsolę, mogą zachować starą nazwę. Nazwa odrzucona przez konsolę może pozostać w tym polu do ponownego otwarcia strony.',
    ptBR='O nome que outros dispositivos veem quando procuram este console por Bluetooth: até 16 letras, números e símbolos, sem acentos. Deixe vazio para PlayStation Vita. Vale a partir da próxima vez que o Bluetooth for ligado, então desligue e ligue o Bluetooth. Dispositivos que já conhecem o console podem manter o nome anterior. Um nome que o console recusar pode continuar visível neste campo até a página ser aberta de novo.',
    tr="Diğer cihazların bu konsolu Bluetooth ile ararken gördüğü ad: en fazla 16 harf, rakam ve simge, aksansız. PlayStation Vita için boş bırakın. Bluetooth bir sonraki açılışında geçerli olur, bu yüzden Bluetooth'u kapatıp yeniden açın. Konsolu zaten tanıyan cihazlar eski adı koruyabilir. Konsolun reddettiği bir ad, sayfa yeniden açılana kadar bu alanda görünmeye devam edebilir.")
add('ROW_NAMENOW',
    ja='Bluetoothネットワーク名',
    en='Bluetooth Network Name',
    fr='Nom du réseau Bluetooth',
    es='Nombre de red Bluetooth',
    de='Bluetooth-Netzwerkname',
    it='Nome rete Bluetooth',
    nl='Bluetooth-netwerknaam',
    ptPT='Nome da rede Bluetooth',
    ru='Имя сети Bluetooth',
    ko='Bluetooth 네트워크 이름',
    zhT='Bluetooth 網路名稱',
    zhS='Bluetooth 网络名称',
    fi='Bluetooth-verkon nimi',
    sv='Bluetooth-nätverksnamn',
    da='Bluetooth-netværksnavn',
    no='Bluetooth-nettverksnavn',
    pl='Nazwa sieci Bluetooth',
    ptBR='Nome da rede Bluetooth',
    tr='Bluetooth ağ adı')
add('HINT_NAMENOW',
    ja='Bluetooth名に設定されている名前です。新しい名前は次にBluetoothをオンにしたときから使われます。この項目は、ページを次に開いたときに更新されます。',
    en='The name set under Bluetooth Name. A new name is used from the next time Bluetooth turns on. This row updates the next time the page is opened.',
    fr="Le nom défini sous « Nom Bluetooth ». Un nouveau nom est utilisé à partir de la prochaine activation du Bluetooth. Cette ligne est mise à jour à la prochaine ouverture de la page.",
    es='El nombre definido en «Nombre Bluetooth». Un nombre nuevo se usa a partir de la próxima vez que se active el Bluetooth. Esta fila se actualiza la próxima vez que se abra la página.',
    de='Der unter „Bluetooth-Name“ festgelegte Name. Ein neuer Name gilt ab dem nächsten Einschalten von Bluetooth. Diese Zeile wird beim nächsten Öffnen der Seite aktualisiert.',
    it='Il nome impostato in «Nome Bluetooth». Un nuovo nome viene usato dalla prossima volta che il Bluetooth si attiva. Questa riga si aggiorna alla prossima apertura della pagina.',
    nl="De naam die bij 'Bluetooth-naam' is ingesteld. Een nieuwe naam wordt gebruikt vanaf de volgende keer dat Bluetooth aangaat. Deze regel wordt bijgewerkt de volgende keer dat de pagina wordt geopend.",
    ptPT='O nome definido em «Nome Bluetooth». Um nome novo é usado a partir da próxima vez que o Bluetooth for ativado. Esta linha atualiza-se da próxima vez que a página for aberta.',
    ru='Имя, заданное в разделе «Имя Bluetooth». Новое имя используется со следующего включения Bluetooth. Эта строка обновляется при следующем открытии страницы.',
    ko='Bluetooth 이름에서 설정한 이름입니다. 새 이름은 다음에 Bluetooth를 켤 때부터 사용됩니다. 이 항목은 다음에 페이지를 열 때 갱신됩니다.',
    zhT='在「Bluetooth 名稱」中設定的名稱。新名稱從下次開啟 Bluetooth 時起使用。此項目會在下次開啟此頁面時更新。',
    zhS='在“Bluetooth 名称”中设置的名称。新名称从下次开启 Bluetooth 时起使用。此项目会在下次打开此页面时更新。',
    fi='Bluetooth-nimi-kohdassa asetettu nimi. Uusi nimi on käytössä seuraavasta kerrasta, kun Bluetooth kytketään päälle. Tämä rivi päivittyy, kun sivu avataan seuraavan kerran.',
    sv='Namnet som är inställt under ”Bluetooth-namn”. Ett nytt namn används från nästa gång Bluetooth slås på. Den här raden uppdateras nästa gång sidan öppnas.',
    da='Navnet, der er angivet under »Bluetooth-navn«. Et nyt navn bruges fra næste gang Bluetooth tændes. Denne række opdateres, næste gang siden åbnes.',
    no='Navnet som er angitt under «Bluetooth-navn». Et nytt navn brukes fra neste gang Bluetooth slås på. Denne raden oppdateres neste gang siden åpnes.',
    pl='Nazwa ustawiona w opcji „Nazwa Bluetooth”. Nowa nazwa jest używana od następnego włączenia Bluetooth. Ten wiersz zostanie zaktualizowany przy następnym otwarciu strony.',
    ptBR='O nome definido em "Nome do Bluetooth". Um nome novo é usado a partir da próxima vez que o Bluetooth for ligado. Esta linha é atualizada na próxima vez que a página for aberta.',
    tr='"Bluetooth adı" altında ayarlanan ad. Yeni ad, Bluetooth bir sonraki açılışından itibaren kullanılır. Bu satır, sayfa bir sonraki açılışında güncellenir.')

add('ROW_HEADSET',
    ja='ヘッドセットのバッテリー',
    en='Headset Battery',
    fr='Batterie du micro-casque',
    es='Batería del auricular',
    de='Headset-Akku',
    it='Batteria della cuffia',
    nl='Batterij van de headset',
    ptPT='Bateria do auricular',
    ru='Аккумулятор гарнитуры',
    ko='헤드셋 배터리',
    zhT='耳機電池',
    zhS='耳机电池',
    fi='Kuulokkeiden akku',
    sv='Headsetets batteri',
    da='Headsettets batteri',
    no='Headsettets batteri',
    pl='Bateria zestawu słuchawkowego',
    ptBR='Bateria do headset',
    tr='Kulaklık pili')
add('HINT_HEADSET',
    ja='接続中のBluetoothヘッドセットのバッテリー残量です。ヘッドセットが報告した値を表示します。バッテリー残量を報告しないヘッドセットでは「報告なし」と表示されます。',
    en='The battery level of the connected Bluetooth headset, as the headset reports it. A headset that does not report its battery reads Not Reported.',
    fr='Niveau de batterie du micro-casque Bluetooth connecté, tel que le micro-casque le communique. Un micro-casque qui ne communique pas sa batterie indique « Non communiqué ».',
    es='Nivel de batería del auricular Bluetooth conectado, tal como lo notifica el auricular. Un auricular que no notifica su batería indica «No notificado».',
    de='Akkustand des verbundenen Bluetooth-Headsets, wie das Headset ihn meldet. Bei einem Headset, das seinen Akkustand nicht meldet, steht dort „Nicht gemeldet“.',
    it='Livello della batteria della cuffia Bluetooth collegata, come lo segnala la cuffia. Per una cuffia che non segnala la batteria indica «Non segnalato».',
    nl="Het batterijniveau van de verbonden Bluetooth-headset, zoals de headset het meldt. Bij een headset die zijn batterij niet meldt staat er 'Niet gemeld'.",
    ptPT='O nível da bateria do auricular Bluetooth ligado, tal como o auricular o comunica. Num auricular que não comunica a bateria mostra «Não comunicado».',
    ru='Уровень заряда подключённой Bluetooth-гарнитуры в том виде, в каком его сообщает гарнитура. Если гарнитура не сообщает заряд, показано «Нет данных».',
    ko="연결된 Bluetooth 헤드셋의 배터리 잔량으로, 헤드셋이 보고한 값입니다. 배터리 잔량을 보고하지 않는 헤드셋은 '보고 없음'으로 표시됩니다.",
    zhT='已連接的 Bluetooth 耳機的電量，以耳機回報的為準。不回報電量的耳機會顯示「未回報」。',
    zhS='已连接的 Bluetooth 耳机的电量，以耳机报告的为准。不报告电量的耳机会显示“未报告”。',
    fi='Yhdistettyjen Bluetooth-kuulokkeiden akun varaus sellaisena kuin kuulokkeet sen ilmoittavat. Kuulokkeissa, jotka eivät ilmoita akun varausta, näkyy ”Ei ilmoitettu”.',
    sv='Batterinivån hos det anslutna Bluetooth-headsetet, så som headsetet rapporterar den. Ett headset som inte rapporterar sitt batteri visar ”Inte rapporterat”.',
    da='Batteriniveauet på det tilsluttede Bluetooth-headset, som headsettet rapporterer det. Et headset, der ikke rapporterer sit batteri, viser »Ikke rapporteret«.',
    no='Batterinivået til det tilkoblede Bluetooth-headsettet, slik headsettet rapporterer det. Et headset som ikke rapporterer batteriet, viser «Ikke rapportert».',
    pl='Poziom baterii podłączonego zestawu słuchawkowego Bluetooth, taki, jaki zgłasza zestaw. Zestaw, który nie zgłasza poziomu baterii, pokazuje „Brak danych”.',
    ptBR='O nível de bateria do headset Bluetooth conectado, como o headset informa. Um headset que não informa a bateria mostra "Não informado".',
    tr='Bağlı Bluetooth kulaklığın pil düzeyi, kulaklığın bildirdiği şekliyle. Pil düzeyini bildirmeyen bir kulaklıkta "Bildirilmedi" görünür.')
# The level as the row shows it: the number style (pct_text) with {n} where the number goes; the Settings module fills it in.
add('ITEM_PCT', **{l: pct_text(l, 0).replace('0', '{n}') for l in LANGS})
add('ITEM_NOTREPORTED',
    ja='報告なし',
    en='Not Reported',
    fr='Non communiqué',
    es='No notificado',
    de='Nicht gemeldet',
    it='Non segnalato',
    nl='Niet gemeld',
    ptPT='Não comunicado',
    ru='Нет данных',
    ko='보고 없음',
    zhT='未回報',
    zhS='未报告',
    fi='Ei ilmoitettu',
    sv='Inte rapporterat',
    da='Ikke rapporteret',
    no='Ikke rapportert',
    pl='Brak danych',
    ptBR='Não informado',
    tr='Bildirilmedi')
# The Headset Battery row's value while no headset is connected (Not Reported is for a connected headset that sends no level).
add('ITEM_NOTCONNECTED',
    ja='未接続',
    en='Not Connected',
    fr='Non connecté',
    es='No conectado',
    de='Nicht verbunden',
    it='Non connesso',
    nl='Niet verbonden',
    ptPT='Não ligado',
    ru='Не подключено',
    ko='연결 안 됨',
    zhT='未連線',
    zhS='未连接',
    fi='Ei yhdistetty',
    sv='Inte ansluten',
    da='Ikke tilsluttet',
    no='Ikke tilkoblet',
    pl='Brak połączenia',
    ptBR='Não conectado',
    tr='Bağlı değil')
add('ROW_READBATT',
    ja='ヘッドセットのバッテリー残量の読み取り',
    en='Read Headset Battery',
    fr='Lire la batterie du micro-casque',
    es='Leer batería del auricular',
    de='Headset-Akku auslesen',
    it='Leggi batteria della cuffia',
    nl='Headsetbatterij uitlezen',
    ptPT='Ler a bateria do auricular',
    ru='Чтение заряда гарнитуры',
    ko='헤드셋 배터리 읽기',
    zhT='讀取耳機電量',
    zhS='读取耳机电量',
    fi='Lue kuulokkeiden akku',
    sv='Läs headsetets batteri',
    da='Læs headsettets batteri',
    no='Les headsettets batteri',
    pl='Odczyt baterii zestawu',
    ptBR='Ler a bateria do headset',
    tr='Kulaklık pilini oku')
add('HINT_READBATT',
    ja='ヘッドセットがバッテリー残量とマイクに使うハンズフリー接続を開きます。オフにすると、本体標準のヘッドセット接続だけを使います。変更したあとは、Bluetooth機器でヘッドセットを削除し、もう一度ペアリングしてください。',
    en="Opens the hands-free connection that headsets use for their battery level and microphone. Off uses only the console's own headset connection. After changing it, remove the headset in Bluetooth Devices and pair it again.",
    fr="Ouvre la connexion mains libres que les micro-casques utilisent pour leur niveau de batterie et leur microphone. Désactivé, seule la connexion micro-casque propre à la console est utilisée. Après modification, supprimez le micro-casque dans Périphériques Bluetooth et associez-le de nouveau.",
    es='Abre la conexión de manos libres que usan los auriculares para su nivel de batería y su micrófono. Desactivado, solo se usa la conexión de auricular propia de la consola. Tras cambiarlo, elimina el auricular en Dispositivos Bluetooth y vuelve a emparejarlo.',
    de='Öffnet die Freisprechverbindung, die Headsets für Akkustand und Mikrofon nutzen. Aus: Es wird nur die eigene Headset-Verbindung der Konsole genutzt. Nach einer Änderung das Headset unter Bluetooth-Geräte entfernen und neu koppeln.',
    it='Apre la connessione vivavoce che le cuffie usano per il livello della batteria e il microfono. Disattivata, si usa solo la connessione cuffia propria della console. Dopo la modifica, rimuovi la cuffia in Dispositivi Bluetooth e abbinala di nuovo.',
    nl="Opent de handsfree-verbinding die headsets gebruiken voor hun batterijniveau en microfoon. Als dit uit staat, wordt alleen de eigen headsetverbinding van de console gebruikt. Verwijder na een wijziging de headset onder Bluetooth-apparaten en koppel hem opnieuw.",
    ptPT='Abre a ligação mãos-livres que os auriculares usam para o nível da bateria e o microfone. Desligado, usa apenas a ligação de auricular própria da consola. Depois de alterar, remova o auricular em Dispositivos Bluetooth e volte a emparelhá-lo.',
    ru='Открывает соединение Hands-Free, которое гарнитуры используют для уровня заряда и микрофона. В выключенном состоянии используется только собственное соединение консоли с гарнитурой. После изменения удалите гарнитуру в разделе «Устройства Bluetooth» и выполните сопряжение заново.',
    ko='헤드셋이 배터리 잔량과 마이크에 사용하는 핸즈프리 연결을 엽니다. 끄면 본체 자체의 헤드셋 연결만 사용합니다. 변경한 후에는 Bluetooth 기기에서 헤드셋을 삭제하고 다시 페어링하세요.',
    zhT='開啟耳機用於電量與麥克風的免持連線。關閉時只使用主機本身的耳機連線。變更後，請在 Bluetooth 裝置中移除耳機並重新配對。',
    zhS='开启耳机用于电量与麦克风的免提连接。关闭时只使用主机自身的耳机连接。更改后，请在 Bluetooth 设备中移除耳机并重新配对。',
    fi='Avaa handsfree-yhteyden, jota kuulokkeet käyttävät akun varaukseen ja mikrofoniin. Pois päältä käytetään vain konsolin omaa kuulokeyhteyttä. Muutoksen jälkeen poista kuulokkeet Bluetooth-laitteet-kohdassa ja yhdistä ne uudelleen.',
    sv='Öppnar handsfree-anslutningen som headset använder för batterinivå och mikrofon. Avstängt används bara konsolens egen headsetanslutning. Efter en ändring: ta bort headsetet under Bluetooth-enheter och parkoppla det igen.',
    da='Åbner handsfree-forbindelsen, som headset bruger til batteriniveau og mikrofon. Når den er slået fra, bruges kun konsollens egen headsetforbindelse. Efter en ændring: fjern headsettet under Bluetooth-enheder, og par det igen.',
    no='Åpner handsfree-forbindelsen som headset bruker til batterinivå og mikrofon. Når den er av, brukes bare konsollens egen headsetforbindelse. Etter en endring: fjern headsettet under Bluetooth-enheter og par det på nytt.',
    pl='Otwiera połączenie głośnomówiące, którego zestawy słuchawkowe używają do poziomu baterii i mikrofonu. Po wyłączeniu używane jest tylko własne połączenie konsoli z zestawem. Po zmianie usuń zestaw w sekcji „Urządzenia Bluetooth” i sparuj go ponownie.',
    ptBR='Abre a conexão viva-voz que os headsets usam para o nível da bateria e o microfone. Desligado, usa apenas a conexão de headset própria do console. Depois de alterar, remova o headset em Dispositivos Bluetooth e pareie-o de novo.',
    tr="Kulaklıkların pil düzeyi ve mikrofon için kullandığı eller serbest bağlantısını açar. Kapalıyken yalnızca konsolun kendi kulaklık bağlantısı kullanılır. Değiştirdikten sonra kulaklığı Bluetooth Cihazları'ndan kaldırıp yeniden eşleştirin.")
add('ROW_LOWBATT',
    ja='バッテリー残量低下の通知',
    en='Low Battery Notifications',
    fr='Notifications de batterie faible',
    es='Notificaciones de batería baja',
    de='Benachrichtigungen bei niedrigem Akku',
    it='Notifiche di batteria scarica',
    nl='Meldingen bij lage batterij',
    ptPT='Notificações de bateria fraca',
    ru='Уведомления о низком заряде',
    ko='배터리 부족 알림',
    zhT='低電量通知',
    zhS='低电量通知',
    fi='Alhaisen akun ilmoitukset',
    sv='Aviseringar om lågt batteri',
    da='Underretninger om lavt batteri',
    no='Varsler om lavt batteri',
    pl='Powiadomienia o niskiej baterii',
    ptBR='Notificações de bateria baixa',
    tr='Düşük pil bildirimleri')
add('HINT_LOWBATT',
    ja='接続中のBluetoothヘッドセットのバッテリー残量が20%、10%、5%まで下がると、システム通知でお知らせします。通知は残量が下がるごとに1回です。バッテリー残量を報告するヘッドセットが必要です。5%の通知には、10%より細かい刻みで報告するヘッドセットが必要です。',
    en='Shows a system notification when the battery of the connected Bluetooth headset falls to 20%, 10% and 5%, once for each drain. Needs a headset that reports its battery; 5% needs one that reports in steps finer than 10%.',
    fr="Affiche une notification système lorsque la batterie du micro-casque Bluetooth connecté descend à 20 %, 10 % et 5 %, une fois par décharge. Nécessite un micro-casque qui communique sa batterie ; 5 % nécessite un micro-casque qui l'indique par pas inférieurs à 10 %.",
    es='Muestra una notificación del sistema cuando la batería del auricular Bluetooth conectado baja al 20%, 10% y 5%, una vez en cada descarga. Requiere un auricular que notifique su batería; el 5% requiere uno que la notifique en pasos menores de 10%.',
    de='Zeigt eine Systembenachrichtigung, wenn der Akku des verbundenen Bluetooth-Headsets auf 20 %, 10 % und 5 % fällt, einmal pro Entladung. Erfordert ein Headset, das seinen Akkustand meldet; für 5 % muss es in Schritten unter 10 % melden.',
    it='Mostra una notifica di sistema quando la batteria della cuffia Bluetooth collegata scende al 20%, 10% e 5%, una volta per ogni scaricamento. Richiede una cuffia che segnali la batteria; per il 5% serve una cuffia che la segnali a passi più fini del 10%.',
    nl='Toont een systeemmelding wanneer de batterij van de verbonden Bluetooth-headset daalt tot 20%, 10% en 5%, één keer per ontlading. Vereist een headset die zijn batterij meldt; voor 5% moet die in stappen kleiner dan 10% melden.',
    ptPT='Mostra uma notificação do sistema quando a bateria do auricular Bluetooth ligado desce para 20%, 10% e 5%, uma vez por descarga. Requer um auricular que comunique a bateria; os 5% requerem um que a comunique em passos inferiores a 10%.',
    ru='Показывает системное уведомление, когда заряд подключённой Bluetooth-гарнитуры падает до 20%, 10% и 5%, по одному разу за разряд. Нужна гарнитура, которая сообщает заряд; для 5% она должна сообщать заряд шагом мельче 10%.',
    ko='연결된 Bluetooth 헤드셋의 배터리가 20%, 10%, 5%로 떨어지면 시스템 알림을 표시하며, 방전 한 번에 각각 한 번씩 표시합니다. 배터리 잔량을 보고하는 헤드셋이 필요하며, 5% 알림에는 10%보다 세밀한 단위로 보고하는 헤드셋이 필요합니다.',
    zhT='當已連接的 Bluetooth 耳機電量降到 20%、10% 和 5% 時顯示系統通知，每次放電各通知一次。需要會回報電量的耳機；5% 的通知需要以小於 10% 的級距回報的耳機。',
    zhS='当已连接的 Bluetooth 耳机电量降到 20%、10% 和 5% 时显示系统通知，每次放电各通知一次。需要会报告电量的耳机；5% 的通知需要以小于 10% 的级距报告的耳机。',
    fi='Näyttää järjestelmäilmoituksen, kun yhdistettyjen Bluetooth-kuulokkeiden akku laskee 20 %:iin, 10 %:iin ja 5 %:iin, kerran kutakin purkautumista kohden. Vaatii kuulokkeet, jotka ilmoittavat akun varauksen; 5 % vaatii kuulokkeet, jotka ilmoittavat sen alle 10 %:n askelin.',
    sv='Visar en systemavisering när batteriet i det anslutna Bluetooth-headsetet sjunker till 20 %, 10 % och 5 %, en gång per urladdning. Kräver ett headset som rapporterar sitt batteri; 5 % kräver ett som rapporterar i steg finare än 10 %.',
    da='Viser en systemunderretning, når batteriet i det tilsluttede Bluetooth-headset falder til 20 %, 10 % og 5 %, én gang pr. afladning. Kræver et headset, der rapporterer sit batteri; 5 % kræver et, der rapporterer i trin finere end 10 %.',
    no='Viser et systemvarsel når batteriet i det tilkoblede Bluetooth-headsettet faller til 20 %, 10 % og 5 %, én gang per utlading. Krever et headset som rapporterer batteriet; 5 % krever et som rapporterer i trinn finere enn 10 %.',
    pl='Pokazuje powiadomienie systemowe, gdy bateria podłączonego zestawu słuchawkowego Bluetooth spadnie do 20%, 10% i 5%, raz przy każdym rozładowaniu. Wymaga zestawu, który zgłasza poziom baterii; 5% wymaga zestawu zgłaszającego go w krokach drobniejszych niż 10%.',
    ptBR='Mostra uma notificação do sistema quando a bateria do headset Bluetooth conectado cai para 20%, 10% e 5%, uma vez por descarga. Requer um headset que informe a bateria; os 5% exigem um que informe em passos menores que 10%.',
    tr="Bağlı Bluetooth kulaklığın pili %20, %10 ve %5'e düştüğünde bir sistem bildirimi gösterir, her boşalmada bir kez. Pil düzeyini bildiren bir kulaklık gerekir; %5 için %10'dan ince adımlarla bildiren bir kulaklık gerekir.")

# ---- 0.2.0: the AAC rows (Bluetooth Codec, AAC Bit Rate, Hardware Accelerated AAC) and the two rows they change ----
# Codec names and the bit rates are the same in every language.
add('ITEM_CODEC_SBC', **{l: 'SBC' for l in LANGS})
add('ITEM_CODEC_AAC', **{l: 'AAC' for l in LANGS})
add('ITEM_CODEC_APTX', **{l: 'aptX™' for l in LANGS})
add('ITEM_CODEC_LDAC', **{l: 'LDAC™' for l in LANGS})
add('ITEM_LDAC_330', **{l: '330 kb/s' for l in LANGS})
add('ITEM_LDAC_660', **{l: '660 kb/s' for l in LANGS})
add('ITEM_LDAC_ABR',
    ja='自動調整',
    en='Adaptive',
    fr='Adaptatif',
    es='Adaptativo',
    de='Adaptiv',
    it='Adattivo',
    nl='Adaptief',
    ptPT='Adaptável',
    ru='Адаптивный',
    ko='적응형',
    zhT='自適應',
    zhS='自适应',
    fi='Mukautuva',
    sv='Adaptiv',
    da='Adaptiv',
    no='Adaptiv',
    pl='Adaptacyjny',
    ptBR='Adaptável',
    tr='Uyarlanabilir')
add('ROW_LDACRATE',
    ja='LDACビットレート',
    en='LDAC Bit Rate',
    fr='Débit LDAC',
    es='Tasa de bits de LDAC',
    de='LDAC-Bitrate',
    it='Bit rate LDAC',
    nl='LDAC-bitrate',
    ptPT='Débito de LDAC',
    ru='Битрейт LDAC',
    ko='LDAC 비트레이트',
    zhT='LDAC 位元率',
    zhS='LDAC 码率',
    fi='LDAC-bittinopeus',
    sv='LDAC-bithastighet',
    da='LDAC-bithastighed',
    no='LDAC-bithastighet',
    pl='Przepływność LDAC',
    ptBR='Taxa de bits do LDAC',
    tr='LDAC bit hızı')
for _r in (128, 192, 256, 320):
    add('ITEM_RATE_%d' % _r, **{l: '%d kb/s' % _r for l in LANGS})

add('ROW_CODEC',
    ja='Bluetoothコーデック',
    en='Bluetooth Codec',
    fr='Codec Bluetooth',
    es='Códec Bluetooth',
    de='Bluetooth-Codec',
    it='Codec Bluetooth',
    nl='Bluetooth-codec',
    ptPT='Codec Bluetooth',
    ru='Кодек Bluetooth',
    ko='Bluetooth 코덱',
    zhT='Bluetooth 編解碼器',
    zhS='Bluetooth 编解码器',
    fi='Bluetooth-koodekki',
    sv='Bluetooth-codec',
    da='Bluetooth-codec',
    no='Bluetooth-kodek',
    pl='Kodek Bluetooth',
    ptBR='Codec Bluetooth',
    tr='Bluetooth codec')
# Three hints for the one row, by what the console can offer: AAC or aptX usable, neither add-on installed, an add-on there but neither usable (yet).
add('HINT_CODEC',
    ja='SBCは本体の標準のコーデックです。AAC、aptX、LDACは、それぞれのアドオンを入れている場合に、対応したヘッドホンにだけ、どちら側から接続した場合でも送ります。AACとLDACは下のコーデック設定で選んだビットレートで、aptXは、ヘッドホンが対応していて「可能ならaptX HD」がオンのとき、aptX HD（試験的）で送ります。ほかのヘッドホンはSBCのままです。ヘッドホンは変更を反映するため、一時的に再接続されます。SBCに戻したあとヘッドホンから音が出ない場合は、ペアリングを解除してからもう一度ペアリングしてください。',
    en="SBC is the console's default codec. AAC, aptX and LDAC, if their add-ons are installed, are sent only to headphones that offer them, whichever side connects them: AAC and LDAC at the bit rates set in Codec Settings, aptX as aptX HD (experimental) when the headphones offer it and aptX HD When Available is on. All other headphones keep SBC. The headphones reconnect briefly to apply a change. If headphones stay silent after you go back to SBC, remove them and pair them again.",
    fr="SBC est le codec par défaut de la console. AAC, aptX et LDAC, si leurs modules complémentaires sont installés, ne sont envoyés qu'aux écouteurs qui les proposent, quel que soit le côté qui lance la connexion : AAC et LDAC aux débits réglés dans Paramètres du codec, aptX en aptX HD (expérimental) si les écouteurs le proposent et que « aptX HD si disponible » est activé. Les autres écouteurs gardent SBC. Les écouteurs se reconnectent brièvement pour appliquer le changement. Si des écouteurs restent muets après un retour à SBC, supprimez-les puis associez-les de nouveau.",
    es='SBC es el códec predeterminado de la consola. AAC, aptX y LDAC, si sus complementos están instalados, se envían solo a los auriculares que los admiten, sea cual sea el lado que inicie la conexión: AAC y LDAC a las tasas de bits fijadas en Ajustes del códec, aptX como aptX HD (experimental) si los auriculares lo ofrecen y «aptX HD si está disponible» está activado. Los demás auriculares siguen con SBC. Los auriculares se reconectan brevemente para aplicar el cambio. Si unos auriculares se quedan sin sonido al volver a SBC, elimínalos y vuelve a emparejarlos.',
    de='SBC ist der Standard-Codec der Konsole. AAC, aptX und LDAC werden, wenn ihre Zusatzmodule installiert sind, nur an Kopfhörer gesendet, die sie unterstützen, egal welche Seite die Verbindung aufbaut: AAC und LDAC mit den unter Codec-Einstellungen gewählten Bitraten, aptX als aptX HD (experimentell), wenn die Kopfhörer das anbieten und „aptX HD, wenn verfügbar“ eingeschaltet ist. Alle anderen Kopfhörer bleiben bei SBC. Die Kopfhörer verbinden sich kurz neu, um die Änderung zu übernehmen. Bleiben Kopfhörer nach der Rückkehr zu SBC stumm, entferne sie und koppele sie neu.',
    it='SBC è il codec predefinito della console. AAC, aptX e LDAC, se i loro componenti aggiuntivi sono installati, vengono inviati solo alle cuffie che li supportano, da qualunque lato parta la connessione: AAC e LDAC ai bit rate impostati in Impostazioni codec, aptX come aptX HD (sperimentale) se le cuffie lo offrono e «aptX HD se disponibile» è attivo. Le altre cuffie restano su SBC. Le cuffie si riconnettono brevemente per applicare la modifica. Se le cuffie restano mute dopo il ritorno a SBC, rimuovile e abbinale di nuovo.',
    nl='SBC is de standaardcodec van de console. AAC, aptX en LDAC worden, als hun uitbreidingen zijn geïnstalleerd, alleen gestuurd naar koptelefoons die ze ondersteunen, ongeacht welke kant de verbinding start: AAC en LDAC met de bij Codec-instellingen gekozen bitrates, aptX als aptX HD (experimenteel) als de koptelefoon dat aanbiedt en \'aptX HD indien beschikbaar\' aanstaat. Alle andere koptelefoons houden SBC. De koptelefoons maken kort opnieuw verbinding om de wijziging toe te passen. Blijft een koptelefoon stil nadat je naar SBC bent teruggegaan, verwijder hem dan en koppel hem opnieuw.',
    ptPT='SBC é o codec predefinido da consola. O AAC, o aptX e o LDAC, se os seus extras estiverem instalados, só são enviados para auscultadores que os suportem, seja qual for o lado que inicie a ligação: o AAC e o LDAC aos débitos definidos em Definições do codec, o aptX como aptX HD (experimental) se os auscultadores o oferecerem e «aptX HD quando disponível» estiver ativado. Os restantes auscultadores continuam em SBC. Os auscultadores voltam a ligar-se brevemente para aplicar a alteração. Se uns auscultadores ficarem mudos depois de voltar ao SBC, remova-os e volte a emparelhá-los.',
    ru='SBC — кодек консоли по умолчанию. AAC, aptX и LDAC, если установлены их дополнения, передаются только на наушники, которые их поддерживают, независимо от того, какая сторона устанавливает соединение: AAC и LDAC — с битрейтами, заданными в настройках кодека, aptX — как aptX HD (экспериментально), если наушники его предлагают и включён пункт «aptX HD, если доступен». Остальные наушники остаются на SBC. Наушники ненадолго переподключаются, чтобы применить изменение. Если после возврата к SBC наушники молчат, удалите их из списка и выполните сопряжение заново.',
    ko='SBC는 본체의 기본 코덱입니다. AAC, aptX, LDAC는 각 추가 모듈이 설치되어 있을 때, 해당 방식을 지원하는 헤드폰에만 어느 쪽에서 연결하든 보냅니다. AAC와 LDAC는 코덱 설정에서 정한 비트레이트로, aptX는 헤드폰이 지원하고 \'가능하면 aptX HD 사용\'이 켜져 있으면 aptX HD(실험적)로 보냅니다. 그 밖의 헤드폰은 SBC를 계속 사용합니다. 변경 사항을 적용하기 위해 헤드폰이 잠시 다시 연결됩니다. SBC로 되돌린 뒤 헤드폰에서 소리가 나지 않으면 헤드폰 등록을 삭제하고 다시 페어링하세요.',
    zhT='SBC 是主機的預設編解碼器。若已安裝 AAC、aptX 與 LDAC 的附加模組，它們只會傳送給支援的耳機，無論由哪一方發起連線：AAC 與 LDAC 使用編解碼器設定中所設定的位元率；若耳機支援 aptX HD（實驗性）且已開啟「可用時使用 aptX HD」，aptX 就以 aptX HD 傳送。其他耳機仍使用 SBC。耳機會短暫重新連線以套用變更。若改回 SBC 後耳機沒有聲音，請移除耳機並重新配對。',
    zhS='SBC 是主机的默认编解码器。如果已安装 AAC、aptX 和 LDAC 的附加模块，它们只会发送给支持的耳机，无论由哪一方发起连接：AAC 和 LDAC 使用编解码器设置中所设置的码率；如果耳机支持 aptX HD（实验性）且已开启“可用时使用 aptX HD”，aptX 就以 aptX HD 发送。其他耳机仍使用 SBC。耳机会短暂重新连接以应用更改。如果改回 SBC 后耳机没有声音，请移除耳机并重新配对。',
    fi='SBC on konsolin oletuskoodekki. AAC, aptX ja LDAC, jos niiden lisäosat on asennettu, lähetetään vain kuulokkeille, jotka tukevat niitä, riippumatta siitä, kumpi osapuoli muodostaa yhteyden: AAC ja LDAC koodekin asetuksissa asetetuilla bittinopeuksilla, aptX aptX HD:nä (kokeellinen), jos kuulokkeet tarjoavat sitä ja ”aptX HD, kun saatavilla” on päällä. Muut kuulokkeet käyttävät edelleen SBC:tä. Kuulokkeet yhdistyvät hetkeksi uudelleen muutoksen käyttöön ottamiseksi. Jos kuulokkeet pysyvät hiljaa SBC:hen palaamisen jälkeen, poista ne ja muodosta laitepari uudelleen.',
    sv='SBC är konsolens standardcodec. AAC, aptX och LDAC, om deras tillägg är installerade, skickas bara till hörlurar som stöder dem, oavsett vilken sida som ansluter: AAC och LDAC med bithastigheterna som anges i Codec-inställningar, aptX som aptX HD (experimentellt) om hörlurarna erbjuder det och ”aptX HD när det finns” är på. Alla andra hörlurar behåller SBC. Hörlurarna ansluter kort på nytt för att tillämpa ändringen. Om hörlurar förblir tysta efter att du gått tillbaka till SBC, ta bort dem och parkoppla dem igen.',
    da='SBC er konsollens standardcodec. AAC, aptX og LDAC, hvis deres tilføjelsesmoduler er installeret, sendes kun til hovedtelefoner, der understøtter dem, uanset hvilken side der opretter forbindelsen: AAC og LDAC med de bithastigheder, der er angivet i Codec-indstillinger, aptX som aptX HD (eksperimentelt), hvis hovedtelefonerne tilbyder det og »aptX HD når det er muligt« er slået til. Alle andre hovedtelefoner beholder SBC. Hovedtelefonerne forbindes kort igen for at anvende ændringen. Hvis hovedtelefoner forbliver tavse, efter at du er gået tilbage til SBC, så fjern dem og par dem igen.',
    no='SBC er konsollens standardkodek. AAC, aptX og LDAC, hvis tilleggene deres er installert, sendes bare til hodetelefoner som støtter dem, uansett hvilken side som kobler til: AAC og LDAC med bithastighetene som er satt i Kodekinnstillinger, aptX som aptX HD (eksperimentelt) hvis hodetelefonene tilbyr det og «aptX HD når tilgjengelig» er på. Alle andre hodetelefoner beholder SBC. Hodetelefonene kobles kort til igjen for å bruke endringen. Hvis hodetelefoner forblir stille etter at du har gått tilbake til SBC, fjern dem og par dem på nytt.',
    pl='SBC to domyślny kodek konsoli. AAC, aptX i LDAC, jeśli ich dodatki są zainstalowane, są wysyłane tylko do słuchawek, które je obsługują, niezależnie od tego, która strona nawiązuje połączenie: AAC i LDAC z przepływnościami ustawionymi w Ustawieniach kodeka, aptX jako aptX HD (eksperymentalnie), jeśli słuchawki to oferują i włączona jest opcja „aptX HD, gdy dostępne”. Pozostałe słuchawki zostają przy SBC. Słuchawki na chwilę łączą się ponownie, aby zastosować zmianę. Jeśli po powrocie do SBC słuchawki milczą, usuń je i sparuj ponownie.',
    ptBR='SBC é o codec padrão do console. O AAC, o aptX e o LDAC, se os complementos deles estiverem instalados, só são enviados para fones de ouvido que os suportem, não importa qual lado inicie a conexão: o AAC e o LDAC nas taxas de bits definidas em Configurações do codec, o aptX como aptX HD (experimental) se os fones de ouvido o oferecerem e "aptX HD quando disponível" estiver ligado. Os demais fones de ouvido continuam em SBC. Os fones de ouvido se reconectam brevemente para aplicar a mudança. Se os fones de ouvido ficarem mudos depois de voltar ao SBC, remova-os e pareie de novo.',
    tr="SBC, konsolun varsayılan codec'idir. AAC, aptX ve LDAC, eklentileri kuruluysa, yalnızca onları destekleyen kulaklıklara, bağlantıyı hangi taraf başlatırsa başlatsın gönderilir: AAC ve LDAC, Codec ayarları'nda belirlenen bit hızlarıyla, aptX ise kulaklık sunuyorsa ve \"Varsa aptX HD\" açıksa aptX HD (deneysel) olarak. Diğer kulaklıklar SBC kullanmaya devam eder. Değişikliği uygulamak için kulaklıklar kısa süreliğine yeniden bağlanır. SBC'ye döndükten sonra kulaklıklar sessiz kalırsa onları kaldırıp yeniden eşleştirin.")
add('HINT_CODEC_NOADDON',
    ja='使用できるのは本体の標準のコーデックであるSBCだけです。AAC、aptX、LDACは任意のアドオンとして、GitHubのBluetooth Turboのページ（リリース）で入手できます。',
    en="Only SBC, the console's default codec, can be used. AAC, aptX and LDAC come as optional add-ons, available from the Bluetooth Turbo page on GitHub (the releases).",
    fr="Seul SBC, le codec par défaut de la console, est utilisable. AAC, aptX et LDAC sont des modules complémentaires facultatifs, disponibles sur la page Bluetooth Turbo de GitHub (les versions publiées).",
    es='Solo se puede usar SBC, el códec predeterminado de la consola. AAC, aptX y LDAC son complementos opcionales, disponibles en la página de Bluetooth Turbo en GitHub (las versiones publicadas).',
    de='Nur SBC, der Standard-Codec der Konsole, kann verwendet werden. AAC, aptX und LDAC gibt es als optionale Zusatzmodule auf der Seite von Bluetooth Turbo auf GitHub (unter den Veröffentlichungen).',
    it='È possibile usare solo SBC, il codec predefinito della console. AAC, aptX e LDAC sono componenti aggiuntivi facoltativi, disponibili nella pagina di Bluetooth Turbo su GitHub (le release).',
    nl='Alleen SBC, de standaardcodec van de console, is te gebruiken. AAC, aptX en LDAC zijn optionele uitbreidingen, te vinden op de Bluetooth Turbo-pagina op GitHub (de releases).',
    ptPT='Só é possível usar o SBC, o codec predefinido da consola. O AAC, o aptX e o LDAC são extras opcionais, disponíveis na página do Bluetooth Turbo no GitHub (as versões publicadas).',
    ru='Доступен только SBC — кодек консоли по умолчанию. AAC, aptX и LDAC поставляются как необязательные дополнения на странице Bluetooth Turbo на GitHub (в разделе релизов).',
    ko='본체의 기본 코덱인 SBC만 사용할 수 있습니다. AAC, aptX, LDAC는 선택 사항인 추가 모듈로, GitHub의 Bluetooth Turbo 페이지(릴리스)에서 받을 수 있습니다.',
    zhT='目前只能使用主機的預設編解碼器 SBC。AAC、aptX 與 LDAC 是選用的附加模組，可從 GitHub 上的 Bluetooth Turbo 頁面（發行版本）取得。',
    zhS='目前只能使用主机的默认编解码器 SBC。AAC、aptX 和 LDAC 是可选的附加模块，可从 GitHub 上的 Bluetooth Turbo 页面（发布版本）获取。',
    fi='Vain SBC, konsolin oletuskoodekki, on käytettävissä. AAC, aptX ja LDAC ovat valinnaisia lisäosia, jotka saa Bluetooth Turbon GitHub-sivulta (julkaisuista).',
    sv='Bara SBC, konsolens standardcodec, kan användas. AAC, aptX och LDAC finns som valfria tillägg på Bluetooth Turbos sida på GitHub (under releaser).',
    da='Kun SBC, konsollens standardcodec, kan bruges. AAC, aptX og LDAC findes som valgfrie tilføjelsesmoduler på Bluetooth Turbos side på GitHub (under udgivelser).',
    no='Bare SBC, konsollens standardkodek, kan brukes. AAC, aptX og LDAC finnes som valgfrie tillegg på Bluetooth Turbo-siden på GitHub (under utgivelser).',
    pl='Można używać tylko SBC, domyślnego kodeka konsoli. AAC, aptX i LDAC są opcjonalnymi dodatkami, dostępnymi na stronie Bluetooth Turbo w serwisie GitHub (w wydaniach).',
    ptBR='Só é possível usar o SBC, o codec padrão do console. O AAC, o aptX e o LDAC são complementos opcionais, disponíveis na página do Bluetooth Turbo no GitHub (as versões publicadas).',
    tr="Yalnızca konsolun varsayılan codec'i olan SBC kullanılabilir. AAC, aptX ve LDAC, GitHub'daki Bluetooth Turbo sayfasından (sürümler) edinilebilen isteğe bağlı eklentilerdir.")
add('HINT_CODEC_WAIT',
    ja='現在AAC、aptX、LDACは使用できません。それぞれ、本体の起動から約45秒後、起動時の自己診断に合格すると選べるようになります。診断に失敗した場合や、前回の使用が異常終了した場合は、選べません。それまでは本体の標準のコーデックであるSBCだけが選べます。',
    en="AAC, aptX and LDAC cannot be used right now. Each is offered about 45 seconds after the console starts, once its start-up self-check has passed, and not at all if that check failed or its last session ended badly. Until then only SBC, the console's default codec, is offered.",
    fr="AAC, aptX et LDAC sont indisponibles pour le moment. Chacun est proposé environ 45 secondes après le démarrage de la console, une fois son test automatique réussi, et jamais si le test automatique a échoué ou si sa dernière session s'est mal terminée. D'ici là, seul SBC, le codec par défaut de la console, est proposé.",
    es='AAC, aptX y LDAC no se pueden usar ahora mismo. Cada uno se ofrece aproximadamente 45 segundos después de arrancar la consola, cuando supera su autocomprobación, y no se ofrece si esta falló o si su última sesión terminó mal. Mientras tanto solo se ofrece SBC, el códec predeterminado de la consola.',
    de='AAC, aptX und LDAC lassen sich gerade nicht verwenden. Jedes wird etwa 45 Sekunden nach dem Start der Konsole angeboten, sobald seine Selbstprüfung bestanden ist, und gar nicht, wenn die Prüfung fehlschlug oder seine letzte Sitzung schlecht endete. Bis dahin wird nur SBC, der Standard-Codec der Konsole, angeboten.',
    it="AAC, aptX e LDAC al momento non sono utilizzabili. Ciascuno viene offerto circa 45 secondi dopo l'avvio della console, quando ha superato il test automatico, e non viene offerto affatto se il test è fallito o se la sua ultima sessione è terminata male. Fino ad allora è offerto solo SBC, il codec predefinito della console.",
    nl='AAC, aptX en LDAC zijn nu niet te gebruiken. Elk wordt ongeveer 45 seconden na het opstarten van de console aangeboden, zodra de zelftest is geslaagd, en helemaal niet als die test mislukte of de laatste sessie slecht eindigde. Tot dan wordt alleen SBC, de standaardcodec van de console, aangeboden.',
    ptPT='O AAC, o aptX e o LDAC não podem ser usados neste momento. Cada um é oferecido cerca de 45 segundos depois de a consola arrancar, quando passa na sua autoverificação, e não é oferecido de todo se a verificação falhar ou se a sua última sessão terminar mal. Até lá, só é oferecido o SBC, o codec predefinido da consola.',
    ru='AAC, aptX и LDAC сейчас использовать нельзя. Каждый становится доступен примерно через 45 секунд после запуска консоли, когда пройдена его самопроверка, и не будет доступен, если проверка не пройдена или его последний сеанс завершился сбоем. До этого доступен только SBC — кодек консоли по умолчанию.',
    ko='지금은 AAC, aptX, LDAC를 사용할 수 없습니다. 각각 본체를 켠 지 약 45초 뒤, 시작 시 자체 점검을 통과하면 선택할 수 있게 됩니다. 점검에 실패했거나 마지막 사용이 비정상으로 끝난 경우에는 선택할 수 없습니다. 그때까지는 본체의 기본 코덱인 SBC만 선택할 수 있습니다.',
    zhT='目前無法使用 AAC、aptX 與 LDAC。三者各自在主機啟動約 45 秒後，通過啟動時的自我檢查才會開放；若檢查失敗，或上一次連線異常結束，則不會開放。在此之前只能選擇主機的預設編解碼器 SBC。',
    zhS='目前无法使用 AAC、aptX 和 LDAC。三者各自在主机启动约 45 秒后，通过启动时的自检才会开放；如果检查失败，或上一次连接异常结束，则不会开放。在此之前只能选择主机的默认编解码器 SBC。',
    fi='AAC, aptX ja LDAC eivät ole nyt käytettävissä. Kukin tarjotaan noin 45 sekunnin kuluttua konsolin käynnistymisestä, kun sen käynnistysitsetarkastus on läpäisty, eikä lainkaan, jos tarkastus epäonnistui tai sen viimeinen istunto päättyi huonosti. Siihen asti tarjolla on vain SBC, konsolin oletuskoodekki.',
    sv='AAC, aptX och LDAC kan inte användas just nu. Var och en erbjuds ungefär 45 sekunder efter att konsolen startat, när dess självtest har godkänts, och inte alls om testet misslyckades eller dess senaste session tog slut på fel sätt. Tills dess erbjuds bara SBC, konsolens standardcodec.',
    da='AAC, aptX og LDAC kan ikke bruges lige nu. Hver af dem tilbydes cirka 45 sekunder efter, at konsollen er startet, når dens selvtest er bestået, og slet ikke, hvis testen mislykkedes, eller den seneste session sluttede dårligt. Indtil da tilbydes kun SBC, konsollens standardcodec.',
    no='AAC, aptX og LDAC kan ikke brukes akkurat nå. Hver av dem tilbys omtrent 45 sekunder etter at konsollen har startet, når selvtesten er bestått, og ikke i det hele tatt hvis testen mislyktes eller den siste økten endte dårlig. Inntil da tilbys bare SBC, konsollens standardkodek.',
    pl='AAC, aptX i LDAC nie są teraz dostępne. Każdy pojawia się około 45 sekund po uruchomieniu konsoli, gdy przejdzie samokontrolę, i wcale, jeśli kontrola się nie powiodła lub jego ostatnia sesja zakończyła się błędem. Do tego czasu dostępny jest tylko SBC, domyślny kodek konsoli.',
    ptBR='O AAC, o aptX e o LDAC não podem ser usados agora. Cada um é oferecido cerca de 45 segundos depois que o console liga, quando passa na autoverificação, e não é oferecido de jeito nenhum se a verificação falhar ou se a última sessão terminar mal. Até lá, só o SBC, o codec padrão do console, é oferecido.',
    tr="AAC, aptX ve LDAC şu anda kullanılamaz. Her biri, konsol açıldıktan yaklaşık 45 saniye sonra, başlangıç öz denetimi geçildiğinde sunulur; denetim başarısız olduysa veya son oturumu kötü bittiyse hiç sunulmaz. O zamana kadar yalnızca konsolun varsayılan codec'i olan SBC sunulur.")

add('ROW_AACRATE',
    ja='AACビットレート',
    en='AAC Bit Rate',
    fr='Débit AAC',
    es='Tasa de bits de AAC',
    de='AAC-Bitrate',
    it='Bit rate AAC',
    nl='AAC-bitrate',
    ptPT='Débito de AAC',
    ru='Битрейт AAC',
    ko='AAC 비트레이트',
    zhT='AAC 位元率',
    zhS='AAC 码率',
    fi='AAC-bittinopeus',
    sv='AAC-bithastighet',
    da='AAC-bithastighed',
    no='AAC-bithastighet',
    pl='Przepływność AAC',
    ptBR='Taxa de bits do AAC',
    tr='AAC bit hızı')

add('ROW_AACACCEL',
    ja='ハードウェアアクセラレーションAAC',
    en='Hardware Accelerated AAC',
    fr='AAC accéléré matériellement',
    es='AAC con aceleración por hardware',
    de='Hardwarebeschleunigtes AAC',
    it='AAC con accelerazione hardware',
    nl='Hardwaregeversnelde AAC',
    ptPT='AAC com aceleração por hardware',
    ru='Аппаратное ускорение AAC',
    ko='AAC 하드웨어 가속',
    zhT='AAC 硬體加速',
    zhS='AAC 硬件加速',
    fi='Laitteistokiihdytetty AAC',
    sv='Hårdvaruaccelererad AAC',
    da='Hardwareaccelereret AAC',
    no='Maskinvareakselerert AAC',
    pl='Sprzętowo przyspieszane AAC',
    ptBR='AAC com aceleração por hardware',
    tr='Donanım hızlandırmalı AAC')

add('ROW_APTXHD',
    ja='可能ならaptX HD',
    en='aptX HD When Available',
    fr='aptX HD si disponible',
    es='aptX HD si está disponible',
    de='aptX HD, wenn verfügbar',
    it='aptX HD se disponibile',
    nl='aptX HD indien beschikbaar',
    ptPT='aptX HD quando disponível',
    ru='aptX HD, если доступен',
    ko='가능하면 aptX HD 사용',
    zhT='可用時使用 aptX HD',
    zhS='可用时使用 aptX HD',
    fi='aptX HD, kun saatavilla',
    sv='aptX HD när det finns',
    da='aptX HD når det er muligt',
    no='aptX HD når tilgjengelig',
    pl='aptX HD, gdy dostępne',
    ptBR='aptX HD quando disponível',
    tr='Varsa aptX HD')

# The Codec Settings sub-page, directly under Bluetooth Codec: the rows that belong to one codec (SBC Audio Quality, AAC Bit Rate, Hardware Accelerated AAC).
add('ROW_CODECSET',
    ja='コーデック設定',
    en='Codec Settings',
    fr='Paramètres du codec',
    es='Ajustes del códec',
    de='Codec-Einstellungen',
    it='Impostazioni codec',
    nl='Codec-instellingen',
    ptPT='Definições do codec',
    ru='Настройки кодека',
    ko='코덱 설정',
    zhT='編解碼器設定',
    zhS='编解码器设置',
    fi='Koodekin asetukset',
    sv='Codec-inställningar',
    da='Codec-indstillinger',
    no='Kodekinnstillinger',
    pl='Ustawienia kodeka',
    ptBR='Configurações do codec',
    tr='Codec ayarları')
add('HINT_CODECSET',
    ja='コーデックごとの設定です。「SBCオーディオ品質」、AACアドオンがある場合は「AACビットレート」と「ハードウェアアクセラレーションAAC」、LDACアドオンがある場合は「LDACビットレート」（すぐに反映されます）、aptXアドオンがある場合は「可能ならaptX HD」を設定できます。「LDACビットレート」以外は、変更を反映するため、ヘッドホンが一時的に再接続されます。',
    en='The settings that belong to each codec: SBC Audio Quality; with the AAC add-on installed, AAC Bit Rate and Hardware Accelerated AAC; with the LDAC add-on, LDAC Bit Rate, which applies at once; with the aptX add-on, aptX HD When Available. The headphones reconnect briefly to apply a change to any of them except LDAC Bit Rate.',
    fr='Les réglages propres à chaque codec : « Qualité audio SBC » ; avec le module complémentaire AAC, « Débit AAC » et « AAC accéléré matériellement » ; avec le module complémentaire LDAC, « Débit LDAC », appliqué aussitôt ; avec le module complémentaire aptX, « aptX HD si disponible ». Sauf pour « Débit LDAC », les écouteurs se reconnectent brièvement pour appliquer un changement.',
    es='Los ajustes propios de cada códec: «Calidad de audio SBC»; con el complemento de AAC, «Tasa de bits de AAC» y «AAC con aceleración por hardware»; con el complemento de LDAC, «Tasa de bits de LDAC», que se aplica al instante; con el complemento de aptX, «aptX HD si está disponible». Salvo «Tasa de bits de LDAC», los auriculares se reconectan brevemente para aplicar un cambio.',
    de='Die Einstellungen, die zu den einzelnen Codecs gehören: „SBC-Audioqualität“; mit dem AAC-Zusatzmodul „AAC-Bitrate“ und „Hardwarebeschleunigtes AAC“; mit dem LDAC-Zusatzmodul „LDAC-Bitrate“, die sofort gilt; mit dem aptX-Zusatzmodul „aptX HD, wenn verfügbar“. Außer bei „LDAC-Bitrate“ verbinden sich die Kopfhörer kurz neu, um eine Änderung zu übernehmen.',
    it='Le impostazioni di ciascun codec: «Qualità audio SBC»; con il componente aggiuntivo AAC, «Bit rate AAC» e «AAC con accelerazione hardware»; con il componente aggiuntivo LDAC, «Bit rate LDAC», che si applica subito; con il componente aggiuntivo aptX, «aptX HD se disponibile». Tranne per «Bit rate LDAC», le cuffie si riconnettono brevemente per applicare una modifica.',
    nl="De instellingen die bij elke codec horen: 'SBC-audiokwaliteit'; met de AAC-uitbreiding 'AAC-bitrate' en 'Hardwaregeversnelde AAC'; met de LDAC-uitbreiding 'LDAC-bitrate', die direct ingaat; met de aptX-uitbreiding 'aptX HD indien beschikbaar'. Behalve bij 'LDAC-bitrate' maken de koptelefoons kort opnieuw verbinding om een wijziging toe te passen.",
    ptPT='As definições de cada codec: «Qualidade de áudio SBC»; com o extra de AAC, «Débito de AAC» e «AAC com aceleração por hardware»; com o extra de LDAC, «Débito de LDAC», que se aplica de imediato; com o extra de aptX, «aptX HD quando disponível». Exceto em «Débito de LDAC», os auscultadores voltam a ligar-se brevemente para aplicar uma alteração.',
    ru='Настройки, относящиеся к каждому кодеку: «Качество звука SBC»; при установленном дополнении AAC — «Битрейт AAC» и «Аппаратное ускорение AAC»; при установленном дополнении LDAC — «Битрейт LDAC», он действует сразу; при установленном дополнении aptX — «aptX HD, если доступен». Кроме параметра «Битрейт LDAC», наушники ненадолго переподключаются, чтобы применить изменение.',
    ko="코덱별 설정입니다. 'SBC 오디오 품질', AAC 추가 모듈이 있으면 'AAC 비트레이트'와 'AAC 하드웨어 가속', LDAC 추가 모듈이 있으면 'LDAC 비트레이트'(바로 적용됨), aptX 추가 모듈이 있으면 '가능하면 aptX HD 사용'을 설정할 수 있습니다. 'LDAC 비트레이트'를 제외하고는 변경 사항을 적용하기 위해 헤드폰이 잠시 다시 연결됩니다.",
    zhT='各編解碼器專屬的設定：「SBC 音訊品質」；已安裝 AAC 附加模組時有「AAC 位元率」和「AAC 硬體加速」；已安裝 LDAC 附加模組時有「LDAC 位元率」（立即生效）；已安裝 aptX 附加模組時有「可用時使用 aptX HD」。除「LDAC 位元率」外，變更其他設定時耳機會短暫重新連線。',
    zhS='各编解码器专属的设置：“SBC 音频质量”；已安装 AAC 附加模块时有“AAC 码率”和“AAC 硬件加速”；已安装 LDAC 附加模块时有“LDAC 码率”（立即生效）；已安装 aptX 附加模块时有“可用时使用 aptX HD”。除“LDAC 码率”外，更改其他设置时耳机会短暂重新连接。',
    fi='Kunkin koodekin omat asetukset: ”SBC-äänenlaatu”; AAC-lisäosan kanssa ”AAC-bittinopeus” ja ”Laitteistokiihdytetty AAC”; LDAC-lisäosan kanssa ”LDAC-bittinopeus”, joka tulee voimaan heti; aptX-lisäosan kanssa ”aptX HD, kun saatavilla”. Kuulokkeet yhdistyvät hetkeksi uudelleen muutoksen käyttöön ottamiseksi, paitsi kun muutat asetusta ”LDAC-bittinopeus”.',
    sv='Inställningarna för varje codec: ”SBC-ljudkvalitet”; med AAC-tillägget ”AAC-bithastighet” och ”Hårdvaruaccelererad AAC”; med LDAC-tillägget ”LDAC-bithastighet”, som gäller direkt; med aptX-tillägget ”aptX HD när det finns”. Utom för ”LDAC-bithastighet” ansluter hörlurarna kort på nytt för att tillämpa en ändring.',
    da='Indstillingerne for hver codec: »SBC-lydkvalitet«; med AAC-tilføjelsesmodulet »AAC-bithastighed« og »Hardwareaccelereret AAC«; med LDAC-tilføjelsesmodulet »LDAC-bithastighed«, som gælder med det samme; med aptX-tilføjelsesmodulet »aptX HD når det er muligt«. Undtagen for »LDAC-bithastighed« forbindes hovedtelefonerne kort igen for at anvende en ændring.',
    no='Innstillingene for hver kodek: «SBC-lydkvalitet»; med AAC-tillegget «AAC-bithastighet» og «Maskinvareakselerert AAC»; med LDAC-tillegget «LDAC-bithastighet», som gjelder med en gang; med aptX-tillegget «aptX HD når tilgjengelig». Unntatt «LDAC-bithastighet» kobles hodetelefonene kort til igjen for å bruke en endring.',
    pl='Ustawienia każdego kodeka: „Jakość dźwięku SBC”; z dodatkiem AAC „Przepływność AAC” i „Sprzętowo przyspieszane AAC”; z dodatkiem LDAC „Przepływność LDAC”, która działa od razu; z dodatkiem aptX „aptX HD, gdy dostępne”. Poza ustawieniem „Przepływność LDAC” słuchawki na chwilę łączą się ponownie, aby zastosować zmianę.',
    ptBR='As configurações de cada codec: "Qualidade de áudio SBC"; com o complemento do AAC, "Taxa de bits do AAC" e "AAC com aceleração por hardware"; com o complemento do LDAC, "Taxa de bits do LDAC", que vale na hora; com o complemento do aptX, "aptX HD quando disponível". Exceto "Taxa de bits do LDAC", os fones de ouvido se reconectam brevemente para aplicar uma mudança.',
    tr='Her codec\'e ait ayarlar: "SBC ses kalitesi"; AAC eklentisiyle "AAC bit hızı" ve "Donanım hızlandırmalı AAC"; LDAC eklentisiyle hemen uygulanan "LDAC bit hızı"; aptX eklentisiyle "Varsa aptX HD". "LDAC bit hızı" dışında, bir değişikliği uygulamak için kulaklıklar kısa süreliğine yeniden bağlanır.')

# Bluetooth Audio Quality becomes SBC Audio Quality (same key and values); the latency hint ends with what each codec adds.
S['ROW_QUALITY'] = {
    'ja': 'SBCオーディオ品質', 'en': 'SBC Audio Quality', 'fr': 'Qualité audio SBC', 'es': 'Calidad de audio SBC',
    'de': 'SBC-Audioqualität', 'it': 'Qualità audio SBC', 'nl': 'SBC-audiokwaliteit', 'ptPT': 'Qualidade de áudio SBC',
    'ru': 'Качество звука SBC', 'ko': 'SBC 오디오 품질', 'zhT': 'SBC 音訊品質', 'zhS': 'SBC 音频质量',
    'fi': 'SBC-äänenlaatu', 'sv': 'SBC-ljudkvalitet', 'da': 'SBC-lydkvalitet', 'no': 'SBC-lydkvalitet',
    'pl': 'Jakość dźwięku SBC', 'ptBR': 'Qualidade de áudio SBC', 'tr': 'SBC ses kalitesi'}
NOTE_LATENCY = {
    'ja': 'aptXによる遅延はほとんどなく、aptX HDとLDACは少し遅延します（LDACは約8〜19 ms）。AACは約55 msです。ゲームでは、aptXまたはSBCと「最小」の組み合わせが最も速く反応します。',
    'en': 'aptX adds almost no delay; aptX HD and LDAC add some (LDAC about 8–19 ms); AAC adds about 55 ms. For games, aptX or SBC with Lowest is quickest.',
    'fr': "aptX n'ajoute presque aucun délai ; aptX HD et LDAC en ajoutent un peu (LDAC environ 8 à 19 ms) ; AAC ajoute environ 55 ms. Pour les jeux, aptX ou SBC avec « La plus faible » réagit le plus vite.",
    'es': 'aptX casi no añade retardo; aptX HD y LDAC añaden algo (LDAC unos 8–19 ms); AAC añade unos 55 ms. En los juegos, aptX o SBC con «La más baja» responde más rápido.',
    'de': 'aptX fügt fast keine Verzögerung hinzu; aptX HD und LDAC fügen etwas hinzu (LDAC etwa 8–19 ms); AAC etwa 55 ms. Bei Spielen reagieren aptX oder SBC mit „Am niedrigsten“ am schnellsten.',
    'it': "aptX non aggiunge quasi ritardo; aptX HD e LDAC ne aggiungono un po' (LDAC circa 8–19 ms); AAC circa 55 ms. Nei giochi, aptX o SBC con «La più bassa» sono i più reattivi.",
    'nl': "aptX voegt vrijwel geen vertraging toe; aptX HD en LDAC voegen wat toe (LDAC ongeveer 8–19 ms); AAC ongeveer 55 ms. Bij games reageren aptX of SBC met 'Laagst' het snelst.",
    'ptPT': 'O aptX quase não acrescenta atraso; o aptX HD e o LDAC acrescentam algum atraso (LDAC cerca de 8–19 ms); o AAC cerca de 55 ms. Nos jogos, o aptX ou o SBC com «A mais baixa» é o mais rápido.',
    'ru': 'aptX почти не добавляет задержки; aptX HD и LDAC добавляют немного (LDAC около 8–19 ms); AAC около 55 ms. В играх быстрее всего отклик с aptX или SBC и значением «Минимальная».',
    'ko': "aptX는 지연이 거의 없고, aptX HD와 LDAC는 약간의 지연이 더해집니다(LDAC는 약 8~19 ms). AAC는 약 55 ms가 더해집니다. 게임에서는 aptX 또는 SBC와 '가장 낮음' 조합이 가장 빠릅니다.",
    'zhT': 'aptX 幾乎不增加延遲；aptX HD 與 LDAC 會增加一些（LDAC 約 8–19 ms）；AAC 約增加 55 ms。玩遊戲時，aptX 或 SBC 搭配「最低」反應最快。',
    'zhS': 'aptX 几乎不增加延迟；aptX HD 和 LDAC 会增加一些（LDAC 约 8–19 ms）；AAC 约增加 55 ms。玩游戏时，aptX 或 SBC 搭配“最低”反应最快。',
    'fi': 'aptX ei lisää viivettä juuri lainkaan; aptX HD ja LDAC lisäävät jonkin verran (LDAC noin 8–19 ms); AAC noin 55 ms. Peleissä aptX tai SBC ja Pienin reagoivat nopeimmin.',
    'sv': 'aptX lägger knappt till någon fördröjning; aptX HD och LDAC lägger till en del (LDAC cirka 8–19 ms); AAC cirka 55 ms. I spel är aptX eller SBC med Lägst snabbast.',
    'da': 'aptX lægger næsten ingen forsinkelse til; aptX HD og LDAC lægger lidt til (LDAC cirka 8–19 ms); AAC cirka 55 ms. I spil er aptX eller SBC med Lavest hurtigst.',
    'no': 'aptX legger nesten ikke til forsinkelse; aptX HD og LDAC legger til noe (LDAC cirka 8–19 ms); AAC cirka 55 ms. I spill er aptX eller SBC med Lavest raskest.',
    'pl': 'aptX prawie nie dodaje opóźnienia; aptX HD i LDAC dodają trochę (LDAC około 8–19 ms); AAC około 55 ms. W grach najszybciej reaguje aptX lub SBC z ustawieniem „Najniższe”.',
    'ptBR': 'O aptX quase não acrescenta atraso; o aptX HD e o LDAC acrescentam algum atraso (LDAC cerca de 8–19 ms); o AAC cerca de 55 ms. Em jogos, o aptX ou o SBC com "A mais baixa" é o mais rápido.',
    'tr': 'aptX neredeyse hiç gecikme eklemez; aptX HD ve LDAC biraz ekler (LDAC yaklaşık 8–19 ms); AAC yaklaşık 55 ms ekler. Oyunlarda aptX veya SBC ile En düşük en hızlı yanıt verir.'}
for _l in LANGS:
    if _l == 'enGB':
        continue
    _sep = '' if _l in ('ja', 'zhT', 'zhS') else ' '
    S['HINT_LATENCY'][_l] = S['HINT_LATENCY'][_l] + _sep + NOTE_LATENCY[_l]

# The low battery notification's text (shown by bluetoothturbo.suprx in the Home screen process, which keeps only these twenty
# strings, none of the Settings page's): the phrase and the level in the number style, {n} where the number goes.
N = {}
NOTICE_PREFIX = {
    'ja': ('ヘッドセットのバッテリー残量は', 'です'),
    'en': ('Headset battery at ', ''),
    'fr': ('Batterie du micro-casque à ', ''),
    'es': ('Batería del auricular al ', ''),
    'de': ('Headset-Akku bei ', ''),
    'it': ('Batteria della cuffia al ', ''),
    'nl': ('Batterij van de headset op ', ''),
    'ptPT': ('Bateria do auricular a ', ''),
    'ru': ('Заряд гарнитуры: ', ''),
    'ko': ('헤드셋 배터리 ', ''),
    'zhT': ('耳機電量剩餘 ', ''),
    'zhS': ('耳机电量剩余 ', ''),
    'fi': ('Kuulokkeiden akku ', ''),
    'sv': ('Headsetets batteri på ', ''),
    'da': ('Headsettets batteri på ', ''),
    'no': ('Headsettets batteri på ', ''),
    'pl': ('Bateria zestawu słuchawkowego: ', ''),
    'ptBR': ('Bateria do headset em ', ''),
    'tr': ('Kulaklık pili ', ''),
}
for _l in LANGS:
    if _l == 'enGB':
        continue
    N[_l] = NOTICE_PREFIX[_l][0] + pct_text(_l, 0).replace('0', '{n}') + NOTICE_PREFIX[_l][1]



# ---- 0.2.0: the rows on Sony's per-device Bluetooth page (Codec, Sample Rate, and the values) ----
# Bluetooth Latency and Headset Battery reuse ROW_LATENCY and ROW_HEADSET, their values the items above (Standard, Low, Lowest, the level, Not Reported).
# The values are templates, {n} and {m} where the figures go; "bitpool" is the codec's own term and stays as it is in every language.
add('ROW_DV_CODEC',
    ja='コーデック', en='Codec', fr='Codec', es='Códec', de='Codec', it='Codec', nl='Codec',
    ptPT='Codec', ru='Кодек', ko='코덱', zhT='編解碼器', zhS='编解码器', fi='Koodekki', sv='Codec',
    da='Codec', no='Kodek', pl='Kodek', ptBR='Codec', tr='Codec')
add('ROW_DV_RATE',
    ja='サンプリングレート', en='Sample Rate', fr="Fréquence d'échantillonnage", es='Frecuencia de muestreo',
    de='Abtastrate', it='Frequenza di campionamento', nl='Samplefrequentie', ptPT='Taxa de amostragem',
    ru='Частота дискретизации', ko='샘플링 속도', zhT='取樣率', zhS='采样率', fi='Näytteenottotaajuus',
    sv='Samplingsfrekvens', da='Samplingfrekvens', no='Samplingfrekvens', pl='Częstotliwość próbkowania',
    ptBR='Taxa de amostragem', tr='Örnekleme hızı')
add('DV_AAC', **{l: 'AAC, {n} kb/s' for l in LANGS})
add('DV_APTX', **{l: 'aptX, {n} kb/s' for l in LANGS})
add('DV_LDAC', **{l: 'LDAC, {n} kb/s' for l in LANGS})
add('DV_APTX_HD', **{l: 'aptX HD, {n} kb/s' for l in LANGS})
add('DV_SBC', **{l: 'SBC, bitpool {n}, {m}' for l in LANGS})
add('DV_KHZ', **{l: '{n} kHz' for l in LANGS})
# The sample rate's decimal separator, and the template of the one-line fallback title ({n} the row's name, {m} its value).
_COMMA = ('fr', 'es', 'de', 'it', 'nl', 'ptPT', 'ptBR', 'ru', 'fi', 'sv', 'da', 'no', 'pl', 'tr')
add('DV_DECIMAL', **{l: ',' if l in _COMMA else '.' for l in LANGS})
add('DV_LINE', **{l: '{n}：{m}' if l in ('ja', 'zhT', 'zhS') else '{n} : {m}' if l == 'fr' else '{n}: {m}' for l in LANGS})
# 0.3.0: the Headset Codecs row on the device page. The codec names stay as they are in every language; the row's words are the title and four
# templates ({n} the codec's name). "Add-on" is the term for the optional modules.
add('ROW_DV_HCODECS',
    ja='ヘッドセットのコーデック', en='Headset Codecs', fr='Codecs des écouteurs', es='Códecs de los auriculares', de='Headset-Codecs',
    it='Codec delle cuffie', nl='Headsetcodecs', ptPT='Codecs dos auscultadores', ru='Кодеки гарнитуры', ko='헤드셋 코덱',
    zhT='耳機編解碼器', zhS='耳机编解码器', fi='Kuulokkeiden koodekit', sv='Hörlurarnas codecs', da='Headsettets codecs',
    no='Kodeker for headset', pl='Kodeki słuchawek', ptBR='Codecs do headset', tr='Kulaklık codec\'leri')
add('DV_HC_ADDON',
    ja='{n}（アドオンあり）', en='{n} (add-on available)', fr='{n} (module complémentaire disponible)', es='{n} (complemento disponible)',
    de='{n} (Zusatzmodul verfügbar)', it='{n} (componente aggiuntivo disponibile)', nl='{n} (uitbreiding beschikbaar)', ptPT='{n} (extra disponível)',
    ru='{n} (доступно дополнение)', ko='{n} (추가 모듈 사용 가능)', zhT='{n}（可用附加模組）', zhS='{n}（可用附加模块）',
    fi='{n} (lisäosa saatavilla)', sv='{n} (tillägg tillgängligt)', da='{n} (tilføjelsesmodul tilgængeligt)', no='{n} (tillegg tilgjengelig)',
    pl='{n} (dodatek dostępny)', ptBR='{n} (complemento disponível)', tr='{n} (eklenti kullanılabilir)')
add('DV_HC_UNSUPPORTED',
    ja='{n}（非対応）', en='{n} (unsupported)', fr='{n} (non pris en charge)', es='{n} (no compatible)', de='{n} (nicht unterstützt)',
    it='{n} (non supportato)', nl='{n} (niet ondersteund)', ptPT='{n} (não suportado)', ru='{n} (не поддерживается)', ko='{n} (지원 안 함)',
    zhT='{n}（不支援）', zhS='{n}（不支持）', fi='{n} (ei tuettu)', sv='{n} (stöds inte)', da='{n} (understøttes ikke)', no='{n} (støttes ikke)',
    pl='{n} (nieobsługiwany)', ptBR='{n} (não compatível)', tr='{n} (desteklenmiyor)')
add('DV_HC_OTHER',
    ja='その他', en='Other', fr='Autre', es='Otro', de='Andere', it='Altro', nl='Overig', ptPT='Outro', ru='Другой', ko='기타',
    zhT='其他', zhS='其他', fi='Muu', sv='Annat', da='Andet', no='Annet', pl='Inne', ptBR='Outro', tr='Diğer')
add('DV_HC_UNKNOWN',
    ja='まだ不明', en='Not Known Yet', fr='Pas encore connu', es='Aún no se conoce', de='Noch nicht bekannt', it='Non ancora noto',
    nl='Nog niet bekend', ptPT='Ainda não conhecido', ru='Пока неизвестно', ko='아직 알 수 없음', zhT='尚未得知', zhS='尚未获知',
    fi='Ei vielä tiedossa', sv='Inte känt än', da='Ikke kendt endnu', no='Ikke kjent ennå', pl='Jeszcze nieznane', ptBR='Ainda não conhecido', tr='Henüz bilinmiyor')
# 0.2.0: the Signal row on the device page: the row's title and its five values (the radio link as a word; "Measuring…" for the few seconds after a connection).
add('ROW_DV_SIGNAL',
    ja='信号強度', en='Signal', fr='Signal', es='Señal', de='Signal', it='Segnale', nl='Signaal', ptPT='Sinal', ru='Сигнал', ko='신호',
    zhT='訊號', zhS='信号', fi='Signaali', sv='Signal', da='Signal', no='Signal', pl='Sygnał', ptBR='Sinal', tr='Sinyal')
add('DV_SIG_EXCELLENT',
    ja='非常に良い', en='Excellent', fr='Excellent', es='Excelente', de='Sehr gut', it='Ottimo', nl='Uitstekend', ptPT='Excelente', ru='Отличный', ko='매우 좋음',
    zhT='極佳', zhS='极佳', fi='Erinomainen', sv='Utmärkt', da='Fremragende', no='Utmerket', pl='Doskonały', ptBR='Excelente', tr='Mükemmel')
add('DV_SIG_GOOD',
    ja='良好', en='Good', fr='Bon', es='Buena', de='Gut', it='Buono', nl='Goed', ptPT='Bom', ru='Хороший', ko='좋음',
    zhT='良好', zhS='良好', fi='Hyvä', sv='Bra', da='Godt', no='Godt', pl='Dobry', ptBR='Bom', tr='İyi')
add('DV_SIG_FAIR',
    ja='普通', en='Fair', fr='Moyen', es='Regular', de='Mittel', it='Discreto', nl='Redelijk', ptPT='Razoável', ru='Средний', ko='보통',
    zhT='普通', zhS='一般', fi='Kohtalainen', sv='Måttlig', da='Middel', no='Middels', pl='Średni', ptBR='Razoável', tr='Orta')
add('DV_SIG_WEAK',
    ja='弱い', en='Weak', fr='Faible', es='Débil', de='Schwach', it='Debole', nl='Zwak', ptPT='Fraco', ru='Слабый', ko='약함',
    zhT='微弱', zhS='微弱', fi='Heikko', sv='Svag', da='Svagt', no='Svakt', pl='Słaby', ptBR='Fraco', tr='Zayıf')
add('DV_SIG_MEASURING',
    ja='測定中…', en='Measuring…', fr='Mesure en cours…', es='Midiendo…', de='Wird gemessen…', it='Misurazione…', nl='Meten…', ptPT='A medir…', ru='Измерение…', ko='측정 중…',
    zhT='測量中…', zhS='测量中…', fi='Mitataan…', sv='Mäter…', da='Måler…', no='Måler…', pl='Pomiar…', ptBR='Medindo…', tr='Ölçülüyor…')
add('DV_HC_SEP', **{l: '、' if l in ('ja', 'zhT', 'zhS') else ', ' for l in LANGS})
# The Refresh Codecs button on the device page (shown only while the headphones' list is not known): its title and its help text.
add('ROW_DV_REFRESH',
    ja='コーデックを再取得',
    en='Refresh Codecs',
    fr='Actualiser les codecs',
    es='Actualizar códecs',
    de='Codecs aktualisieren',
    it='Aggiorna codec',
    nl='Codecs vernieuwen',
    ptPT='Atualizar codecs',
    ru='Обновить кодеки',
    ko='코덱 새로 고침',
    zhT='重新整理編解碼器',
    zhS='刷新编解码器',
    fi='Päivitä koodekit',
    sv='Uppdatera codecs',
    da='Opdater codecs',
    no='Oppdater kodeker',
    pl='Odśwież kodeki',
    ptBR='Atualizar codecs',
    tr="Codec'leri yenile")
add('HINT_DV_REFRESH',
    ja='ヘッドホンが一時的に再接続され、Vitaがそのコーデックを読み取ります。',
    en='The headphones reconnect briefly so the Vita can read their codecs.',
    fr='Les écouteurs se reconnectent brièvement pour que la Vita puisse lire leurs codecs.',
    es='Los auriculares se reconectan brevemente para que la Vita pueda leer sus códecs.',
    de='Die Kopfhörer verbinden sich kurz neu, damit die Vita ihre Codecs auslesen kann.',
    it='Le cuffie si riconnettono brevemente così la Vita può leggerne i codec.',
    nl='De koptelefoons maken kort opnieuw verbinding zodat de Vita hun codecs kan uitlezen.',
    ptPT='Os auscultadores voltam a ligar-se brevemente para que a Vita possa ler os seus codecs.',
    ru='Наушники ненадолго переподключаются, чтобы Vita смогла прочитать их кодеки.',
    ko='헤드폰이 잠시 다시 연결되어 Vita가 코덱을 읽을 수 있습니다.',
    zhT='耳機會短暫重新連線，讓 Vita 讀取其編解碼器。',
    zhS='耳机会短暂重新连接，让 Vita 读取其编解码器。',
    fi='Kuulokkeet yhdistyvät hetkeksi uudelleen, jotta Vita voi lukea niiden koodekit.',
    sv='Hörlurarna ansluter kort på nytt så att Vita kan läsa av deras codecs.',
    da='Hovedtelefonerne forbindes kort igen, så Vita kan læse deres codecs.',
    no='Hodetelefonene kobles kort til igjen slik at Vita kan lese kodekene deres.',
    pl='Słuchawki na chwilę łączą się ponownie, aby Vita mogła odczytać ich kodeki.',
    ptBR='Os fones de ouvido se reconectam brevemente para que o Vita possa ler os codecs deles.',
    tr="Vita'nın codec'lerini okuyabilmesi için kulaklıklar kısa süreliğine yeniden bağlanır.")
add('ITEM_DV_DUAL',
    ja='デュアルチャンネル', en='Dual Channel', fr='Double canal', es='Canal doble', de='Zweikanal',
    it='Doppio canale', nl='Dubbel kanaal', ptPT='Canal duplo', ru='Двойной канал', ko='듀얼 채널',
    zhT='雙聲道', zhS='双声道', fi='Kaksikanavainen', sv='Dubbelkanal', da='Dobbeltkanal',
    no='Dobbeltkanal', pl='Podwójny kanał', ptBR='Canal duplo', tr='Çift kanal')
add('ITEM_DV_JOINT',
    ja='ジョイントステレオ', en='Joint Stereo', fr='Stéréo conjointe', es='Estéreo conjunto', de='Joint-Stereo',
    it='Stereo congiunto', nl='Joint stereo', ptPT='Estéreo conjunto', ru='Объединённое стерео', ko='조인트 스테레오',
    zhT='聯合立體聲', zhS='联合立体声', fi='Yhteisstereo', sv='Joint stereo', da='Joint stereo',
    no='Joint stereo', pl='Stereo łączone', ptBR='Estéreo conjunto', tr='Birleşik stereo')

# ---- 0.2.0: the codec help page, the list-style hints of Codec Settings, the fallback notification (tools/gen_i18n_codec.py) ----
FB = gen_i18n_codec.install(S, LANGS, HINT_LINES)   # the fallback notification's two texts per language, for the kernel module's table


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'


# The Settings module decodes a text into a buffer of this many 16-bit units (settings_app/bluetoothturbo_settings.c).
MAX_UNITS = 760

# English row names and list items are Title Case, as the system's own rows are; hints are not.
SMALL_WORDS = {'a', 'an', 'and', 'as', 'at', 'by', 'for', 'in', 'of', 'on', 'or', 'the', 'to', 'with', 'kb/s'}


def title_case_problems(sid, text):
    if not (sid.startswith('ROW_') or sid.startswith('ITEM_') or sid == 'TITLE'):
        return []
    words = text.split()
    bad = []
    for i, w in enumerate(words):
        if i and w in SMALL_WORDS:
            continue
        if w[0].isalpha() and not w[0].isupper() and not w.startswith('aptX'):   # the codec's own spelling
            bad.append('%s: "%s" is not Title Case in English' % (sid, text))
            break
    return bad


def problems():
    """Every string present in all 20 languages (enGB may default to en), no empty text, the
    same printf-style placeholders in every language of a string, English row names in Title
    Case, and no text longer than the Settings module's buffer."""
    bad = []
    for sid, t in S.items():
        for l in LANGS:
            if l == 'enGB':
                continue
            if not t.get(l):
                bad.append(f'{sid}: missing or empty {l}')
        for l in ('en', 'enGB'):
            if t.get(l) or l == 'en':
                bad += title_case_problems(sid, t.get(l, t['en']))
        for l in LANGS:
            if len(t.get(l, t['en'])) > MAX_UNITS:
                bad.append(f'{sid}: {l} is longer than {MAX_UNITS} characters')
        ph = {l: re.findall(r'%[-+#0-9.]*[a-zA-Z]', t.get(l, t.get('en', ''))) for l in LANGS}
        if any(ph[l] != ph['en'] for l in LANGS if l in t):
            bad.append(f'{sid}: placeholders differ between languages')
        br = {l: sorted(re.findall(r'\{[a-z]\}', t.get(l, t.get('en', '')))) for l in LANGS}
        if any(br[l] != br['en'] for l in LANGS if l in t):
            bad.append(f'{sid}: {{n}}-style placeholders differ between languages')
    # the hints in the form that is not in use must fit as well (the switch can be turned over at any time)
    other = 'semicolon' if HINT_LINES == 'newline' else 'newline'
    for l in LANGS:
        if l == 'enGB':
            continue
        for sid, text in gen_i18n_codec.hints(S, l, other).items():
            if len(text) > MAX_UNITS:
                bad.append(f'{sid}: {l} is longer than {MAX_UNITS} characters in the "{other}" form')
        for sid in ('HINT_QUALITY', 'HINT_AACRATE', 'HINT_LDACRATE', 'HINT_APTXHD', 'HINT_AACACCEL'):
            if (HINT_LINES == 'newline') != ('\n' in S[sid][l]):
                bad.append(f'{sid}: {l} is not in the "{HINT_LINES}" form')
        for k, name in enumerate(('doesn\'t support', 'couldn\'t use')):
            t = FB[l][k]
            if t.count('{c}') != 1 or '%' in t or '\n' in t:
                bad.append(f'fallback notice {l} ({name}): the text must hold {{c}} once and nothing else to substitute: {t!r}')
            if len(t.replace('{c}', 'aptX HD').encode('utf-16-le')) // 2 > 63:
                bad.append(f'fallback notice {l} ({name}): longer than 63 UTF-16 units with the longest codec name')
    for l in LANGS:
        if l != 'enGB':
            t = N.get(l, '')
            if t.count('{n}') != 1:
                bad.append(f'notice {l}: the text must hold {{n}} once: {t!r}')
            if len(t.replace('{n}', '100').encode('utf-16-le')) // 2 > 63:
                bad.append(f'notice {l}: longer than 63 UTF-16 units with a three-digit level')
    return bad


def render():
    head = ['/* Generated by tools/gen_i18n.py - edit the strings there, then regenerate. */', '']
    ids = head + ['#define BTT_I18N_IDS(X) \\'] + \
        [' \\\n'.join(f'\tX({sid})' for sid in S)] + ['']
    out = {'bluetoothturbo_i18n_ids.h': '\n'.join(ids) + '\n'}

    lines = head[:1] + ['/* Columns: ja en-US fr es de it nl pt-PT ru ko zh-T zh-S fi sv da no pl pt-BR en-GB tr. */', '']
    lines.append('static const char *const g_tbl[BTT_I18N_COUNT][BTT_LANG_COUNT] = {')
    for sid, t in S.items():
        row = [t.get(l, t['en']) if l == 'enGB' else t[l] for l in LANGS]
        lines.append(f'/* BTT_I18N_{sid} */')
        lines.append('{\n\t' + ',\n\t'.join(cstr(x) for x in row) + '\n},')
    lines.append('};')
    out['bluetoothturbo_i18n_table.h'] = '\n'.join(lines) + '\n'
    # the Home screen module's table: one text per language, in the order of the system's language numbers
    nl = head[:1] + ['/* The low battery notification, one text per system language: ja en-US fr es de it nl pt-PT ru ko zh-T zh-S fi sv da no pl pt-BR en-GB tr. */',
                     '/* {n} is where the level goes, in the language\'s number style. */', '', 'static const char *const g_notice[20] = {']
    nl += ['\t' + cstr(N['en'] if l == 'enGB' else N[l]) + ',' for l in LANGS]
    nl.append('};')
    out['user/bluetoothturbo_notice_table.h'] = '\n'.join(nl) + '\n'
    # the kernel module's table: the codec fallback notification, two texts per language ({c} is the codec's name), the same language order
    kl = head[:1] + ['/* The codec fallback notification, per system language: ja en-US fr es de it nl pt-PT ru ko zh-T zh-S fi sv da no pl pt-BR en-GB tr. */',
                     '/* Each row: the headphones do not offer the codec, the headphones could not use it. {c} is where the codec\'s name goes. */', '',
                     'static const char *const g_fallback_text[20][2] = {']
    for l in LANGS:
        a, b = FB['en'] if l == 'enGB' else FB[l]
        kl.append('\t{ ' + cstr(a) + ', ' + cstr(b) + ' },')
    kl.append('};')
    out['kernel/bluetoothturbo_fallback_table.h'] = '\n'.join(kl) + '\n'
    return out


def main(argv):
    check = '--check' in argv
    args = [a for a in argv if a != '--check']
    if len(args) not in (1, 2, 3):
        raise SystemExit(__doc__)
    outdirs = {'settings': args[0], 'user': args[1] if len(args) >= 2 else None, 'kernel': args[2] if len(args) == 3 else None}
    bad = problems()
    if bad:
        raise SystemExit('\n'.join(bad))
    files = render()
    paths = {}
    for name, text in files.items():
        if name.startswith('user/'):
            if outdirs['user'] is not None:
                paths[f"{outdirs['user']}/{name[5:]}"] = text
        elif name.startswith('kernel/'):
            if outdirs['kernel'] is not None:
                paths[f"{outdirs['kernel']}/{name[7:]}"] = text
        else:
            paths[f"{outdirs['settings']}/{name}"] = text
    if check:
        for path, text in paths.items():
            try:
                on_disk = open(path, encoding='utf-8').read()
            except OSError:
                on_disk = None
            if on_disk != text:
                raise SystemExit(f'{path} is out of date: run tools/gen_i18n.py ' + ' '.join(a for a in args))
        print(f'  ok   {len(S)} strings x {len(LANGS)} languages complete; headers up to date')
        return
    for path, text in paths.items():
        open(path, 'w', encoding='utf-8').write(text)
    print(f'{len(S)} strings x {len(LANGS)} languages -> {args[0]}' + (f', notice table -> {args[1]}' if len(args) >= 2 else '') + (f', fallback table -> {args[2]}' if len(args) == 3 else ''))


if __name__ == '__main__':
    main(sys.argv[1:])
