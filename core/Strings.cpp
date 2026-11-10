// String section arranger. Three layers:
//   1. skeleton: the four-voice search of the choir engine with string ranges (Violin I, Violin II,
//      Viola, Cello);
//   2. texture: every part turns its skeleton line into string writing by the rule its composer
//      profile gives for the part's complexity level; the basses are derived from the cellos;
//   3. advice: what notes cannot say (bowing, patches, doublings) goes to the report as text.
// Everything is deterministic: the same input and settings always give the same parts.
#include "Strings.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cmath>
#include <sstream>

namespace strings {

const char* PART_NAMES[NPARTS] = {"Violin I", "Violin II", "Viola", "Cello", "Contrabass"};
const char* PART_SHORT[NPARTS] = {"V1", "V2", "Va", "Vc", "Cb"};

namespace {

constexpr int Q = 480, E = 240, X = 120;

// sounding limits of the instruments: lowest note (an open string), comfortable top, hard limit
const int LO[NPARTS] = {55, 55, 48, 36, 28};
const int TOP[NPARTS] = {88, 88, 81, 69, 55};
const int LIMIT[NPARTS] = {100, 100, 88, 81, 67};

enum class Tex { Sustain, Detache, Repeat8, Repeat16, Tremolo, Alberti8, Alberti16, Arp8, Arp16, PizzArp, Offbeat,
                 Syncop, Dotted, Osc8, Osc16, PizzBeat, Ostinato, Augurs, Stabs, OctaveUp, Unison, Shadow, Shadow2,
                 Tintin, Tintin2, Walk, Drone, Double8vb, Roots, RootsPizz };

struct TexInfo {
    Tex tex;
    const char* code;     // used in the profile table
    const char* label;    // shown in the report
};

const TexInfo TEX[] = {
    {Tex::Sustain, "sus", "выдержанные ноты"},
    {Tex::Detache, "det", "деташе по долям"},
    {Tex::Repeat8, "rep8", "репетиции восьмыми"},
    {Tex::Repeat16, "rep16", "репетиции шестнадцатыми"},
    {Tex::Tremolo, "trem", "тремоло"},
    {Tex::Alberti8, "alb8", "альбертиевы фигуры восьмыми"},
    {Tex::Alberti16, "alb16", "альбертиевы фигуры шестнадцатыми"},
    {Tex::Arp8, "arp8", "ломаные аккорды восьмыми"},
    {Tex::Arp16, "arp16", "арпеджио шестнадцатыми"},
    {Tex::PizzArp, "parp", "арпеджио пиццикато"},
    {Tex::Offbeat, "off", "аккорды на слабую долю"},
    {Tex::Syncop, "syn", "синкопы через долю"},
    {Tex::Dotted, "dot", "пунктирный ритм"},
    {Tex::Osc8, "osc8", "покачивание восьмыми"},
    {Tex::Osc16, "osc16", "покачивание шестнадцатыми"},
    {Tex::PizzBeat, "pizz", "пиццикато по долям"},
    {Tex::Ostinato, "ost", "остинато 3+3+2"},
    {Tex::Augurs, "aug", "остинато со сбитыми акцентами"},
    {Tex::Stabs, "stab", "акцентированные удары"},
    {Tex::OctaveUp, "oct", "в октаву (divisi)"},
    {Tex::Unison, "uni", "мелодия в октаву с первыми скрипками"},
    {Tex::Shadow, "sh", "вторит мелодии аккордовым тоном ниже"},
    {Tex::Shadow2, "sh2", "вторит мелодии двумя тонами ниже"},
    {Tex::Tintin, "tin", "тинтиннабули: тон тонического трезвучия под мелодией"},
    {Tex::Tintin2, "tin2", "тинтиннабули: второй тон трезвучия под мелодией"},
    {Tex::Walk, "walk", "шагающий бас восьмыми"},
    {Tex::Drone, "drone", "педаль на тонике"},
    {Tex::Double8vb, "dbl", "октавой ниже виолончелей"},
    {Tex::Roots, "roots", "выдержанный бас"},
    {Tex::RootsPizz, "rpizz", "пиццикато на сильные доли"},
};

const char* texLabel(Tex t) {
    for (auto& i : TEX)
        if (i.tex == t) return i.label;
    return "";
}

struct Step {
    int from;
    Tex tex;
};

// harmonic vocabulary of the skeleton
enum Vocab {
    FUNC,     // functional harmony, strict voice leading
    COLOR,    // added tones of the input stay, voices move as little as they can
    LUSH,     // sevenths and ninths are added, passing diminished chords higher up
    MODAL     // plain triads only
};

struct Row {
    const char* id;
    const char* label;
    const char* group;
    Vocab vocab;
    Section section;
    const char* ladder[NPARTS];   // "0:sus 3:rep8 7:rep16" for each part
    int ornCap;                   // sustained lines are never ornamented above this level
    bool legato;                  // sustained notes are slurred
    bool accents;                 // sforzando on chord changes from level 5
    bool bite;                    // a second is added to accented ostinato chords from level 7
    int mel;                      // register of the melody: -1 low, 0, +1 high
    int spacing;                  // 0 normal, 1 close, 2 wide
    int vel;                      // overall loudness offset
};

const Section SQ = Section::Quartet, SB = Section::Baroque, SC = Section::Classical, SR = Section::Romantic,
              SS = Section::Symphonic;

// Every row is one composer: what the skeleton may do, and for each part which texture it takes from which
// complexity level. The order is the order of the style menu and is saved in projects: add rows at the end
// of the table only (the menu groups them by the `group` text, wherever they stand).
const Row ROWS[] = {
    // ------------------------------------------------------------------ baroque
    {"corelli", "Корелли", "Барокко", FUNC, SB,
     {"0:sus", "0:sus", "0:sus", "0:sus 2:det 4:walk", "0:dbl"}, 8, false, false, false, 0, 0, 0},
    {"vivaldi", "Вивальди", "Барокко", FUNC, SB,
     {"0:sus 7:rep16", "0:sus 3:rep8 6:osc16", "0:sus 3:rep8 9:rep16", "0:sus 2:det 5:rep8", "0:dbl"}, 4, false, false, false, 0, 0, 0},
    {"purcell", "Пёрселл", "Барокко", FUNC, SB,
     {"0:sus", "0:sus 6:dot", "0:sus 4:dot", "0:sus 2:det 7:dot", "0:dbl"}, 7, false, false, false, 0, 0, 0},
    {"telemann", "Телеман", "Барокко", FUNC, SB,
     {"0:sus", "0:sus 3:sh", "0:sus 4:rep8", "0:sus 2:det 7:walk", "0:dbl"}, 5, false, false, false, 0, 0, 0},
    {"bach", "Бах", "Барокко", FUNC, SB,
     {"0:sus", "0:sus", "0:sus", "0:sus", "0:dbl"}, 10, false, false, false, 0, 0, 0},
    {"handel", "Гендель", "Барокко", FUNC, SB,
     {"0:sus 6:dot", "0:sus 3:dot", "0:sus 3:dot", "0:sus 2:det 5:walk", "0:dbl"}, 5, false, true, false, 0, 0, 6},
    {"rameau", "Рамо", "Барокко", FUNC, SB,
     {"0:sus", "0:sus 3:off", "0:sus 3:off", "0:sus 2:det 6:dot", "0:dbl"}, 6, false, false, false, 0, 0, 0},
    // ------------------------------------------------------------------ classical
    {"haydn", "Гайдн", "Классицизм", FUNC, SC,
     {"0:sus", "0:sus 3:rep8", "0:sus 4:rep8", "0:sus 2:det 8:rep8", "0:dbl"}, 6, false, false, false, 0, 0, 0},
    {"mozart", "Моцарт", "Классицизм", FUNC, SC,
     {"0:sus", "0:sus 3:alb8 7:alb16", "0:sus 5:rep8", "0:sus 2:det", "0:dbl"}, 7, true, false, false, 0, 0, 0},
    {"boccherini", "Боккерини", "Классицизм", FUNC, SC,
     {"0:sus", "0:sus 3:syn", "0:sus 3:rep8", "0:sus", "0:dbl 3:rpizz"}, 7, true, false, false, 0, 0, -4},
    {"beethoven", "Бетховен", "Классицизм", FUNC, SC,
     {"0:sus 8:oct", "0:sus 3:rep8 6:rep16", "0:sus 3:rep8 7:rep16", "0:sus 2:det 5:rep8", "0:dbl"}, 5, false, true, false, 0, 0, 6},
    {"beethoven-late", "Бетховен, поздние квартеты", "Классицизм", FUNC, SQ,
     {"0:sus", "0:sus 6:syn", "0:sus", "0:sus", "0:dbl"}, 9, true, false, false, 0, 2, -4},
    // ------------------------------------------------------------------ romantic
    {"schubert", "Шуберт", "Романтизм", FUNC, SC,
     {"0:sus", "0:sus 3:rep8 8:trem", "0:sus 3:rep8", "0:sus 4:pizz", "0:dbl 3:rpizz"}, 6, true, false, false, 0, 0, -4},
    {"mendelssohn", "Мендельсон", "Романтизм", FUNC, SC,
     {"0:sus", "0:sus 3:rep8 5:rep16", "0:sus 4:rep16", "0:sus 3:pizz", "0:dbl 3:rpizz"}, 7, true, false, false, 1, 0, -8},
    {"schumann", "Шуман", "Романтизм", FUNC, SR,
     {"0:sus", "0:sus 3:syn", "0:sus 3:syn 7:rep8", "0:sus 5:arp8", "0:dbl 4:rpizz"}, 7, true, false, false, 0, 0, 0},
    {"brahms", "Брамс", "Романтизм", FUNC, SR,
     {"0:sus 8:oct", "0:sus 3:sh", "0:sus 4:syn", "0:sus 5:arp8", "0:dbl 4:rpizz"}, 6, true, false, false, -1, 1, 2},
    {"bruckner", "Брукнер", "Романтизм", FUNC, SS,
     {"0:sus 5:trem", "0:sus 2:trem", "0:sus 3:trem", "0:sus", "0:dbl 3:rpizz"}, 4, true, false, false, 0, 2, 0},
    {"wagner", "Вагнер", "Романтизм", LUSH, SS,
     {"0:sus 7:oct", "0:sus", "0:sus 5:trem", "0:sus", "0:roots"}, 9, true, false, false, 1, 2, 0},
    {"dvorak", "Дворжак", "Романтизм", FUNC, SR,
     {"0:sus 8:oct", "0:sus 3:off", "0:sus 3:off 7:rep8", "0:sus 3:pizz 8:arp8", "0:dbl 3:rpizz"}, 6, true, false, false, 0, 0, 0},
    {"grieg", "Григ", "Романтизм", FUNC, SR,
     {"0:sus", "0:sus 3:sh", "0:sus 4:off", "0:sus 3:pizz", "0:dbl 2:drone"}, 6, true, false, false, 0, 0, -2},
    {"mahler", "Малер", "Романтизм", FUNC, SS,
     {"0:sus", "0:sus", "0:sus 4:parp", "0:sus", "0:roots 3:rpizz"}, 9, true, false, false, 1, 2, -8},
    {"elgar", "Элгар", "Романтизм", FUNC, SR,
     {"0:sus 7:oct", "0:sus 4:syn", "0:sus 4:arp8", "0:sus", "0:dbl 3:rpizz"}, 7, true, false, false, 0, 0, 0},
    {"strauss", "Рихард Штраус", "Романтизм", LUSH, SS,
     {"0:sus 6:oct", "0:sus", "0:sus", "0:sus", "0:roots"}, 10, true, false, false, 0, 2, 0},
    // ------------------------------------------------------------------ russian school
    {"glinka", "Глинка", "Русская школа", FUNC, SC,
     {"0:sus", "0:sus 3:off", "0:sus 3:rep8", "0:sus 3:pizz", "0:dbl 3:rpizz"}, 5, false, false, false, 0, 0, 0},
    {"borodin", "Бородин", "Русская школа", FUNC, SR,
     {"0:sus", "0:sus 3:syn", "0:sus 3:syn", "0:sus", "0:dbl 2:drone"}, 9, true, false, false, 0, 0, -2},
    {"mussorgsky", "Мусоргский", "Русская школа", MODAL, SS,
     {"0:sus", "0:sus 4:uni", "0:sus 3:det", "0:sus 3:det", "0:dbl"}, 2, false, true, false, 0, 0, 8},
    {"rimsky", "Римский-Корсаков", "Русская школа", FUNC, SS,
     {"0:sus 7:oct", "0:sus 3:trem 8:rep16", "0:sus 4:trem", "0:sus 5:arp8", "0:dbl 3:rpizz"}, 6, true, false, false, 0, 0, 0},
    {"tchaikovsky", "Чайковский", "Русская школа", FUNC, SS,
     {"0:sus 6:oct", "0:sus 4:off", "0:sus 4:off 8:arp8", "0:sus", "0:dbl 3:rpizz"}, 8, true, false, false, 0, 0, 0},
    {"glazunov", "Глазунов", "Русская школа", FUNC, SS,
     {"0:sus 7:oct", "0:sus 3:sh", "0:sus 4:off", "0:sus 5:arp8", "0:dbl 3:rpizz"}, 7, true, false, false, 0, 0, 0},
    {"rachmaninoff", "Рахманинов", "Русская школа", LUSH, SS,
     {"0:sus 5:oct", "0:sus 4:syn", "0:sus 4:syn 8:arp8", "0:sus", "0:roots"}, 9, true, false, false, 0, 0, 2},
    {"scriabin", "Скрябин", "Русская школа", COLOR, SS,
     {"0:sus 6:trem", "0:sus 3:trem", "0:sus 4:arp16", "0:sus", "0:roots"}, 6, true, false, false, 1, 2, -4},
    // ------------------------------------------------------------------ france
    {"berlioz", "Берлиоз", "Франция", FUNC, SS,
     {"0:sus", "0:sus 3:trem 7:rep16", "0:sus 3:rep8", "0:sus 4:pizz", "0:dbl 3:rpizz"}, 5, false, true, false, 0, 0, 4},
    {"saint-saens", "Сен-Санс", "Франция", FUNC, SR,
     {"0:sus", "0:sus 3:arp8", "0:sus 4:parp", "0:sus", "0:dbl 3:rpizz"}, 6, true, false, false, 0, 0, 0},
    {"faure", "Форе", "Франция", COLOR, SR,
     {"0:sus", "0:sus 3:arp8", "0:sus 5:arp8", "0:sus", "0:roots"}, 6, true, false, false, 0, 0, -8},
    {"debussy", "Дебюсси", "Франция", LUSH, SS,
     {"0:sus", "0:sus 2:sh 7:trem", "0:sus 3:sh2", "0:sus 4:arp16", "0:roots 3:rpizz"}, 4, true, false, false, 1, 2, -8},
    {"ravel", "Равель", "Франция", LUSH, SS,
     {"0:sus 7:oct", "0:sus 3:arp16", "0:sus 3:parp", "0:sus 4:pizz", "0:dbl 3:rpizz"}, 5, true, false, false, 0, 0, -2},
    // ------------------------------------------------------------------ twentieth century
    {"sibelius", "Сибелиус", "XX век", FUNC, SS,
     {"0:sus", "0:sus 2:osc8 6:osc16", "0:sus 3:osc8 7:trem", "0:sus", "0:drone"}, 4, true, false, false, 0, 0, -4},
    {"schoenberg", "Шёнберг, «Просветлённая ночь»", "XX век", LUSH, SR,
     {"0:sus", "0:sus 4:trem", "0:sus", "0:sus", "0:roots"}, 10, true, false, false, 1, 2, -2},
    {"janacek", "Яначек", "XX век", COLOR, SR,
     {"0:sus", "0:sus 3:osc16", "0:sus 3:ost", "0:sus 4:stab", "0:dbl"}, 3, false, true, false, 1, 0, 2},
    {"stravinsky", "Стравинский, русский период", "XX век", COLOR, SS,
     {"0:sus 4:aug", "0:sus 3:aug", "0:sus 3:aug", "0:sus 3:aug", "0:dbl"}, 3, false, false, true, 0, 0, 6},
    {"stravinsky-neo", "Стравинский, неоклассика", "XX век", FUNC, SC,
     {"0:sus 6:dot", "0:sus 3:rep8", "0:sus 3:off", "0:sus 2:pizz", "0:dbl 2:rpizz"}, 3, false, false, true, 0, 0, 0},
    {"bartok", "Барток", "XX век", COLOR, SR,
     {"0:sus", "0:sus 3:ost", "0:sus 3:ost", "0:sus 3:pizz", "0:dbl 3:rpizz"}, 3, false, true, true, 0, 0, 4},
    {"prokofiev", "Прокофьев", "XX век", FUNC, SS,
     {"0:sus 7:oct", "0:sus 3:rep8", "0:sus 3:off", "0:sus 2:pizz 7:rep8", "0:dbl 2:rpizz"}, 4, false, true, false, 0, 0, 2},
    {"shostakovich", "Шостакович", "XX век", FUNC, SS,
     {"0:sus", "0:sus 3:uni", "0:sus 3:rep8 7:rep16", "0:sus 4:rep8", "0:dbl 2:drone"}, 3, false, true, false, 0, 0, 2},
    {"vaughan-williams", "Воан-Уильямс", "XX век", MODAL, SS,
     {"0:sus", "0:sus 2:sh", "0:sus 3:sh2", "0:sus", "0:roots 4:drone"}, 4, true, false, false, 1, 2, -8},
    {"barber", "Барбер", "XX век", COLOR, SR,
     {"0:sus", "0:sus", "0:sus", "0:sus", "0:roots"}, 10, true, false, false, 0, 0, -8},
    {"britten", "Бриттен", "XX век", COLOR, SC,
     {"0:sus", "0:sus 3:parp", "0:sus 3:parp", "0:sus 3:pizz", "0:dbl 3:rpizz"}, 4, false, false, false, 0, 0, 0},
    {"schnittke", "Шнитке", "XX век", FUNC, SC,
     {"0:sus", "0:sus 3:rep8 6:trem", "0:sus 3:rep8", "0:sus 2:det 5:walk", "0:dbl"}, 5, false, true, true, 0, 0, 0},
    // ------------------------------------------------------------------ minimalism
    {"part", "Пярт", "Минимализм", MODAL, SR,
     {"0:sus", "0:sus 1:tin", "0:sus 3:tin2", "0:sus", "0:drone"}, 0, true, false, false, 0, 0, -10},
    {"gorecki", "Гурецкий", "Минимализм", MODAL, SS,
     {"0:sus", "0:sus 4:sh", "0:sus", "0:sus", "0:roots"}, 3, true, false, false, -1, 1, -8},
    {"glass", "Гласс", "Минимализм", MODAL, SC,
     {"0:sus", "0:sus 2:osc8", "0:sus 2:alb8 6:alb16", "0:sus 4:osc8", "0:roots"}, 0, true, false, false, 0, 0, -2},
    {"nyman", "Найман", "Минимализм", FUNC, SC,
     {"0:sus 6:oct", "0:sus 2:rep8", "0:sus 2:rep8", "0:sus 2:rep8", "0:dbl"}, 1, false, true, false, 0, 0, 8},
    // ------------------------------------------------------------------ soviet film
    {"dunaevsky", "Дунаевский", "Советское кино", FUNC, SS,
     {"0:sus 6:oct", "0:sus 3:off", "0:sus 3:off", "0:sus 3:pizz", "0:dbl 2:rpizz"}, 6, false, true, false, 0, 0, 4},
    {"schwartz", "Шварц", "Советское кино", FUNC, SR,
     {"0:sus", "0:sus 4:sh", "0:sus 3:arp8", "0:sus", "0:dbl 3:rpizz"}, 5, true, false, false, 0, 0, -6},
    {"sviridov", "Свиридов", "Советское кино", FUNC, SS,
     {"0:sus 6:oct", "0:sus 3:rep8", "0:sus 3:rep8 8:rep16", "0:sus 4:arp8", "0:dbl 3:rpizz"}, 5, true, false, false, 0, 0, 0},
    {"petrov", "Андрей Петров", "Советское кино", LUSH, SR,
     {"0:sus 5:oct", "0:sus 3:off", "0:sus 3:off", "0:sus 3:pizz", "0:dbl 2:rpizz"}, 4, true, false, false, 0, 0, -2},
    {"tariverdiev", "Таривердиев", "Советское кино", LUSH, SC,
     {"0:sus", "0:sus", "0:sus 3:alb8", "0:sus 5:walk", "0:dbl 3:rpizz"}, 5, true, false, false, 0, 0, -6},
    {"zatsepin", "Зацепин", "Советское кино", LUSH, SR,
     {"0:sus 6:oct", "0:sus 3:syn", "0:sus 3:syn", "0:sus 3:walk", "0:dbl 3:rpizz"}, 4, false, true, false, 0, 0, 2},
    {"gladkov", "Гладков", "Советское кино", LUSH, SC,
     {"0:sus", "0:sus 3:off", "0:sus 3:parp", "0:sus 2:pizz", "0:dbl 2:rpizz"}, 4, false, false, false, 0, 0, 0},
    {"rybnikov", "Рыбников", "Советское кино", FUNC, SR,
     {"0:sus 6:oct", "0:sus 4:arp8", "0:sus 3:rep8", "0:sus", "0:roots"}, 4, true, false, false, 0, 0, 0},
    {"artemyev", "Артемьев", "Советское кино", COLOR, SS,
     {"0:sus", "0:sus 6:trem", "0:sus 4:ost", "0:sus", "0:drone"}, 2, true, false, false, 0, 2, -6},
    {"dashkevich", "Дашкевич", "Советское кино", FUNC, SC,
     {"0:sus", "0:sus 3:dot", "0:sus 3:rep8", "0:sus 2:det 5:walk", "0:dbl"}, 5, false, true, false, 0, 0, 2},
    {"krylatov", "Крылатов", "Советское кино", FUNC, SR,
     {"0:sus 6:oct", "0:sus 3:sh", "0:sus 3:arp8", "0:sus", "0:roots 4:rpizz"}, 5, true, false, false, 1, 0, -2},
    {"doga", "Дога", "Советское кино", FUNC, SS,
     {"0:sus 4:oct", "0:sus 3:off", "0:sus 3:off", "0:sus 3:pizz 7:arp8", "0:dbl 2:rpizz"}, 5, true, false, false, 0, 0, 6},
    // ------------------------------------------------------------------ hollywood and world cinema
    {"steiner", "Макс Штайнер", "Голливуд и мировое кино", LUSH, SS,
     {"0:sus 4:oct", "0:sus 3:trem", "0:sus 4:syn", "0:sus", "0:roots"}, 7, true, false, false, 0, 0, 2},
    {"korngold", "Корнгольд", "Голливуд и мировое кино", LUSH, SS,
     {"0:sus 5:oct", "0:sus 3:sh", "0:sus 3:arp16", "0:sus 5:arp8", "0:dbl 3:rpizz"}, 8, true, false, false, 1, 0, 4},
    {"herrmann", "Херрманн", "Голливуд и мировое кино", COLOR, SC,
     {"0:sus 7:trem", "0:sus 3:rep8 8:rep16", "0:sus 3:rep8", "0:sus 4:stab", "0:dbl"}, 3, false, true, false, 0, 1, 2},
    {"rozsa", "Рожа", "Голливуд и мировое кино", MODAL, SS,
     {"0:sus 6:oct", "0:sus 3:uni", "0:sus 3:det", "0:sus 3:det", "0:dbl"}, 3, false, true, false, 0, 0, 8},
    {"bernstein", "Элмер Бернстайн", "Голливуд и мировое кино", FUNC, SS,
     {"0:sus 6:oct", "0:sus 3:ost", "0:sus 3:ost", "0:sus 3:rep8", "0:dbl 3:rpizz"}, 4, false, true, false, 0, 0, 6},
    {"goldsmith", "Голдсмит", "Голливуд и мировое кино", COLOR, SS,
     {"0:sus", "0:sus 3:osc16", "0:sus 3:aug", "0:sus 3:pizz", "0:roots"}, 4, false, true, true, 0, 0, 2},
    {"williams", "Джон Уильямс", "Голливуд и мировое кино", LUSH, SS,
     {"0:sus 5:oct", "0:sus 3:rep16", "0:sus 3:trem", "0:sus 4:rep8", "0:dbl 3:rpizz"}, 8, false, true, false, 0, 0, 6},
    {"barry", "Джон Барри", "Голливуд и мировое кино", LUSH, SS,
     {"0:sus 3:oct", "0:sus", "0:sus 5:syn", "0:sus", "0:roots"}, 1, true, false, false, 1, 2, -4},
    {"morricone", "Морриконе", "Голливуд и мировое кино", FUNC, SR,
     {"0:sus", "0:sus 4:sh 7:trem", "0:sus 3:arp8", "0:sus", "0:roots 4:rpizz"}, 5, true, false, false, 1, 0, -4},
    {"rota", "Нино Рота", "Голливуд и мировое кино", FUNC, SC,
     {"0:sus 6:trem", "0:sus 3:off", "0:sus 3:off", "0:sus 3:pizz", "0:dbl 2:rpizz"}, 5, true, false, false, 0, 0, -2},
    {"horner", "Хорнер", "Голливуд и мировое кино", COLOR, SS,
     {"0:sus 6:oct", "0:sus 4:osc8", "0:sus 3:rep8", "0:sus", "0:roots 5:drone"}, 4, true, false, false, 0, 0, 0},
    {"elfman", "Элфман", "Голливуд и мировое кино", FUNC, SR,
     {"0:sus 7:trem", "0:sus 3:off", "0:sus 3:parp", "0:sus 2:pizz", "0:dbl 2:rpizz"}, 4, false, true, false, 0, 0, 0},
    {"zimmer", "Циммер", "Голливуд и мировое кино", MODAL, SS,
     {"0:sus 8:oct", "0:sus 3:ost", "0:sus 2:rep8 5:ost", "0:sus 4:rep8", "0:roots 6:drone"}, 1, true, true, false, 0, 0, 4},
    {"shore", "Говард Шор", "Голливуд и мировое кино", MODAL, SS,
     {"0:sus", "0:sus 3:sh", "0:sus 4:trem", "0:sus", "0:drone"}, 3, true, false, false, 0, 2, 0},
    {"desplat", "Деспла", "Голливуд и мировое кино", COLOR, SC,
     {"0:sus", "0:sus 2:osc8", "0:sus 3:parp", "0:sus 3:pizz", "0:dbl 2:rpizz"}, 3, false, false, false, 0, 0, -8},
    {"newman", "Томас Ньюман", "Голливуд и мировое кино", COLOR, SC,
     {"0:sus", "0:sus 3:ost", "0:sus", "0:sus 4:pizz", "0:drone"}, 0, true, false, false, 0, 2, -8},
    {"johannsson", "Йоханнссон", "Голливуд и мировое кино", MODAL, SR,
     {"0:sus", "0:sus 2:tin", "0:sus", "0:sus 5:osc8", "0:drone"}, 1, true, false, false, -1, 1, -10},
};
const int NROWS = (int)(sizeof(ROWS) / sizeof(ROWS[0]));
const Composer DEFAULT_COMPOSER = (Composer)8;   // Mozart


// ------------------------------------------------------------------ flavour of a style
// What makes a composer recognizable beyond the textures: how the harmony is coloured, the shape of the
// phrase dynamics, a one-line description and a playing tip. Looked up by id, so the menu table stays as it is.
enum Harm : unsigned {
    H_SEV = 1,      // dominant sevenths from level 3, ii7 from level 5
    H_MAJ7 = 2,     // major sevenths on I and IV from level 3
    H_ADD9 = 4,     // added ninths from level 2 (colour vocabularies)
    H_SIX = 8,      // a final tonic with an added sixth from level 4
    H_IVM = 16,     // borrowed minor subdominant before the tonic from level 4
    H_PIC = 32,     // a minor piece ends on a major tonic
    H_HITCH = 64,   // minor tonic with a major seventh from level 3, every minor chord from level 7
    H_SUS = 128     // dominant with an added fourth from level 4
};
enum Dyn { D_ARCH = 0, D_TERR, D_SWELL, D_FLAT, D_WAVE };

struct Flavor {
    const char* id;
    unsigned harm;
    int dyn;
    const char* manner;
    const char* tip;      // may be null
};

const Flavor FLAVORS[] = {
    {"corelli", H_SEV | H_PIC, D_TERR, "две скрипки над басом, цепи задержаний, контраст громко и тихо", "короткий смычок, лёгкое вибрато"},
    {"vivaldi", H_SEV | H_PIC, D_TERR, "моторные репетиции, бас восьмыми, контраст громко и тихо", nullptr},
    {"purcell", H_PIC, D_TERR, "пунктирные ритмы и строгий бас", nullptr},
    {"telemann", H_SEV | H_PIC, D_TERR, "галантная лёгкость, вторые скрипки вторят первым", nullptr},
    {"bach", H_SEV | H_PIC, D_TERR, "четыре самостоятельные линии, проходящие ноты и задержания", nullptr},
    {"handel", H_SEV | H_PIC, D_TERR, "французская увертюра: пунктир, акценты, шагающий бас", nullptr},
    {"rameau", H_SEV, D_TERR, "танцевальные аккорды на слабую долю, пунктир в басу", nullptr},
    {"haydn", H_SEV, D_ARCH, "квартетная ясность, барабанные восьмые в середине", nullptr},
    {"mozart", H_SEV, D_ARCH, "певучая мелодия над альбертиевыми фигурами", nullptr},
    {"boccherini", H_SEV, D_ARCH, "мягкие синкопы, пиццикато контрабасов, тихая звучность", nullptr},
    {"beethoven", H_SEV, D_SWELL, "репетиции шестнадцатыми, sforzando на сменах гармонии", nullptr},
    {"beethoven-late", H_SEV | H_IVM, D_WAVE, "плотная полифония, широкое расположение, синкопы", nullptr},
    {"schubert", H_SEV | H_IVM, D_ARCH, "репетиции аккордов, мажор и минор рядом, тремоло в кульминации", nullptr},
    {"mendelssohn", H_SEV, D_ARCH, "лёгкое спиккато, высокий регистр, пиццикато в басах", "спиккато у середины смычка, тихо"},
    {"schumann", H_SEV | H_IVM, D_SWELL, "беспокойные синкопы, ломаные аккорды у виолончелей", nullptr},
    {"brahms", H_SEV | H_IVM, D_SWELL, "низкий плотный регистр, синкопы, мелодия в октаву", nullptr},
    {"bruckner", H_SEV, D_SWELL, "тремоло под хоралом, широкие нарастания", nullptr},
    {"wagner", H_IVM, D_SWELL, "хроматическая насыщенность, высокое divisi, долгие нарастания", nullptr},
    {"dvorak", H_SEV, D_ARCH, "танцевальные аккорды на слабую долю, пиццикато баса", nullptr},
    {"grieg", H_SEV | H_IVM, D_ARCH, "народная педаль в басу, прозрачная фактура", nullptr},
    {"mahler", H_SEV | H_IVM, D_WAVE, "огромные дуги, пиццикато альтов вместо арфы, крайние регистры", "в медленной музыке: портаменто на скачках мелодии"},
    {"elgar", H_SEV | H_IVM, D_SWELL, "nobilmente: мелодия в октаву, ломаные аккорды альтов", nullptr},
    {"strauss", H_IVM | H_SIX, D_SWELL, "пышная гармония, широкое расположение, октавы", nullptr},
    {"glinka", H_SEV, D_ARCH, "ясная классическая фактура под песенной мелодией", nullptr},
    {"borodin", H_SEV | H_IVM, D_WAVE, "певучая мелодия над синкопами, педаль в басу", nullptr},
    {"mussorgsky", H_PIC, D_TERR, "модальные трезвучия, тяжёлые унисоны, деташе", nullptr},
    {"rimsky", H_SEV, D_ARCH, "краска: тремоло под темой, ломаные аккорды виолончелей", nullptr},
    {"tchaikovsky", H_SEV | H_IVM, D_SWELL, "мелодия в октаву, аккорды на слабую долю, пиццикато баса", nullptr},
    {"glazunov", H_SEV | H_IVM, D_SWELL, "плотная романтическая фактура, мелодия в октаву", nullptr},
    {"rachmaninoff", H_IVM | H_SIX, D_SWELL, "широкая мелодия в октаву над синкопами, пышная гармония", nullptr},
    {"scriabin", H_ADD9, D_WAVE, "мерцание: тремоло, арпеджио, неразрешённые созвучия", "sul tasto в тихих местах"},
    {"berlioz", H_SEV, D_SWELL, "контрасты: тремоло, шестнадцатые, акценты", nullptr},
    {"saint-saens", H_SEV, D_ARCH, "французская ясность, ломаные аккорды, пиццикато", nullptr},
    {"faure", H_MAJ7 | H_ADD9, D_ARCH, "текучие ломаные аккорды восьмыми, септаккорды", nullptr},
    {"debussy", H_ADD9 | H_SIX, D_FLAT, "параллельные созвучия под мелодией, тихие арпеджио", "con sordino, sul tasto"},
    {"ravel", H_ADD9 | H_MAJ7, D_ARCH, "прозрачное пиццикато, арпеджио, точные краски", nullptr},
    {"sibelius", H_SEV, D_WAVE, "покачивание восьмыми, педаль, тремоло в нарастании", nullptr},
    {"schoenberg", H_IVM | H_SIX, D_SWELL, "позднеромантическая хроматика, тремоло, широкое divisi", nullptr},
    {"janacek", H_ADD9, D_TERR, "короткие повторяющиеся мотивы, резкие акценты", "sul ponticello на остинато"},
    {"stravinsky", 0, D_TERR, "сбитые акценты, жёсткие аккорды с секундами", "все акценты вниз смычком"},
    {"stravinsky-neo", H_SEV, D_TERR, "сухие восьмые, пунктир, неоклассическая ясность", "без вибрато, коротко"},
    {"bartok", 0, D_TERR, "остинато, пиццикато, острые акценты", "в кульминации щелчковое пиццикато"},
    {"prokofiev", H_SEV, D_ARCH, "моторные восьмые, пиццикато, мелодия высоко в октаву", nullptr},
    {"shostakovich", 0, D_SWELL, "унисон вторых скрипок с первыми, моторные шестнадцатые, педаль", nullptr},
    {"vaughan-williams", H_PIC, D_WAVE, "модальные трезвучия, параллельное движение под мелодией", "divisi на два оркестра, второй с сурдиной"},
    {"barber", 0, D_WAVE, "медленные линии, задержания, долгое нарастание", "в начале без вибрато"},
    {"britten", 0, D_ARCH, "игривое пиццикато, ясная фактура", nullptr},
    {"schnittke", H_SEV, D_TERR, "барочная маска: шагающий бас, тремоло, резкие акценты", "тремоло sul ponticello"},
    {"part", 0, D_FLAT, "тинтиннабули: мелодия и тоны тонического трезвучия, педаль тоники", "без вибрато"},
    {"gorecki", 0, D_WAVE, "очень медленные выдержанные линии, низкий регистр", nullptr},
    {"glass", 0, D_FLAT, "повторяющиеся фигуры, ровная динамика", nullptr},
    {"nyman", H_SEV, D_TERR, "пилящие восьмые на барочной гармонии, громко", "маркато на каждой восьмой"},
    {"dunaevsky", H_SEV, D_ARCH, "марш и оперетта: аккорды на слабую долю, мелодия в октаву", nullptr},
    {"schwartz", H_SEV | H_IVM, D_WAVE, "романс: певучая мелодия, ломаные аккорды альтов", nullptr},
    {"sviridov", H_SEV | H_IVM, D_SWELL, "пульсирующие аккорды, мелодия в октаву", nullptr},
    {"petrov", H_IVM, D_ARCH, "лирический вальс, мелодия в октаву", nullptr},
    {"tariverdiev", 0, D_FLAT, "сдержанность: фигуры альтов, шагающий бас, септаккорды", nullptr},
    {"zatsepin", H_SIX, D_ARCH, "лёгкий танец, синкопы, шагающий бас", nullptr},
    {"gladkov", 0, D_ARCH, "игривые аккорды на слабую долю, пиццикато", nullptr},
    {"rybnikov", H_SEV | H_IVM, D_SWELL, "лирика рок-оперы: ломаные аккорды, мелодия в октаву", nullptr},
    {"artemyev", H_ADD9 | H_SUS, D_FLAT, "тянущиеся созвучия, педаль, медленное остинато", "flautando, без атаки"},
    {"dashkevich", H_SEV | H_PIC, D_TERR, "барочная стилизация: пунктир, шагающий бас", nullptr},
    {"krylatov", H_SEV, D_ARCH, "светлая песенная лирика, пиццикато баса", nullptr},
    {"doga", H_SEV | H_IVM, D_SWELL, "большой вальс: мелодия в октаву уже на средних уровнях", nullptr},
    {"steiner", H_IVM | H_SIX, D_SWELL, "оркестровая мелодрама: октавы, тремоло, синкопы", "портаменто в мелодии"},
    {"korngold", H_IVM | H_SIX, D_SWELL, "приключенческая пышность: арпеджио, мелодия высоко", nullptr},
    {"herrmann", H_HITCH, D_TERR, "нервные восьмые, тремоло, удары в басах, минор с большой септимой", "con sordino в тихих местах"},
    {"rozsa", H_PIC, D_SWELL, "эпос: модальные трезвучия, унисоны, деташе", nullptr},
    {"bernstein", H_SEV, D_SWELL, "американский простор: остинато, мелодия в октаву", nullptr},
    {"goldsmith", H_SUS, D_TERR, "сбитые акценты, остинато, кварты в аккордах", nullptr},
    {"williams", H_IVM | H_SIX, D_SWELL, "фанфарные октавы, шестнадцатые, тремоло", nullptr},
    {"barry", 0, D_WAVE, "медленные пышные октавы высоко, синкопы альтов", nullptr},
    {"morricone", H_SEV | H_IVM, D_WAVE, "выдержанная мелодия высоко, тремоло, ломаные аккорды", nullptr},
    {"rota", H_SEV | H_IVM, D_ARCH, "меланхоличный вальс, тремоло мелодии", nullptr},
    {"horner", H_ADD9 | H_SUS, D_WAVE, "кельтская ширь: sus-аккорды, педаль, мелодия в октаву", nullptr},
    {"elfman", H_SEV | H_IVM, D_SWELL, "тёмный вальс: аккорды на слабую долю, пиццикато, тремоло", nullptr},
    {"zimmer", 0, D_SWELL, "остинато 3+3+2, педаль, мелодия в октаву в кульминации", "остинато коротким спиккато, акценты сильно"},
    {"shore", 0, D_SWELL, "модальная эпика: тремоло, педаль, широкое расположение", nullptr},
    {"desplat", H_ADD9, D_FLAT, "лёгкое покачивание и пиццикато, тихо", nullptr},
    {"newman", H_ADD9 | H_MAJ7, D_FLAT, "повторяющиеся остинато, педаль, тихая звучность", "col legno на остинато"},
    {"johannsson", 0, D_FLAT, "медленные педали, тоны трезвучия под мелодией, очень тихо", "без вибрато, flautando"},
};

const Flavor& flavor(const char* id) {
    for (auto& f : FLAVORS)
        if (std::string(f.id) == id) return f;
    static const Flavor none{"", 0, D_ARCH, "", nullptr};
    return none;
}

// chord tones of a quality, for the chord list the voice-leading engine reads
std::set<int> pcsOf(int root, const std::string& q) {
    static const std::map<std::string, std::vector<int>> iv = {
        {"", {0, 4, 7}},        {"m", {0, 3, 7}},       {"7", {0, 4, 7, 10}},  {"maj7", {0, 4, 7, 11}},
        {"m7", {0, 3, 7, 10}},  {"mmaj7", {0, 3, 7, 11}}, {"add9", {0, 2, 4, 7}}, {"madd9", {0, 2, 3, 7}},
        {"6", {0, 4, 7, 9}},    {"m6", {0, 3, 7, 9}},   {"sus4", {0, 5, 7}},   {"m7b5", {0, 3, 6, 10}}};
    std::set<int> out;
    auto it = iv.find(q);
    for (int i : it == iv.end() ? std::vector<int>{0, 4, 7} : it->second) out.insert((root + i) % 12);
    return out;
}

// pitch classes the melody sounds while a chord lasts
std::set<int> melodyPcs(const std::vector<Note>& mel, int start, int end) {
    std::set<int> out;
    for (auto& n : mel)
        if (n.start < end && n.end > start) out.insert(((n.pitch % 12) + 12) % 12);
    return out;
}

void recolour(std::vector<choir::Chord>& ch, const std::vector<Note>& mel, int tonic, bool minor, unsigned harm, int level) {
    auto set = [](choir::Chord& c, const char* q) {
        c.quality = q;
        c.pcs = pcsOf(c.root, q);
    };
    for (size_t i = 0; i < ch.size(); ++i) {
        auto& c = ch[i];
        bool last = i + 1 == ch.size();
        int deg = ((c.root - tonic) % 12 + 12) % 12;
        int nextDeg = last ? -1 : ((ch[i + 1].root - tonic) % 12 + 12) % 12;
        auto mpcs = melodyPcs(mel, c.start, c.end);
        auto melodyAvoids = [&](int pc) { return !mpcs.count(((pc % 12) + 12) % 12); };
        // removing a chord tone is allowed only where the melody does not sing it
        if ((harm & H_IVM) && level >= 4 && !minor && deg == 5 && c.quality == "" && nextDeg == 0 && melodyAvoids(c.root + 4))
            set(c, "m");
        if ((harm & H_PIC) && minor && last && deg == 0 && c.quality == "m" && melodyAvoids(c.root + 3)) set(c, "");
        // adding tones
        if ((harm & H_SEV) && level >= 3 && deg == 7 && c.quality == "") set(c, "7");
        if ((harm & H_SEV) && level >= 5 && deg == 2 && c.quality == "m") set(c, "m7");
        if ((harm & H_SEV) && level >= 5 && minor && deg == 2 && c.quality == "dim") set(c, "m7b5");
        if ((harm & H_MAJ7) && level >= 3 && (deg == 0 || deg == 5) && c.quality == "" && !(last && level < 6)) set(c, "maj7");
        if ((harm & H_MAJ7) && level >= 3 && (deg == 2 || deg == 9 || deg == 4) && c.quality == "m") set(c, "m7");
        if ((harm & H_ADD9) && level >= 2 && deg != 7 && c.quality == "") set(c, "add9");
        if ((harm & H_ADD9) && level >= 2 && c.quality == "m") set(c, "madd9");
        if ((harm & H_SIX) && level >= 4 && last && deg == 0 && c.quality == "") set(c, "6");
        if ((harm & H_HITCH) && minor && c.quality == "m" && ((level >= 3 && deg == 0) || level >= 7)) set(c, "mmaj7");
        if ((harm & H_SUS) && level >= 4 && deg == 7 && (c.quality == "" || c.quality == "7") && !last) set(c, "sus4");
    }
}

struct Profile {
    const Row* row;
    std::vector<Step> ladder[NPARTS];
};

std::vector<Step> parseLadder(const char* text) {
    std::vector<Step> out;
    std::stringstream ss(text);
    std::string item;
    while (ss >> item) {
        auto colon = item.find(':');
        Step st{std::stoi(item.substr(0, colon)), Tex::Sustain};
        for (auto& i : TEX)
            if (item.substr(colon + 1) == i.code) st.tex = i.tex;
        out.push_back(st);
    }
    return out;
}

const Profile& profile(Composer c) {
    static const std::vector<Profile> all = [] {
        std::vector<Profile> v;
        for (int i = 0; i < NROWS; ++i) {
            Profile p;
            p.row = &ROWS[i];
            for (int k = 0; k < NPARTS; ++k) p.ladder[k] = parseLadder(ROWS[i].ladder[k]);
            v.push_back(p);
        }
        return v;
    }();
    return all[(size_t)std::max(0, std::min(NROWS - 1, (int)c))];
}

int clamp10(int x) { return std::max(0, std::min(10, x)); }
int clampVel(int v) { return std::max(1, std::min(127, v)); }

Tex texFor(const Profile& p, int part, int level) {
    Tex t = p.ladder[part].front().tex;
    for (auto& s : p.ladder[part])
        if (level >= s.from) t = s.tex;
    return t;
}

// textures that keep the skeleton line as it is: these lines take the ornaments of the voice-leading engine
bool sustainedTex(Tex t) { return t == Tex::Sustain || t == Tex::Tremolo || t == Tex::OctaveUp; }

int pitchAt(const std::vector<Note>& ns, int t) {
    for (auto& n : ns)
        if (n.start <= t && t < n.end) return n.pitch;
    return -1;
}

int velAt(const std::vector<Note>& ns, int t) {
    for (auto& n : ns)
        if (n.start <= t && t < n.end) return n.vel;
    return 80;
}

struct Ctx {
    const std::vector<choir::Span>& spans;
    int bar, beat, num;
    int holdFrom;        // the last harmony is held, not figured
    bool legato;
    int tonic = 0;       // pitch class
    bool minor = false;
    const std::vector<Note>* melody = nullptr;   // the first violins' line in the skeleton

