#!/usr/bin/env python3
"""Regenerate src/f19/f19_strings_ru.h — the EN->RU UI string table for F-19.

Sources of truth:
  - The current header is parsed for the START.EXE / END.EXE pairs, which were
    produced by offset-order anchor alignment of the EN and Boriss binaries and
    hand-reviewed (that alignment was good for START/END).
  - EGAME.EXE pairs are NOT reused: the repacked Boriss EGAME defeated anchor
    alignment and produced shifted/garbage pairs.  Instead, EGAME_MAP below is
    an explicit positional pairing of the real EN string inventory
    (/home/xor/games/f19/F19/EGAME.EXE @ 0x239ac..0x24fcf) against the Boriss
    RU inventory (/home/xor/games/f19ru/F19/EGAME.EXE @ 0x23b72..0x24f95),
    decoded from KOI-7 transliteration to UTF-8.
  - OVERRIDES: garbage/misaligned pair fixes plus user-sanctioned improvements
    of Boriss's phonetic transliterations for visible menu text.

Key layout note: the Boriss font replaces ASCII letters with KOI-7 Cyrillic
slots, so untranslated Latin renders letter-for-letter pseudo-Cyrillic — every
visible string needs a real pair to look right.
"""
import re, sys

HDR = '/home/xor/games/f15se2-ex/src/f19/f19_strings_ru.h'

# ---- explicit EGAME.EXE map (positional EN<->RU inventories, decoded) ----
EGAME_MAP = [
    ("Cargo delivered", "Груз доставлен"),
    ("Safe Landing", "Мяг. посадка"),
    ("None", "Нет"),
    ("SA-2", "СА-2"), ("SA-5", "СА-5"), ("SA-8B", "СА-8Б"), ("SA-10", "СА-10"),
    ("SA-11", "СА-11"), ("SA-12", "СА-12"), ("SA-13", "СА-13"),
    ("SA-N-4", "СА-Н-4"), ("SA-N-5", "СА-Н-5"), ("SA-N-6", "СА-Н-6"), ("SA-N-7", "СА-Н-7"),
    ("Hawk", "Хок"),              # Boriss dropped Hawk; restored
    ("Rapier", "Рапира"), ("Tiger", "Тигр"), ("Seacat", "Сикэт"),
    ("IL76", "ИЛ76"), ("IL-76", "ИЛ-76"),
    ("MIG-23", "МИГ-23"), (" Flogger", " Флоггер"),
    ("MIG-25", "МИГ-25"), (" Foxbat", " Фоксбат"),
    ("MIG-29", "МИГ-29"), (" Fulcrum", " Фулкрум"),
    ("MIG-31", "МИГ-31"), (" Foxhound", " Фоксхаунд"),
    ("Su-27", "Су-27"), (" Flanker", " Фланкер"),
    (" Mainstay", " Майнстэй"),
    ("F-4E", "Ф-4Е"), (" Phantom", " Фантом"),
    ("F-14", "Ф-14"), (" Tomcat", " Томкэт"),
    ("F-18", "Ф-18"), (" Hornet", " Хорнет"),
    ("An-72", "Ан-72"), (" Coaler", " Коалер"),
    ("Yak-38", "Як-38"), (" Forger", " Форджер"),
    ("Tu-95", "Ту-95"), (" Bear", " Беар"),
    ("Mi-24", "Ми-24"), (" Hind", " Хинд"),
    (" Tiger", " Тигр"), (" Boeing", " Боинг"),
    ("AA-2", "АА-2"), ("AA-8", "АА-8"), ("AA-6", "АА-6"), ("AA-7", "АА-7"),
    ("AA-9", "АА-9"), ("AA-10", "АА-10"),
    ("AIM120", "АИМ120"), ("AIM-9", "АИМ-9"), ("HARM", "ХАРМ"),
    ("Penguin", "Пингвин"), ("Harpoon", "Гарпун"),
    ("AGM-65", "АГМ-65"), ("LGBOMB", "ЛГБОМБ"), ("RTBOMB", "РТБОМБ"),
    ("FFBOMB", "ФФБОМБ"), ("AIM-7W", "АИМ-7В"), ("AIM-9W", "АИМ-9В"),
    ("SA-14", "СА-14"), ("Equip.", "Груз."),
    ("AIM-9M", "АИМ-9М"), ("Sidewinder", "Сайдвиндер"), ("AIM-120", "АИМ-120"),
    ("AMRAAM", "АМРААМ"), ("AGM-88A", "АГМ-88А"), ("P3 ASM", "П3 АСМ"),
    ("AGM-86A", "АГМ-86А"), ("AGM-65D", "АГМ-65Д"), ("Maverick", "Мейверик"),
    ("GBU-12", "ГБУ-12"), ("Paveway", "Пейвэй"),
    ("Mk 20", "Мк 20"), ("Rockeye", "Рокай"), ("Dndl", "Дндл"),
    ("Durandal", "Дурандал"), ("Mk 82-0", "Мк 82-0"), ("Slick", "Слик"),
    ("Mk 82-1", "Мк 82-1"), ("Snakeye", "Снейкай"), ("Rockeye II", "Рокай 2"),
    ("Mk 122", "Мк 122"), ("Fireeye", "Файрай"), ("CBU-72", "ЦБУ-72"),
    ("Fuel-Air", "ФАС"),           # Boriss dropped Fuel-Air; restored
    ("Mk 35", "Мк 35"), ("IN Cluster", "В кассете"), ("ISC B-1", "ИСЦ Б-1"),
    ("Minelets", "Майнлетс"), ("135 mm", "135 мм"), ("Camera", "КАМЕРА"),
    ("1900lbs", "1900лбс"), ("Extra Fuel", "Запас. бак"), ("20 mm", "20 мм"),
    ("Guns", "Пуш"), ("Special", "Спец."), ("Equip", "Груз"),
    # HUD/event message fragments (composed at runtime via strcat)
    ("Stores exhausted", "Запас исчерпан"),
    ("Flare", "Фларе"), ("Chaff", "Диполи"), ("Decoy", "Экран"),
    (" released", " выпущен"), (" at ", " в "), (" detected", " обнаруж."),
    (" - Radar ID", " - Радар ИД"), (" firing ", " пущен "),
    (" fired by ", " выпущен "), (" fired", " пущен"),
    (" landing", " посадка"), (" on final", " посадка"),
    ("Visual ID - ", "Визуальн.ИД "), (" taking off", " взлет"),
    (" misses ", " промах "), (" destroyed by ", " уничтожен "),
    (" destroyed by gunfire", " сбит"),
    ("Blast damage", "Пораж. оскол"),
    ("Equipment Delivered", "  ГРУЗ ДОСТАВЛЕН   "),
    ("Bad Drop", "Плохой сброс, удар о грунт"),
    (" ground impact", "Пораж. "),
    ("Hit by gunfire", "Сбит огнем"),
    ("Hit by ", "Сбит "), ("Ineffective", "Неэффектив."),
    (" hit by ", " пораж. "),
    ("Second. target", "Вторичн. цель"), ("Primary target", "Основная цель"),
    ("Bay closed", "ЛЮК ЗАКРЫТ"), ("Fuel transfer", " ПЕРЕКАЧАНО  "),
    # instrument panel / MFD labels
    ("stall warning", "угроза сваливания"),
    ("GUN ", "ПУШ "), ("AIR-GROUND", "ВОЗ-ЗЕМ   "), ("AIR-AIR", "ВОЗ-ВОЗ"),
    ("FLAPS", "ЗАКР "), ("BRAKE", "ТОРМ "), ("ACCEL", "УСКОР"),
    ("TRAINING", "ТРЕНАВ  "), ("Waypoint ", "УКАЗАТЕЛЬ"),
    (" reached", " найден"), ("TrackCam ", "Сканнер "),
    ("Ahead", "Вперед"), ("Rear", "Назад"), ("Right", "Прав."), ("Left", "Лев."),
    ("Select Waypoint", "Выбор Указат."), ("Change Waypoint", "Смена Указат."),
    ("TIME", "Врем"), (" Waypoint ", " Указатель"), (" Dist-", " Дист-"),
    (" MIN.  ETA", " МИН.  ВРМ"), ("Fuel ", "ГОР  "), (" lbs.", " фун."),
    ("Weapons", "ВООРУЖ "), ("Damage", "ПОВРЕЖ"),
    ("CHAFF  ", "ФОЛЬГИ "), ("FLARES ", "РАКЕТ "),
    ("Mission", "ЗАДАНИЕ"), ("Mission Orders", "БОЕВАЯ ЗАДАЧА "),
    ("Take off from", "ВЗЛЕТ С "), ("SECONDARY", "ВТОРИЧНАЯ"),
    ("PRIMARY", "ОСНОВН."), (" Mission", " ЗАДАЧА"), ("Completed", "ВЫПОЛНЕНА"),
    ("Photograph enemy", "Сфотографировать"),
    ("Land supplies at", "Доставить груз в"),
    ("Drop supplies over", "Сбросить груз над"),
    ("Destroy enemy", "Уничтожить"),
    ("Destroy AN72 flying to", "Сбить АН72 летящий на"),
    ("Intercept AN72 leaving", "Сбить АН72 летящий из"),
    ("Plane returning to", "Самолет уходящ. на"),
    ("Destroy plane orbiting", "Сбить самолет над "),
    ("Before ", "До "), ("Time expired", "Время вышло"),
    ("Return to", "ПОСАДКА В"), ("Sightings  V ", "Положение  В "),
    ("  R ", "  Р "), ("Data Base", "Данные"),
    ("Missile range- ", "Дальн. ракет - "), (" missiles", " ракет"),
    ("Max Speed- ", "Макс скор. "), (" mph", " М/Ч"),
    (" doppler", " допплер"), (" pulse", " импул"), (" radar", " радар"),
    ("Max. range ", "Макс дальн."), ("Eff. range ", "Эфф. дальн."),
    ("Frame ", "Кадр "), ("Target photographed.", "Цель сфотографиров."),
    ("Primary Target", "Основная цель"), ("Secondary Target", "Вторичная цель"),
    ("Civilian or Friendly", "Гражд. или дружест."),
    ("Speed ", "СКОР  "), (" KMH", " КМ/Ч"),
    ("Radio Beacon", "Радиомаяк"),
    ("Radio beacon bearing ", "Радиомаяк по курсу "),
    ("Drop supplies now", "Сбрасывайте груз"), ("Missile Lock", "Захват цели"),
    ("Range ", "ДИСТ  "), ("BRG ", "КУР "),
    ("Detail Level ", "ДЕТАЛИЗАЦИЯ  "), ("Kybd Sensitivity", "ЧУВСТВИТЕЛЬНОСТЬ"),
    ("Sounds ", "ЗВУК   "),
]