    const choir::Span* spanAt(int t) const {
        for (auto& s : spans)
            if (s.start <= t && t < s.end) return &s;
        return spans.empty() ? nullptr : &spans.back();
    }
    bool chordTone(int pitch, int t) const {
        auto s = spanAt(t);
        return s && s->pcs.count(((pitch % 12) + 12) % 12) > 0;
    }
    // chord tones of the harmony at t, nearest first, going down (dir -1) or up (+1) from pitch
    std::vector<int> tones(int pitch, int t, int dir, int lo, int hi, int count) const {
        std::vector<int> out;
        for (int p = pitch + dir; p >= lo && p <= hi && (int)out.size() < count; p += dir)
            if (chordTone(p, t)) out.push_back(p);
        return out;
    }
};

Note mk(int s, int e, int pitch, int vel, int art) {
    Note n;
    n.start = s, n.end = e, n.pitch = pitch, n.vel = clampVel(vel), n.art = art;
    return n;
}

// the part of a line that is held at the end: its notes from holdFrom on, as they are
void holdTail(const std::vector<Note>& line, const Ctx& c, std::vector<Note>& out, int art) {
    for (auto& n : line) {
        if (n.end <= c.holdFrom) continue;
        out.push_back(mk(std::max(n.start, c.holdFrom), n.end, n.pitch, n.vel, art));
    }
}

// walks a line in cells of `unit` on the bar grid, up to the held ending
template <typename F>
void cells(const std::vector<Note>& line, const Ctx& c, int unit, F f) {
    for (auto& n : line) {
        int end = std::min(n.end, c.holdFrom);
        int t = n.start;
        while (t < end) {
            int next = std::min(end, (t / unit) * unit + unit);
            if (end - next > 0 && end - next < unit / 2) next = end;   // no splinter at the end of a note
            f(t, next, n);
            t = next;
        }
    }
}

int accent(int t, const Ctx& c) {
    int inBar = t % c.bar;
    if (inBar == 0) return 10;
    if (inBar % c.beat == 0) return 4;
    return -8;
}

std::vector<Note> texSustain(const std::vector<Note>& line, const Ctx& c, int art) {
    std::vector<Note> out;
    for (auto& n : line) out.push_back(mk(n.start, n.end, n.pitch, n.vel, art));
    (void)c;
    return out;
}

std::vector<Note> texRepeat(const std::vector<Note>& line, const Ctx& c, int unit, int art) {
    std::vector<Note> out;
    cells(line, c, unit, [&](int t, int e, const Note& n) { out.push_back(mk(t, e, n.pitch, n.vel + accent(t, c), art)); });
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texAlberti(const std::vector<Note>& line, const Ctx& c, int unit, int part) {
    std::vector<Note> out;
    cells(line, c, unit, [&](int t, int e, const Note& n) {
        // low - high - middle - high, the line's own note on top so the figure stays under the melody
        int high = n.pitch;
        auto below = c.tones(high, t, -1, LO[part], LIMIT[part], 2);
        int mid = below.size() > 0 ? below[0] : high, low = below.size() > 1 ? below[1] : mid;
        if (below.size() < 2) {   // near the open string: build the figure upwards
            auto above = c.tones(high, t, +1, LO[part], TOP[part], 2);
            low = high;
            mid = above.size() > 0 ? above[0] : high;
            high = above.size() > 1 ? above[1] : mid;
        }
        int k = ((t % c.bar) / unit) % 4;
        int p = k == 0 ? low : k == 2 ? mid : high;
        out.push_back(mk(t, e, p, n.vel + (k == 0 ? 6 : -8), A_STACC));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texArp(const std::vector<Note>& line, const Ctx& c, int part) {
    std::vector<Note> out;
    cells(line, c, E, [&](int t, int e, const Note& n) {
        // root - fifth or sixth above - the next chord tone - back: an open broken chord
        int t0 = n.pitch;
        auto up = c.tones(t0 + 3, t, +1, LO[part], TOP[part], 2);
        int t1 = up.size() > 0 ? up[0] : t0, t2 = up.size() > 1 ? up[1] : t1;
        int k = ((t % c.bar) / E) % 4;
        int p = k == 0 ? t0 : k == 2 ? t2 : t1;
        out.push_back(mk(t, e, p, n.vel + (k == 0 ? 4 : -6), c.legato ? A_LEGATO : A_LONG));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

// the extent of a line as beat starts
std::vector<int> beatsOf(const std::vector<Note>& line, const Ctx& c, int step) {
    std::vector<int> out;
    if (line.empty()) return out;
    int t0 = (line.front().start / step) * step, t1 = line.back().end;
    for (auto& n : line) t1 = std::max(t1, n.end);
    for (int t = t0; t < t1; t += step) out.push_back(t);
    (void)c;
    return out;
}

std::vector<Note> texOffbeat(const std::vector<Note>& line, const Ctx& c) {
    std::vector<Note> out;
    for (int b : beatsOf(line, c, c.beat)) {
        if (b >= c.holdFrom) break;
        if (c.num == 3) {   // waltz: rest on one, chords on two and three
            if (b % c.bar == 0) continue;
            int p = pitchAt(line, b);
            if (p >= 0) out.push_back(mk(b, b + c.beat, p, velAt(line, b) - 6, A_STACC));
        } else {
            int t = b + c.beat / 2;
            int p = pitchAt(line, t);
            if (p >= 0) out.push_back(mk(t, b + c.beat, p, velAt(line, t) - 6, A_STACC));
        }
    }
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texPizz(const std::vector<Note>& line, const Ctx& c, int step, int shift, int lo) {
    std::vector<Note> out;
    for (int b : beatsOf(line, c, step)) {
        int p = pitchAt(line, b);
        if (p < 0) continue;
        if (p + shift >= lo) p += shift;
        out.push_back(mk(b, b + c.beat / 2, p, velAt(line, b) + (b % c.bar == 0 ? 8 : 0), A_PIZZ));
    }
    return out;
}

std::vector<Note> texOstinato(const std::vector<Note>& line, const Ctx& c) {
    std::vector<Note> out;
    cells(line, c, X, [&](int t, int e, const Note& n) {
        int k = ((t % c.bar) / X) % 8;
        bool acc = k == 0 || k == 3 || k == 6;
        out.push_back(mk(t, e, n.pitch, n.vel + (acc ? 14 : -10), A_SPICC));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texAugurs(const std::vector<Note>& line, const Ctx& c, bool bite, int part) {
    // accents fall on fixed eighths of a 32-eighth cycle, against the bar line
    static const int ACC[] = {0, 9, 11, 17, 20, 24, 29};
    std::vector<Note> out;
    cells(line, c, E, [&](int t, int e, const Note& n) {
        int k = (t / E) % 32;
        bool acc = std::find(std::begin(ACC), std::end(ACC), k) != std::end(ACC);
        out.push_back(mk(t, e, n.pitch, n.vel + (acc ? 22 : -4), acc ? A_MARC : A_STACC));
        if (acc && bite && n.pitch + 2 <= TOP[part]) out.push_back(mk(t, e, n.pitch + 2, n.vel + 16, A_MARC));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texStabs(const std::vector<Note>& line, const Ctx& c) {
    std::vector<int> at;
    for (auto& s : c.spans) at.push_back(s.start);
    for (int b : beatsOf(line, c, c.bar)) at.push_back(b);
    std::sort(at.begin(), at.end());
    at.erase(std::unique(at.begin(), at.end()), at.end());
    std::vector<Note> out;
    for (int t : at) {
        if (t >= c.holdFrom) break;
        int p = pitchAt(line, t);
        if (p >= 0) out.push_back(mk(t, t + c.beat / 2, p, velAt(line, t) + 20, A_MARC));
    }
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texOctave(const std::vector<Note>& line, const Ctx& c, int part, int art) {
    std::vector<Note> out;
    for (auto& n : line) {
        out.push_back(mk(n.start, n.end, n.pitch, n.vel, art));
        if (n.pitch + 12 <= TOP[part] + 5) out.push_back(mk(n.start, n.end, n.pitch + 12, n.vel, art));
    }
    (void)c;
    return out;
}

// basses: long bass notes of the harmonies, an octave under the cellos where the instrument reaches
std::vector<Note> texRoots(const std::vector<Note>& cello, const Ctx& c) {
    std::vector<Note> out;
    for (auto& s : c.spans) {
        int p = pitchAt(cello, s.start);
        if (p < 0) continue;
        if (p - 12 >= LO[P_CB]) p -= 12;
        if (!out.empty() && out.back().pitch == p && out.back().end == s.start) out.back().end = s.end;
        else out.push_back(mk(s.start, s.end, p, velAt(cello, s.start), A_LONG));
    }
    return out;
}

// off-beat notes tied over the beat: quarter notes that start an eighth late
std::vector<Note> texSyncop(const std::vector<Note>& line, const Ctx& c, int art) {
    std::vector<Note> out;
    for (int b : beatsOf(line, c, c.beat)) {
        int t = b + c.beat / 2;
        if (t >= c.holdFrom) break;
        int p = pitchAt(line, t);
        if (p < 0) continue;
        out.push_back(mk(t, std::min(t + c.beat, c.holdFrom), p, velAt(line, t) - 4, art));
    }
    holdTail(line, c, out, A_LONG);
    return out;
}

// dotted eighth and sixteenth on every beat
std::vector<Note> texDotted(const std::vector<Note>& line, const Ctx& c) {
    std::vector<Note> out;
    int shortLen = c.beat / 4;
    for (int b : beatsOf(line, c, c.beat)) {
        if (b >= c.holdFrom) break;
        int p = pitchAt(line, b), q = pitchAt(line, b + c.beat - shortLen);
        if (p >= 0) out.push_back(mk(b, b + c.beat - shortLen, p, velAt(line, b) + accent(b, c), A_LONG));
        if (q >= 0) out.push_back(mk(b + c.beat - shortLen, b + c.beat, q, velAt(line, b) - 6, A_STACC));
    }
    holdTail(line, c, out, A_LONG);
    return out;
}

// the line's note alternating with the chord tone under it
std::vector<Note> texOsc(const std::vector<Note>& line, const Ctx& c, int unit, int part) {
    std::vector<Note> out;
    cells(line, c, unit, [&](int t, int e, const Note& n) {
        int k = ((t % c.bar) / unit) % 2;
        int p = n.pitch;
        if (k == 1) {
            auto below = c.tones(p, t, -1, LO[part], LIMIT[part], 1);
            auto above = c.tones(p, t, +1, LO[part], TOP[part], 1);
            p = !below.empty() ? below[0] : !above.empty() ? above[0] : p;
        }
        out.push_back(mk(t, e, p, n.vel + (k == 0 ? 2 : -8), A_LEGATO));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

// four chord tones up and back: under the line's note for the upper parts, over it for the cellos
std::vector<Note> texArp16(const std::vector<Note>& line, const Ctx& c, int unit, int part, int art) {
    std::vector<Note> out;
    cells(line, c, unit, [&](int t, int e, const Note& n) {
        std::vector<int> ts;
        if (part >= P_VC) {
            ts.push_back(n.pitch);
            for (int p : c.tones(n.pitch + 3, t, +1, LO[part], TOP[part], 3)) ts.push_back(p);
        } else {
            auto below = c.tones(n.pitch, t, -1, LO[part], LIMIT[part], 3);
            for (auto it = below.rbegin(); it != below.rend(); ++it) ts.push_back(*it);
            ts.push_back(n.pitch);
        }
        int m = (int)ts.size();
        int period = std::max(1, 2 * m - 2);
        int k = ((t % c.bar) / unit) % period;
        int idx = k < m ? k : period - k;
        int nominalEnd = art == A_PIZZ ? t + std::max(60, (e - t) / 2) : e;
        out.push_back(mk(t, nominalEnd, ts[(size_t)idx], n.vel + (k == 0 ? 4 : -6), art));
    });
    if (art == A_PIZZ) {
        for (auto& n : line)
            if (n.end > c.holdFrom && n.start <= c.holdFrom) out.push_back(mk(c.holdFrom, c.holdFrom + c.beat / 2, n.pitch, n.vel, A_PIZZ));
    } else {
        holdTail(line, c, out, A_LONG);
    }
    return out;
}

// the melody an octave or two under the first violins, wherever the instrument takes it
std::vector<Note> texUnison(const Ctx& c, int part, int art) {
    std::vector<Note> out;
    if (!c.melody || c.melody->empty()) return out;
    int hi = 0;
    for (auto& n : *c.melody) hi = std::max(hi, n.pitch);
    int shift = part == P_V2 ? -12 : part == P_VA ? -12 : -24;
    while (hi + shift > TOP[part]) shift -= 12;
    for (auto& n : *c.melody) {
        int p = n.pitch + shift;
        while (p < LO[part]) p += 12;
        out.push_back(mk(n.start, n.end, p, n.vel, art));
    }
    return out;
}

// follows the melody's rhythm on a tone under it: a chord tone (parallel motion) or a tone of the tonic
// triad (tintinnabuli); depth 1 is the nearest such tone at least a third below, depth 2 the next one
std::vector<Note> texShadow(const std::vector<Note>& own, const Ctx& c, int part, int depth, bool tonicTriad, int art) {
    std::vector<Note> out;
    if (!c.melody || c.melody->empty()) return texSustain(own, c, art);
    int third = c.minor ? 3 : 4;
    auto inTriad = [&](int p) {
        int pc = (((p - c.tonic) % 12) + 12) % 12;
        return pc == 0 || pc == third || pc == 7;
    };
    for (auto& n : *c.melody) {
        std::vector<int> ts;
        for (int p = n.pitch - 3; p >= LO[part] && (int)ts.size() < depth; --p)
            if (tonicTriad ? inTriad(p) : c.chordTone(p, n.start)) ts.push_back(p);
        int p = (int)ts.size() >= depth ? ts[(size_t)depth - 1] : pitchAt(own, n.start);
        if (p < 0) continue;
        while (p > LIMIT[part]) p -= 12;
        out.push_back(mk(n.start, n.end, p, n.vel - 4, art));
    }
    return out;
}

// bass in eighths: the bass note on the beat, a chord tone on the way to the next beat after it
std::vector<Note> texWalk(const std::vector<Note>& line, const Ctx& c, int part) {
    std::vector<Note> out;
    cells(line, c, E, [&](int t, int e, const Note& n) {
        int p = n.pitch;
        if ((t % c.beat) != 0) {
            int target = pitchAt(line, (t / c.beat + 1) * c.beat);
            if (target < 0) target = p;
            int dir = target > p ? 1 : -1;
            int pick = -1;
            for (int q = p + dir; q != target && q >= LO[part] && q <= TOP[part]; q += dir)
                if (c.chordTone(q, t)) { pick = q; break; }
            if (pick < 0) pick = p + 12 <= TOP[part] - 5 ? p + 12 : p - 12 >= LO[part] ? p - 12 : p;   // an octave leap
            p = pick;
        }
        out.push_back(mk(t, e, p, n.vel + accent(t, c), A_LONG));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

// pedal: the tonic while the harmony has it, the dominant when only that fits, the root otherwise
std::vector<Note> texDrone(const std::vector<Note>& bassLine, const Ctx& c, int part) {
    std::vector<Note> out;
    auto low = [&](int pc) {
        int p = LO[part] + ((pc - LO[part]) % 12 + 12) % 12;
        return p;
    };
    for (auto& s : c.spans) {
        int pc = s.pcs.count(c.tonic) ? c.tonic : s.pcs.count((c.tonic + 7) % 12) ? (c.tonic + 7) % 12 : s.root;
        int p = low(pc);
        int v = velAt(bassLine, s.start);
        // re-bowed every two bars so the pedal does not die away
        for (int t = s.start; t < s.end;) {
            int e = std::min(s.end, (t / (2 * c.bar) + 1) * 2 * c.bar);
            if (!out.empty() && out.back().pitch == p && out.back().end == t && t % (2 * c.bar) != 0) out.back().end = e;
            else out.push_back(mk(t, e, p, v, A_LONG));
            t = e;
        }
    }
    return out;
}

std::vector<Note> buildPart(Tex tex, int part, const std::vector<Note>& line, const Ctx& c, const Row& pr, int level) {
    int longArt = pr.legato ? A_LEGATO : A_LONG;
    switch (tex) {
        case Tex::Sustain: return texSustain(line, c, longArt);
        case Tex::Detache: return texRepeat(line, c, c.beat, A_LONG);
        case Tex::Repeat8: return texRepeat(line, c, E, A_STACC);
        case Tex::Repeat16: return texRepeat(line, c, X, A_SPICC);
        case Tex::Tremolo: return texSustain(line, c, A_TREM);
        case Tex::Alberti8: return texAlberti(line, c, E, part);
        case Tex::Alberti16: return texAlberti(line, c, X, part);
        case Tex::Arp8: return texArp(line, c, part);
        case Tex::Arp16: return texArp16(line, c, X, part, A_LEGATO);
        case Tex::PizzArp: return texArp16(line, c, E, part, A_PIZZ);
        case Tex::Offbeat: return texOffbeat(line, c);
        case Tex::Syncop: return texSyncop(line, c, longArt);
        case Tex::Dotted: return texDotted(line, c);
        case Tex::Osc8: return texOsc(line, c, E, part);
        case Tex::Osc16: return texOsc(line, c, X, part);
        case Tex::PizzBeat: return texPizz(line, c, c.beat, 0, LO[part]);
        case Tex::Ostinato: return texOstinato(line, c);
        case Tex::Augurs: return texAugurs(line, c, pr.bite && level >= 7 && (part == P_V2 || part == P_VA), part);
        case Tex::Stabs: return texStabs(line, c);
        case Tex::OctaveUp: return texOctave(line, c, part, longArt);
        case Tex::Unison: return texUnison(c, part, longArt);
        case Tex::Shadow: return texShadow(line, c, part, 1, false, longArt);
        case Tex::Shadow2: return texShadow(line, c, part, 2, false, longArt);
        case Tex::Tintin: return texShadow(line, c, part, 1, true, longArt);
        case Tex::Tintin2: return texShadow(line, c, part, 2, true, longArt);
        case Tex::Walk: return texWalk(line, c, part);
        case Tex::Drone: return texDrone(line, c, part);
        default: return texSustain(line, c, longArt);
    }
}

const char* artWord(int art) {
    switch (art) {
        case A_LEGATO: return "легато";
        case A_STACC: return "стаккато";
        case A_SPICC: return "спиккато";
        case A_PIZZ: return "пиццикато";
        case A_TREM: return "тремоло";
        case A_MARC: return "маркато";
        default: return "длинные ноты";
    }
}

const char* patchFor(Section s) {
    switch (s) {
        case Section::Quartet: return "сольные инструменты (solo strings)";
        case Section::Baroque: case Section::Classical: return "камерная секция (chamber strings)";
        default: return "большая секция (symphonic strings)";
    }
}

}  // namespace

// ---------------------------------------------------------------- names
const char* composerLabel(Composer c) { return profile(c).row->label; }
const char* composerId(Composer c) { return profile(c).row->id; }
const char* composerGroup(Composer c) { return profile(c).row->group; }
const char* composerManner(Composer c) { return flavor(profile(c).row->id).manner; }

Composer composerById(const std::string& id) {
    for (int i = 0; i < NROWS; ++i)
        if (id == ROWS[i].id) return (Composer)i;
    return DEFAULT_COMPOSER;
}

const char* sectionLabel(Section s) {
    switch (s) {
        case Section::Auto: return "По стилю";
        case Section::Quartet: return "Квартет";
        case Section::Baroque: return "Барочный";
        case Section::Classical: return "Классический";
        case Section::Romantic: return "Романтический";
        case Section::Symphonic: return "Симфонический";
    }
    return "";
}

const char* sectionDesks(Section s, Composer c) {
    if (s == Section::Auto) s = profile(c).row->section;
    switch (s) {
        case Section::Quartet: return "1-1-1-1";
        case Section::Baroque: return "4-4-2-2-1";
        case Section::Classical: return "6-6-4-3-2";
        case Section::Romantic: return "12-10-8-6-4";
        default: return "16-14-12-10-8";
    }
}

const char* libraryLabel(Library l) {
    switch (l) {
        case Library::None: return "Без оптимизации";
        case Library::SpitfireUACC: return "Spitfire (UACC)";
        case Library::SplitTracks: return "Дорожка на штрих";
        case Library::BBCSO: return "BBC Symphony Orchestra";
    }
    return "";
}

static_assert(NROWS == NUM_COMPOSERS, "NUM_COMPOSERS in Strings.h must equal the number of rows in the table");

static choir::Style baseStyle(Vocab v) {
    switch (v) {
        case COLOR: return choir::Style::Contemporary;
        case LUSH: return choir::Style::Gospel;
        case MODAL: return choir::Style::Renaissance;
        default: return choir::Style::Bach;
    }
}

static choir::Skeleton skeletonFor(const Row& pr) {
    choir::Skeleton k;
    k.on = true;
    k.freeParallels = pr.vocab == COLOR || pr.vocab == LUSH;
    // register of the melody and spacing of the upper parts
    k.melCLo += 5 * pr.mel, k.melCHi += 5 * pr.mel;
    if (pr.mel < 0) k.melCLo -= 2, k.melCHi -= 2;
    if (pr.spacing == 1) k.maxSA = 10, k.maxAT = 10, k.maxTB = 19;
    if (pr.spacing == 2) k.maxSA = 19, k.maxAT = 19, k.maxTB = 28;
    return k;
}

choir::Settings liveSettings(Composer c) {
    const Row& pr = *profile(c).row;
    choir::Settings s;
    s.style = baseStyle(pr.vocab);
    s.skeleton = skeletonFor(pr);
    return s;
}

// ---------------------------------------------------------------- arrange
Result arrange(const std::vector<Note>& notes, const Meta& meta, const Settings& s) {
    Result res;
    const Profile& prof = profile(s.composer);
    const Row& pr = *prof.row;
    Section sec = s.section == Section::Auto ? pr.section : s.section;
    bool quartet = sec == Section::Quartet;

    // is the top line the user's own melody? then it is protected unless its level is set by hand
    choir::Analysis an = choir::analyse(notes);
    bool bare = an.chords.empty() && !an.melody.empty();
    bool real = an.realMelody() || s.input == InputMode::HarmonizeMelody || (s.input == InputMode::Auto && bare);

    int lv[NPARTS];
    for (int i = 0; i < NPARTS; ++i) lv[i] = s.part[i] >= 0 ? clamp10(s.part[i]) : clamp10(s.global);
    if (real && s.part[P_V1] < 0) lv[P_V1] = 0;
    Tex tex[NPARTS];
    for (int i = 0; i < NPARTS; ++i) {
        tex[i] = texFor(prof, i, lv[i]);
        // octave divisi and big gestures need a section
        if ((quartet || sec == Section::Baroque) && tex[i] == Tex::OctaveUp) tex[i] = Tex::Sustain;
        // a part cannot shadow a melody it carries itself
        if (i == P_V1 && (tex[i] == Tex::Unison || tex[i] == Tex::Shadow || tex[i] == Tex::Tintin)) tex[i] = Tex::Sustain;
    }

    // ---- skeleton
    choir::Settings cs;
    cs.style = baseStyle(pr.vocab);
    cs.input = s.input;
    cs.variant = s.variant;
    cs.skeleton = skeletonFor(pr);
    cs.locks = s.skeletonLocks;
    const Flavor& fl = flavor(pr.id);
    {
        unsigned harm = fl.harm;
        int level = clamp10(s.global);
        cs.harmony = [harm, level](std::vector<choir::Chord>& ch, const std::vector<Note>& mel, int tonic, bool minor) {
            recolour(ch, mel, tonic, minor, harm, level);
        };
    }
    auto skLevel = [&](int part) {
        if (sustainedTex(tex[part])) return std::min(lv[part], pr.ornCap);
        return std::min(lv[part], 2);    // a figured part needs plain chord tones under it
    };
    cs.global = skLevel(P_V2);
    cs.S = skLevel(P_V1), cs.A = skLevel(P_V2), cs.T = skLevel(P_VA), cs.B = skLevel(P_VC);
    choir::Result base = choir::arrange(notes, meta, cs);
    static_cast<choir::Result&>(res) = base;
    if (!base.ok()) return res;

    std::vector<Note> line[4];
    for (auto& p : base.parts)
        for (int i = 0; i < 4; ++i)
            if (p.name == PART_NAMES[i]) line[i] = p.notes;
    for (int i = 0; i < 4; ++i) {
        std::sort(line[i].begin(), line[i].end(), [](const Note& a, const Note& b) { return a.start < b.start; });
        res.skeleton.push_back({PART_NAMES[i], line[i]});
    }

    // ---- texture
    int num = meta.num > 0 ? meta.num : 4, den = meta.den > 0 ? meta.den : 4;
    int beat = den == 8 && num % 3 == 0 ? 3 * E : TPB * 4 / den;
    int bar = num * TPB * 4 / den;
    int hold = INT_MAX;
    if (!base.spans.empty() && base.spans.back().end - base.spans.back().start >= 2 * beat) hold = base.spans.back().start;
    Ctx ctx{base.spans, bar, beat, num, hold, pr.legato};
    {
        int major = ((base.keyFifths == 99 ? 0 : base.keyFifths) * 7 % 12 + 12) % 12;
        ctx.minor = base.keyMinor;
        ctx.tonic = base.keyMinor ? (major + 9) % 12 : major;
        ctx.melody = &line[P_V1];
    }

    std::vector<Note> out[NPARTS];
    for (int i = 0; i < 4; ++i) out[i] = buildPart(tex[i], i, line[i], ctx, pr, lv[i]);
    if (!quartet) {
        if (tex[P_CB] == Tex::Roots) {
            out[P_CB] = texRoots(line[P_VC], ctx);
        } else if (tex[P_CB] == Tex::Drone) {
            out[P_CB] = texDrone(line[P_VC], ctx, P_CB);
        } else if (tex[P_CB] == Tex::RootsPizz) {
            int step = num == 4 ? 2 * beat : bar;     // one and three in 4/4, the downbeat elsewhere
            out[P_CB] = texPizz(line[P_VC], ctx, step, -12, LO[P_CB]);
        } else {
            for (auto n : out[P_VC]) {
                if (n.pitch - 12 >= LO[P_CB]) n.pitch -= 12;
                if (n.pitch <= LIMIT[P_CB]) out[P_CB].push_back(n);
            }
        }
    }
    // sforzando on the chord changes
    if (pr.accents) {
        std::set<int> changes;
        for (auto& sp : base.spans) changes.insert(sp.start);
        for (int i = 1; i < NPARTS; ++i) {
            if (lv[i] < 5) continue;
            for (auto& n : out[i])
                if (changes.count(n.start) && n.start < hold && n.art != A_PIZZ && n.art != A_TREM)
                    n.art = A_MARC, n.vel = clampVel(n.vel + 18);
        }
    }

    res.parts.clear();
    for (int i = 0; i < NPARTS; ++i) {
        if (i == P_CB && quartet) continue;
        std::stable_sort(out[i].begin(), out[i].end(), [](const Note& a, const Note& b) {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        res.parts.push_back({PART_NAMES[i], out[i]});
        res.textures[i] = texLabel(tex[i]);
    }
    // finished parts the user locked come back as they were
    res.locked.clear();
    for (auto& p : res.parts) {
        auto it = s.locks.find(p.name);
        if (it == s.locks.end()) continue;
        p.notes = it->second;
        res.locked.push_back(p.name);
    }
    for (int i = 0; i < NPARTS; ++i) res.lv[i] = lv[i];
    res.styleLabel = pr.label;
    if (pr.vel)
        for (auto& p : res.parts)
            if (!s.locks.count(p.name))
                for (auto& n : p.notes) n.vel = clampVel(n.vel + pr.vel);
    res.sectionLabel = std::string(sectionDesks(sec, s.composer));
    res.manner = fl.manner;
    res.dynamics = fl.dyn;
    res.quartet = quartet;

    // ---- checks of the finished parts
    // the skeleton's checks speak the choir's language: rename the voices, drop what does not apply
    {
        std::vector<choir::Issue> kept;
        for (auto i : res.issues) {
            if (i.what.find("расстояние больше октавы") != std::string::npos) continue;   // wide spacing is normal for strings
            auto swap = [&](const std::string& a, const std::string& b) {
                for (size_t pos = i.what.find(a); pos != std::string::npos; pos = i.what.find(a, pos + b.size()))
                    i.what.replace(pos, a.size(), b);
            };
            swap("поёт", "играет");
            const char* from[4] = {"S", "A", "T", "B"};
            const char* to[4] = {"V1", "V2", "Va", "Vc"};
            for (int k = 0; k < 4; ++k) {
                if (i.what.rfind(std::string(from[k]) + " ", 0) == 0) i.what.replace(0, 1, to[k]);
                swap(std::string(" ") + from[k] + "-", std::string(" ") + to[k] + "-");
                swap(std::string("-") + from[k], std::string("-") + to[k]);
                swap(std::string("выше ") + from[k], std::string("выше ") + to[k]);
            }
            kept.push_back(i);
        }
        res.issues = kept;
    }
    for (auto& p : res.parts) {
        int idx = 0;
        for (int i = 0; i < NPARTS; ++i)
            if (p.name == PART_NAMES[i]) idx = i;
        int low = 0, high = 0;
        for (auto& n : p.notes) low += n.pitch < LO[idx], high += n.pitch > LIMIT[idx];
        if (low) res.issues.push_back({"диапазон", p.name + ": " + std::to_string(low) + " нот ниже нижней струны"});
        if (high) res.issues.push_back({"диапазон", p.name + ": " + std::to_string(high) + " нот выше предела инструмента"});
    }

    // ---- advice: what the notes cannot say
    res.hints.push_back({0, "", std::string("Состав ") + res.sectionLabel + ": " + patchFor(sec)});
    if (fl.tip) res.hints.push_back({0, "", fl.tip});
    int shortParts = 0, pizzParts = 0;
    for (auto& p : res.parts) {
        std::map<int, int> count;
        for (auto& n : p.notes) ++count[n.art];
        int best = A_LONG, bn = -1;
        for (auto& kv : count)
            if (kv.second > bn) best = kv.first, bn = kv.second;
        if (p.notes.empty()) continue;
        res.hints.push_back({p.notes.front().start, p.name, artWord(best)});
        if (best == A_STACC || best == A_SPICC) ++shortParts;
        if (best == A_PIZZ) ++pizzParts;
    }
    if (shortParts >= 2 && pizzParts == 0)
        res.hints.push_back({0, "", "Коротким штрихам не хватает атаки: попробуй перевести альты на пиццикато"});
    if (!quartet && lv[P_V2] >= 6 && tex[P_V1] == Tex::Sustain && tex[P_V2] == Tex::Sustain)
        res.hints.push_back({0, "", "Для напряжения продублируй первые скрипки во вторых на тремоло"});
    return res;
}

std::string Result::report() const {
    std::ostringstream o;
    if (!error.empty()) return "Ошибка: " + error;
    o << "Стиль: " << styleLabel << "   Состав: " << sectionLabel;
    if (!manner.empty()) o << "\nМанера: " << manner;
    o << "\nТональность: " << keyLabel
      << "   Мелодия: " << melodyLabel;
    if (melodyShift) o << " (перенос " << (melodyShift > 0 ? "+" : "") << melodyShift << " полутонов)";
    o << "\nАккорды: ";
    for (size_t i = 0; i < chords.size(); ++i) o << (i ? " | " : "") << chords[i];
    o << "\nФактура:";
    bool first = true;
    for (int i = 0; i < NPARTS; ++i) {
        if (textures[i].empty()) continue;
        o << (first ? " " : ", ") << PART_SHORT[i] << " " << textures[i] << " (" << lv[i] << ")";
        first = false;
    }
    if (!locked.empty()) {
        o << "\nЗакреплены:";
        for (size_t i = 0; i < locked.size(); ++i) o << (i ? ", " : " ") << locked[i];
    }
    if (issues.empty())
        o << "\nПроверка: нарушений нет";
    else
        for (auto& i : issues) o << "\n! " << i.where << ": " << i.what;
    o << "\nСоветы:";
    bool firstH = true;
    for (auto& h : hints) {
        if (!h.part.empty()) continue;   // per-part bowing is already in the texture line
        o << (firstH ? " " : "; ") << h.text;
        firstH = false;
    }
    return o.str();
}

// ---------------------------------------------------------------- MIDI for a library
namespace {

void putVlq(std::vector<unsigned char>& o, uint32_t v) {
    unsigned char b[5];
    int n = 0;
    b[n++] = (unsigned char)(v & 0x7F);
    while ((v >>= 7) > 0) b[n++] = (unsigned char)((v & 0x7F) | 0x80);
    while (n > 0) o.push_back(b[--n]);
}

void putTrack(std::vector<unsigned char>& o, const std::vector<unsigned char>& t) {
    o.insert(o.end(), {'M', 'T', 'r', 'k'});
    uint32_t n = (uint32_t)t.size();
    o.insert(o.end(), {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n});
    o.insert(o.end(), t.begin(), t.end());
}

struct Ev {
    int t, order;                          // order at one tick: note off, controllers and text, note on
    std::vector<unsigned char> bytes;
};

int uacc(int art) {
    switch (art) {
        case A_LEGATO: return 20;   // legato
        case A_STACC: return 40;    // short
        case A_SPICC: return 42;    // short, very short (spiccato)
        case A_PIZZ: return 56;     // plucked
        case A_TREM: return 11;     // tremolo
        case A_MARC: return 52;     // short marcato
        default: return 1;          // long
    }
}

// groups for one track per bowing
int artClass(int art) { return art == A_PIZZ ? 2 : art == A_TREM ? 3 : (art == A_STACC || art == A_SPICC || art == A_MARC) ? 1 : 0; }
const char* CLASS_NAME[4] = {"long", "short", "pizz", "trem"};
const int CLASS_PROGRAM[4] = {48, 48, 45, 44};   // General MIDI: string ensemble, pizzicato, tremolo

int soundingEnd(const Note& n, bool overlap, bool nextJoins) {
    int d = n.end - n.start;
    switch (n.art) {
        case A_STACC: return n.start + std::max(30, d / 2);
        case A_SPICC: return n.start + std::max(30, d * 3 / 10);
        case A_PIZZ: return n.start + std::max(40, d / 2);
        case A_MARC: return n.start + std::max(40, d * 3 / 4);
        case A_LEGATO: return n.end + (overlap && nextJoins ? 24 : 0);   // legato patches need the notes to overlap
        default: return n.end;
    }
}

// phrase dynamics on the mod wheel, by the shape of the style:
//   arch: one arch over four bars, the peak a little after the middle;
//   terraced: loud and soft blocks of two bars, as in baroque music;
//   swell: wide two-bar swells; flat: nearly still; wave: one long arch over eight bars
int dynamicsAt(int t, int bar, int total, int shape) {
    const double pi = 3.14159265358979;
    auto arch = [&](int phrase, double lo, double span) {
        double x = (double)(t % phrase) / phrase;
        double w = x < 0.6 ? x / 0.6 * 0.5 : 0.5 + (x - 0.6) / 0.4 * 0.5;
        return lo + span * std::sin(pi * w);
    };
    int four = std::max(bar, std::min(4 * bar, ((total + bar - 1) / bar) * bar));
    double v;
    switch (shape) {
        case D_TERR: v = (t / (2 * bar)) % 2 == 0 ? 96.0 : 62.0; break;
        case D_SWELL: v = arch(std::max(bar, std::min(2 * bar, four)), 38.0, 74.0); break;
        case D_FLAT: v = 58.0 + 6.0 * std::sin(2 * pi * (double)t / (8.0 * bar)); break;
        case D_WAVE: v = arch(std::max(bar, std::min(8 * bar, ((total + bar - 1) / bar) * bar)), 44.0, 58.0); break;
        default: v = arch(four, 46.0, 44.0); break;
    }
    int tail = four / 4;
    if (shape != D_FLAT && total - t < tail) v = std::min(v, 40.0 + 30.0 * (double)(total - t) / tail);   // dies away at the end
    return std::max(1, std::min(127, (int)std::lround(v)));
}

std::vector<unsigned char> trackBytes(const std::string& name, int chan, const std::vector<Note>& notes,
                                      const std::vector<std::pair<int, std::string>>& texts, const MidiOptions& o,
                                      int program, bool curve, int bar, int total, int shape) {
    std::vector<Ev> ev;
    bool lib = o.library != Library::None;
    bool overlap = lib;
    int cur = -1;
    for (size_t i = 0; i < notes.size(); ++i) {
        const Note& n = notes[i];
        bool joins = false;
        for (size_t k = i + 1; k < notes.size() && notes[k].start <= n.end; ++k)
            if (notes[k].start == n.end && (notes[k].art == A_LEGATO || notes[k].art == A_LONG)) joins = true;
        int end = soundingEnd(n, overlap, joins);
        bool uaccOn = o.library == Library::SpitfireUACC || o.library == Library::BBCSO;
        if (uaccOn && uacc(n.art) != cur) {
            cur = uacc(n.art);
            ev.push_back({std::max(0, n.start - 5), 1, {(unsigned char)(0xB0 | chan), 32, (unsigned char)cur}});
        }
        ev.push_back({n.start, 2, {(unsigned char)(0x90 | chan), (unsigned char)n.pitch, (unsigned char)n.vel}});
        ev.push_back({end, 0, {(unsigned char)(0x80 | chan), (unsigned char)n.pitch, 0}});
    }
    if (curve && !notes.empty()) {
        int last = -1;
        for (int t = notes.front().start; t < total; t += 60) {
            int v = dynamicsAt(t, bar, total, shape);
            if (v == last) continue;
            last = v;
            ev.push_back({t, 1, {(unsigned char)(0xB0 | chan), 1, (unsigned char)v}});
        }
    }
    for (auto& tx : texts) {
        std::vector<unsigned char> b{0xFF, 0x01};
        putVlq(b, (uint32_t)tx.second.size());
        b.insert(b.end(), tx.second.begin(), tx.second.end());
        ev.push_back({tx.first, 1, b});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t != b.t ? a.t < b.t : a.order < b.order; });

    std::vector<unsigned char> t;
    t.insert(t.end(), {0x00, 0xFF, 0x03});
    putVlq(t, (uint32_t)name.size());
    t.insert(t.end(), name.begin(), name.end());
    // a General MIDI program so the DAW opens the track with strings; UACC uses controller 32 itself,
    // so no bank select is written there
    if (o.library != Library::SpitfireUACC && o.library != Library::BBCSO && program >= 0) {
        t.insert(t.end(), {0x00, (unsigned char)(0xB0 | chan), 0x00, 0x00});
        t.insert(t.end(), {0x00, (unsigned char)(0xB0 | chan), 0x20, 0x00});
        t.insert(t.end(), {0x00, (unsigned char)(0xC0 | chan), (unsigned char)program});
    }
    int last = 0;
    for (auto& e : ev) {
        putVlq(t, (uint32_t)(e.t - last));
        last = e.t;
        t.insert(t.end(), e.bytes.begin(), e.bytes.end());
    }
    t.insert(t.end(), {0x00, 0xFF, 0x2F, 0x00});
    return t;
}

}  // namespace

std::vector<unsigned char> writeMidi(const Result& r, const Meta& meta, const MidiOptions& o) {
    int num = meta.num > 0 ? meta.num : 4, den = meta.den > 0 ? meta.den : 4;
    int bar = num * TPB * 4 / den;
    int total = 0;
    for (auto& p : r.parts)
        for (auto& n : p.notes) total = std::max(total, n.end);

    struct Tr { std::string name; std::vector<Note> notes; int program; bool curve; std::vector<std::pair<int, std::string>> texts; };
    std::vector<Tr> tracks;
    bool lib = o.library != Library::None;
    for (size_t pi = 0; pi < r.parts.size(); ++pi) {
        if (o.only >= 0 && (int)pi != o.only) continue;
        const Part& p = r.parts[pi];
        std::vector<std::pair<int, std::string>> texts;
        for (auto& h : r.hints)
            if (h.part == p.name) texts.push_back({h.start, h.text});
        if (o.library == Library::SplitTracks) {
            bool first = true;
            for (int c = 0; c < 4; ++c) {
                std::vector<Note> ns;
                for (auto& n : p.notes)
                    if (artClass(n.art) == c) ns.push_back(n);
                if (ns.empty()) continue;
                tracks.push_back({p.name + " " + CLASS_NAME[c], ns, CLASS_PROGRAM[c], o.dynamics && (c == 0 || c == 3),
                                  first ? texts : std::vector<std::pair<int, std::string>>{}});
                first = false;
            }
        } else if (o.library == Library::BBCSO) {
            // tracks named like the BBC SO patches they are meant for
            static const char* SECTION[NPARTS] = {"Violins 1", "Violins 2", "Violas", "Celli", "Basses"};
            static const char* LEADER[NPARTS] = {"Violin 1 Leader", "Violin 2 Leader", "Viola Leader", "Celli Leader", "Bass Leader"};
            std::string name = p.name;
            for (int k = 0; k < NPARTS; ++k)
                if (p.name == PART_NAMES[k]) name = r.quartet ? LEADER[k] : SECTION[k];
            tracks.push_back({name, p.notes, 48, o.dynamics, texts});
        } else {
            tracks.push_back({p.name, p.notes, 48, lib && o.dynamics, texts});
        }
    }

    std::vector<unsigned char> out{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1};
    out.push_back((unsigned char)((tracks.size() + 1) >> 8));
    out.push_back((unsigned char)(tracks.size() + 1));
    out.push_back((unsigned char)(TPB >> 8));
    out.push_back((unsigned char)(TPB & 0xFF));
    std::vector<unsigned char> c;
    std::string cn = "Conductor";
    c.insert(c.end(), {0x00, 0xFF, 0x03, (unsigned char)cn.size()});
    c.insert(c.end(), cn.begin(), cn.end());
    int dpow = 0;
    while ((1 << dpow) < den) ++dpow;
    c.insert(c.end(), {0x00, 0xFF, 0x58, 0x04, (unsigned char)num, (unsigned char)dpow, 24, 8});
    c.insert(c.end(), {0x00, 0xFF, 0x51, 0x03, (unsigned char)(meta.tempo >> 16), (unsigned char)(meta.tempo >> 8),
                       (unsigned char)meta.tempo});
    std::vector<Hint> general = r.hints;
    if (o.library == Library::BBCSO)
        general.push_back({0, "", r.quartet ? "BBC SO: на дорожки патчи Leader, штрихи переключаются по UACC"
                                            : "BBC SO: на дорожки патчи Violins 1, Violins 2, Violas, Celli, Basses; штрихи переключаются по UACC"});
    for (auto& h : general) {
        if (!h.part.empty() || o.only >= 0) continue;
        c.insert(c.end(), {0x00, 0xFF, 0x01});
        putVlq(c, (uint32_t)h.text.size());
        c.insert(c.end(), h.text.begin(), h.text.end());
    }
    c.insert(c.end(), {0x00, 0xFF, 0x2F, 0x00});
    putTrack(out, c);
    for (size_t i = 0; i < tracks.size(); ++i) {
        int chan = (int)i < 9 ? (int)i : std::min(15, (int)i + 1);   // channel 10 is drums in General MIDI
        putTrack(out, trackBytes(tracks[i].name, chan, tracks[i].notes, tracks[i].texts, o, tracks[i].program,
                                 tracks[i].curve, bar, total, r.dynamics));
    }
    return out;
}

int soundingEnd(const Note& n) { return soundingEnd(n, false, false); }

std::string toMusicXml(const Result& r, const Meta& meta, const std::string& title) {
    std::vector<choir::ScoreWord> words;
    for (auto& h : r.hints)
        if (h.part.empty()) words.push_back({h.start, "", h.text});
    return choir::toMusicXml(r, meta, title, words, "Strings Arranger");
}

}  // namespace strings