# ---- curated fixes/improvements over the aligned START/END pairs ----
OVERRIDES = [
    # Boriss phonetically transliterated the graphics-mode menu; render it in
    # real Russian (user-sanctioned improvement).
    ("                           Select graphics mode:                              ",
     "                           Выберите графический режим:                          "),
    ("                        1) VGA/MCGA (256 color)                               ",
     "                        1) VGA/MCGA (256 цветов)                               "),
    ("                        2) EGA (16 color)                                     ",
     "                        2) EGA (16 цветов)                                     "),
    ("                        3) Tandy 1000 (16 color)                              ",
     "                        3) Tandy 1000 (16 цветов)                              "),
    ("                        4) CGA (4 color)                                      ",
     "                        4) CGA (4 цвета)                                       "),
    ("                        5) Hercules Monochrome (2 color)                      ",
     "                        5) Hercules Monochrome (2 цвета)                       "),
    ("                            All Rights Reserved.                            ",
     "                            Все права защищены.                               "),
    ("                   F-19 Stealth Fighter - Version 435.04                    ",
     "                   Ф-19 Стэлс Истребитель - Версия 435.04                     "),
    ("    Program & Audio-visual Copyright (C) 1988 by MicroProse Software,Inc.,  ",
     "    Программно-мультимедийные права (C) 1988 MicroProse Software, Inc.,       "),
    ("         Move joystick to upper left corner, then press fire button 1 ",
     "         Отведите джойстик в левый верхний угол, нажмите кнопку 1 "),
    ("       Now move joystick to lower right corner, then press fire button 2 ",
     "       Теперь отведите джойстик в правый нижний угол, нажмите кнопку 2 "),
    ("*** NOTE ***", "*** ВНИМАНИЕ ***"),
    ("Open Error on *.3DG, assuming new file !",
     "Ошибка открытия *.3DG, создаю новый файл !"),
    ("Open Error on *.3DT, assuming new file !",
     "Ошибка открытия *.3DT, создаю новый файл !"),
    # --- END.EXE debrief event log (block realigned: RU inventory order) ---
    ("Press Selector to exit Debriefing", "Нажмите (ВВОД) для продолжения"),
    ("OVERALL", "ОБЩИЙ"),
    ("MISSION RATING", "РЕЙТИНГ ПОЛЕТА"),
    ("MISSION EVENT", "ОЧЕРЕДНОЙ ШАГ"),
    ("TIME: ", "ВРЕМЯ "),
    (" destroyed", " уничтожен"),
    (" shot down", " сбит"),
    ("Cargo delivered", "Груз доставлен"),
    (" photographed", " сфотограф."),
    ("Hit by ", "Пораж. "),
    (" missile", " ракетой"),
    (" Track", " Обн."),
    (" Radar Track", " Радар. обн."),
    (" Visual ID", " Виз. Обн."),
    ("Takeoff point:", "Точка взлета:"),
    ("Mission end:", "Конец полета:"),
    ("Crashed", "Разбил."),
    ("Good Bailout", "Спасся"),
    ("Captured", "Захвачен"),
    ("Bailed & Died", "Катап.и погиб"),
    ("Good Landing", "Мяг. посадка"),
    ("PRIMARY OBJECTIVE", "ОСНОВНАЯ ЦЕЛЬ"),
    ("SECNDRY OBJECTIVE", "ВТОРИЧНАЯ ЦЕЛЬ"),
    ("CUMULATIVE", "ОБЩИЙ"),
    ("Press Selector for next mission event", "(ВВОД) для следующего шага"),
    ("In-Flight", "В ПОЛЕТЕ"),
    ("Gee, it can get downright nasty out there", "Да, получилось не очень здорово."),
    # --- END.EXE debrief narrative (RU fragments resynced after extra entry) ---
    ("mission incomplete.  ", "задача не выполнена. "),
    ("After an embarrassing international incident, ", "После международного инцидента, "),
    (" returned to his squadron.  ", " вернулся в свой полк.  "),
    ("Destroying the ", "Выполнение "),
    ("primary and secondary targets ", "основной и вторичной задач "),
    ("primary objective ", "основной задачи "),
    ("secondary target ", "вторичной задачи "),
    ("helped calm the squadron commander.  ", "помогло успокоить командира полка.  "),
    ("The squadron commander was not pleased that the ", "Командир полка недоволен тем, что "),
    ("primary target remains intact.  ", "основная задача не выполнена.  "),
    (" survived ejection after ", " катапультировался после "),
    ("successfully achieving his ", "успешного выполнения "),
    ("primary and secondary objectives.  ", "основной и вторичной задач.  "),
    ("primary objective.  ", "основной задачи.  "),
    ("secondary objective.  ", "вторичной задачи.  "),
    (" survived ejection but ", " катапультировался, но "),
    ("failed to accomplish the mission.  ", "не выполнил свою задачу.  "),
    (" landed safely and ", " приземлился и "),
    ("accomplished his ", "выполнил "),
    ("primary and secondary mission!  ", "основную и вторичную задачи!  "),
    ("primary mission!  ", "основную задачу!  "),
    ("secondary mission!  ", "вторичную задачу!  "),
    (" landed safely but ", " приземлился, но "),
    ("failed to accomplish his assigned objective.  ", "не выполнил свою задачу.  "),
    (" was killed in action ", " погиб в бою "),
    ("after accomplishing his mission.  ", "после выполнения своей задачи.  "),
    (" died in the line of duty, ", " погиб во время полета, "),
    ("successfully destroyed.  ", "успешно выполнены.  "),
    (" and the ", "а "),
    # --- END.EXE medal award / promotion blocks (realigned) ---
    ("AIRMAN's MEDAL", "МЕДАЛЬЮ ВВС"),
    (" for exceptional mission performance", " за исключительное выполнение задачи"),
    ("DISTINGUISHED FLYING CROSS", "КРЕСТОМ ЗА БОЕВОЕ ОТЛИЧИЕ"),
    (" for valor in the line of duty", " за доблестную службу"),
    ("SILVER STAR", "СЕРЕБРЯНОЙ ЗВЕЗДОЙ"),
    (" for heroism while flying a combat mission", " за героизм, проявленный в полете"),
    ("AIR FORCE CROSS", "КРЕСТОМ ВВС"),
    (" for valor and heroism on a combat mission", " за доблесть и героизм проявленные в полете"),
    ("CONGRESSIONAL MEDAL OF HONOR", "МЕДАЛЬЮ КОНГРЕССА США"),
    (", America's highest military honor,  for heroism above and beyond the call of duty",
     ", высшей военной наградой США - за героизм проявленный при выполнении задания"),
    ("PURPLE HEART", "ПУРПУРНЫМ СЕРДЦЕМ"),
    (" for being wounded in combat operations", " за ранение в бою"),
    ("Airman's Medal", "Медаль ВВС"),
    ("Distinguished Flying Cross", "Крест за Боевое Отличие"),
    ("Silver Star", "Сер. Звезда"),
    ("Air Force Cross", "Крест ВВС"),
    ("Congressional Medal of Honor", "Медаль Конгресса США"),
    ("OverSeas Short Tour Ribbon", "Лентой за ближний полет"),
    ("OverSeas Long Tour Ribbon", "Лентой за дальний полет"),
    ("Longevity Service Ribbon", "Лентой за долгую службу"),
    ("Longevity Service Ribbon with Gold Cluster", "Лентой за долгую службу с золотой кистью"),
    ("FIRST LIEUTENANT", "СТАРШ. ЛЕЙТЕНАНТ"),
    ("CAPTAIN", "КАПИТАН"),
    ("MAJOR", "МАЙОР"),
    ("LIEUTENANT COLONEL", "ПОДПОЛКОВНИК"),
    ("COLONEL", "ПОЛКОВНИК"),
    ("BRIGADIER GENERAL", "ГЕНЕРАЛ-МАЙОР"),
    ("First Lieutenant", "Старш. Лейтенант"),
    ("Major", "Майор"),
    ("Lieutenant Colonel", "Подполковник"),
    ("full Colonel", "Полковник"),
    ("Brigadier General", "Генерал - Майор"),
    (" was", " был"),
    (" was ", " был "),
    (", posthumously,", ", посмертно,"),
    (" awarded ", " награжден "),
    (" is awarded the ", " награжден "),
    (" is also awarded the ", " также награжден "),
    ("awarded the ", "награжден "),
    # composed award text: rbuf + " is " + ["also "] + "awarded the \x8e..."
    (" is ", ""),
    ("also ", " также "),
    ("awarded the \x8e", " награжден \x8e"),
    (" are ", " "),
    ("The ", ""),
    (" and the ", " и "),
    ("PURPLE HEART ", "ПУРП. СЕРДЦЕ "),
    ("for being wounded.  ", "за ранение.  "),
    ("Combat Readiness Ribbon", "Лентой Боевой Готовности"),
    (" for ", " за "),
    ("successfully completing this training mission.", "успешное выполнение тренировочной задачи."),
    (" for flying ", " за "),
    (" missions.", " вылетов."),
    ("If this had been a real mission you would have received the ",
     "Если бы это был реальный вылет Вы получили бы "),
    ("Because your capture exposed our stealth technology, the squadron ",
     "Из-за того, что Вы открыли врагу технологию самолетов-\"невидимок\" "),
    ("commander will not recommend you for the ", "командование не представило Вас к "),
    (" remained at his present rank.  ", " остался в том же звании.  "),
    ("For demonstrated skill and achievment, ", "За мастерство и высокий боевой уровень,"),
    ("posthumously ", "посмертно "),
    ("promoted ", "присвоено "),
    ("to the rank of ", " звание "),
    ("The capture and public trial of ", "За добровольную сдачу в плен, "),
    (" will delay the promotion to ", " задержано присвоение звания "),
    ("Congratulations on the successful completion of your 99th mission and retirement from the Air Force.",
     "Поздравляем с успешным выполнением 99-ти боевых вылетов и увольнением из Военно-Воздушных Сил."),
    # --- START.EXE mission-briefing composition fragments ---
    ("to \x89", "\x89"),                    # verb prefix; RU keeps only the color escape
    ("photograph the ", "сфотографировать "),
    (" at ", " в "),
    (", ONC ", ", КВД "),
    (" ONC ", " КВД "),
    ("before ", "до "),
    (" hours", " часов"),
    (" hours.", " часов."),
    (", while the cargo is still being unloaded.", ", пока груз не разгружен."),
    ("destroy the ", "уничтожить "),
    (" the ", " "),
    ("reach the beacon", "обнаружить радиомаяк"),
    (" at ONC ", " в КВД "),
    (" and ", " и "),
    ("drop the supplies before ", "сбросить груз до "),
    (".  To minimize the chance of enemy detection, ",
     ". Для малой вероятности обнаружения врагами, "),
    ("the beacon will be on ONLY during this period.",
     "маяк будет включен только в это время."),
    ("reach the secret airstrip", "сесть на секретный аэродром"),
    (".  The airstrip will be lighted ONLY during this period.",
     ". Полоса будет освещена только в это время."),
    ("AN-72 Coaler transport", "транспорт АН-72"),
    ("intercept and destroy", "обнаружить и уничтожить"),
    (" departing from ", ", отправившийся с "),
    (" leaving from ", ", отправившийся с "),
    (" airbase, ONC ", " авиабазы,КВД "),
    (", at ", ", в "),
    (" hours.  It is expected to arrive at ", " часов. Он прибывает на "),
    (" hours.  The ultimate destination is unknown.", " часов. Пункт назначения неизвестен."),
    ("intercept and destroy the Tu-95", "обнаружить и уничтожить Ту-95"),
    (", last seen in grid ONC ", ", замеченный в КВД "),
    (" hours.  Based on past experience it is probably heading for",
     " часов. По всей вероятности он направляется на "),
    (" airbase.  If so, its estimated arrival time is ",
     " авиабазу. Если так, то время прибытия "),
    (" fighter patrol", " истребитель"),
    (" at, ONC ", " в, КВД "),
    ("Select an option, press Selector", "Выберите и нажмите (ВВОД)"),
    ("SIDEWINDER", "САЙДВИНДЕР"),
    ("MiG-25 Foxbat", "МиГ-25 Фоксбат"),
    ("MiG-29 Fulcrum", "МиГ-29 Фулкрум"),
    ("the ", ""),                           # article dropped in Russian
    # section headers — Boriss keeps the asterisk decorations
    ("Mission Targets", "Боевая задача"),   # drawn form; 'td' prefix is an icon code
    ("AIRBASE DATA", "* АВИАБАЗЫ *"),
    ("RADAR DATA", "* РАДАРЫ *"),
    ("CLASSIFIED                             CLASSIFIED",
     "**********                             **********"),
    ("CLASSIFIED                        CLASSIFIED",
     "**********                        **********"),
]

# EN keys whose current pairs came from the broken EGAME alignment: replace
# wholesale with EGAME_MAP.
EGAME_EN_KEYS = set(en for en, _ in EGAME_MAP)

# Garbage/misaligned keys that survive the noise filters — hard blacklist.
DROP_KEYS = {
    'zzu3', 'ito ', ' uSF', 'uBvlw', 'PUVW', 'PSQRVWU+', 'PSQRVWU',
    'hO6kB', 'y;C_FILE_INFO', ';C_FILE_INFO', 'aircraft.  ', 'attack ',
    'B Gen ', 'l-76 Mainstay', 'ntruder', 'Capt ', 'full ',
    # bare fragments that break real composed text when misaligned —
    # correct versions come from OVERRIDES/EGAME_MAP instead
    ' and',   # breaks 'commander' (' and' matches inside ' commander'!)
    ' was ',  # misaligned ('(ВВОД)...' garbage); real fragment is ' was'/'was '
    ' are ',  # misaligned 'были'; 'are' is dropped in Russian anyway
    'the ',   # misaligned END fragment ('и лишились'); -> '' in OVERRIDES
    ' is ',   # real award-path fragment -> '' in OVERRIDES
    'also ',  # real award-path fragment -> ' также ' in OVERRIDES
    'The ',   # real debrief fragment -> '' in OVERRIDES
}

# RU noise characters that mark a garbage alignment artifact (decoded Boriss
# text never contains these).
NOISE_RU = re.compile(r'[=<>@#$%^&*~`{}\\|]')
# EN-side noise keys (control-byte fragments from failed alignment).
NOISE_EN = re.compile(r'[=<>@#$%^&*~`{}\\|]|[0-9][a-z][a-zA-Z][&@]')

def esc_en(s):
    # EN keys may carry DOS color-escape bytes (e.g. \x89); emit C \xNN for
    # anything outside printable ASCII so the literal stays byte-exact.
    # A \xNN escape greedily eats following hex digits, so split the literal
    # ("..." "") when the next char is a hex digit.
    out = []
    prev_hex_esc = False
    for ch in s:
        o = ord(ch)
        if prev_hex_esc and ch in '0123456789abcdefABCDEF':
            out.append('" "')
        prev_hex_esc = False
        if ch == '"':
            out.append('\\"')
        elif ch == '\\':
            out.append('\\\\')
        elif 0x20 <= o < 0x7f:
            out.append(ch)
        else:
            out.append('\\x%02x' % (o & 0xff))
            prev_hex_esc = True
    return ''.join(out)

def esc_ru(s):
    # RU text is UTF-8, but DOS color-escape bytes (U+0080..U+009F) must emit
    # as raw single bytes (\xNN), not UTF-8 C2 xx sequences.
    out = []
    prev_hex_esc = False
    for ch in s:
        o = ord(ch)
        if prev_hex_esc and ch in '0123456789abcdefABCDEF':
            out.append('" "')
        prev_hex_esc = False
        if ch == '"':
            out.append('\\"')
        elif ch == '\\':
            out.append('\\\\')
        elif 0x80 <= o <= 0x9f or o < 0x20:
            out.append('\\x%02x' % o)
            prev_hex_esc = True
        else:
            out.append(ch)
    return ''.join(out)

def main():
    text = open(HDR, encoding='utf-8').read()
    pairs = re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\}', text)
    def unesc(s):
        return (s.replace('\\"', '"').replace('\\\\', '\\'))
    def unesc_en(s):
        s = unesc(s)
        return re.sub(r'\\x([0-9a-fA-F]{2})', lambda m: chr(int(m.group(1), 16)), s)
    pairs = [(unesc_en(a), unesc(b)) for a, b in pairs]
    seen, merged, dropped = set(), {}, []
    for en, ru in pairs:
        if en in seen:
            continue
        seen.add(en)
        if en in EGAME_EN_KEYS:
            dropped.append(('egame-replaced', en, ru)); continue
        if en in DROP_KEYS:
            dropped.append(('blacklist', en, ru)); continue
        if NOISE_RU.search(ru) or NOISE_EN.search(en):
            dropped.append(('noise', en, ru)); continue
        merged[en] = ru
    # curated fixes win over kept pairs; then the rebuilt EGAME section
    for en, ru in OVERRIDES:
        merged[en] = ru
    for en, ru in EGAME_MAP:
        merged[en] = ru
    # longest-first by C-string byte length (EN keys are latin-1: \xNN = 1 byte)
    out = sorted(merged.items(), key=lambda p: -len(p[0].encode('latin-1')))
    with open(HDR, 'w', encoding='utf-8') as f:
        f.write("""/* F-19 English -> Russian UI string table.
 * RU text derives from the authentic Boriss Software (1990) translation,
 * decoded from the KOI-7/Volapuk transliteration embedded in the Russian
 * release EXEs.  EGAME.EXE pairs are an explicit positional map of the two
 * string inventories (the repacked Boriss binary defeated offset alignment);
 * START/END pairs come from anchor alignment.  Entries marked by manual fixes
 * correct binary-alignment gaps; a few Boriss phonetic transliterations are
 * rendered in proper Russian (user-sanctioned improvement).  Sorted
 * longest-key-first.  Regenerate with tools/f19_rustrings.py. */
#ifndef F19_STRINGS_RU_H
#define F19_STRINGS_RU_H

typedef struct { const char *en; const char *ru; } F19StringPair;
static const F19StringPair f19_ruStrings[] = {
""")
        for en, ru in out:
            f.write('    {"%s", "%s"},\n' % (esc_en(en), esc_ru(ru)))
        f.write("""};

#define f19_ruStringCount ((int)(sizeof(f19_ruStrings) / sizeof(f19_ruStrings[0])))

#endif /* F19_STRINGS_RU_H */
""")
    print('kept %d, dropped %d, total %d' % (len(out), len(dropped), len(out)))
    for kind, en, ru in dropped:
        print('  drop[%s] %r -> %r' % (kind, en[:40], ru[:40]))

if __name__ == '__main__':
    main()
