#include "poplan/machine.hpp"

#include "poplan/console.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace poplan {

namespace {

constexpr std::uint8_t rau_logical = 004;
constexpr std::uint8_t rau_multiplicative = 010;

struct ErrorMessage {
    std::uint16_t code;
    std::uint8_t length;
    std::string_view text;
};

// Disk zone 01200 words 0103..0344 contain this ordered diagnostic catalog.
// The spelling intentionally follows the mixed Cyrillic/Latin glyphs which
// the original character converter sends to the terminal.
constexpr std::array<ErrorMessage, 0242> error_messages{{
    {000002, 23, "НЕВЕРНОЕ УПОТРЕБЛЕНИЕ ◇"},
    {000003, 19, "НЕОПОЗНАННЫЙ СИМВОЛ"},
    {000004, 12, "% БЕЗ СКОБОК"},
    {000175, 27, "ЧУЖОЙ СИМВОЛ В ВОСЬМЕРИЧНОМ"},
    {000177, 23, "ЧУЖОЙ СИМВОЛ В ДВОИЧНОМ"},
    {000271, 19, "ЧИСЛО ВНЕ ДИАПАЗОНА"},
    {000273, 15, "ДЛИНА ЦЕЛОГО>12"},
    {000275, 22, "ДЛИНА ВОСЬМЕРИЧНОГО>13"},
    {000277, 18, "ДЛИНА ДВОИЧНОГО>40"},
    {000500, 28, "МАСRЕSULТS ВНЕ МАСRО ФУНКЦИИ"},
    {000600, 26, "САRRУОN НЕ НАШЕЛ СЛОВА ЕND"},
    {002012, 21, "САNСЕL В ТЕЛЕ ФУНКЦИИ"},
    {002020, 20, "ПОВТОРНАЯ ДЕКЛАРАЦИЯ"},
    {002021, 22, "НЕДОПУСТИМЫЙ ПРИОРИТЕТ"},
    {002022, 19, "ДЕКЛАРАЦИЯ НЕ СЛОВА"},
    {002023, 32, "ДЕКЛАРАЦИЯ СИНТАКСИЧЕСКОГО СЛОВА"},
    {002024, 35, "НЕТ ) В СПИСКЕ ЭЛЕМЕНТОВ ДЕКЛАРАЦИИ"},
    {002025, 32, "ДЕКЛАРАЦИЯ ЗАЩИЩЕННОЙ ПЕРЕМЕННОЙ"},
    {004000, 30, "ПОСЛЕ NОNОР НЕТ ИДЕНТИФИКАТОРА"},
    {004010, 15, "НЕТ РАЗДЕЛИТЕЛЯ"},
    {004020, 15, "НЕТ РАЗДЕЛИТЕЛЯ"},
    {004030, 18, "ОШИБКА В ВЫРАЖЕНИИ"},
    {004040, 34, "ПРИСВАИВАНИЕ ЗАЩИЩЕННОЙ ПЕРЕМЕННОЙ"},
    {004050, 31, "НЕПРАВИЛЬНОЕ УПОТРЕБЛЕНИЕ ТОЧКИ"},
    {004060, 25, "НЕПРАВИЛЬНОЕ ПРИСВАИВАНИЕ"},
    {004140, 20, "ПОСЛЕ GОТО НЕТ МЕТКИ"},
    {004400, 17, "ПОСЛЕ \" НЕТ СЛОВА"},
    {004600, 31, "НЕДОПУСТИМАЯ ЗАКРЫВАЮЩАЯ СКОБКА"},
    {004610, 25, "НЕТ ЗАКРЫВАЮЩЕЙ \" У СЛОВА"},
    {004700, 19, "ОШИБКА В ИМПЕРАТИВЕ"},
    {004710, 13, "ОТСУТСТВУЕТ )"},
    {004720, 14, "ОТСУТСТВУЕТ %)"},
    {004730, 14, "ОТСУТСТВУЕТ %]"},
    {004740, 16, "ОТСУТСТВУЕТ ТНЕN"},
    {004750, 32, "ОТСУТСТВУЕТ ЕLSЕ, СLОSЕ ИЛИ ЕХIТ"},
    {004760, 15, "ОТСУТСТВУЕТ ЕND"},
    {010000, 19, "АРГУМЕНТ + НЕ ЧИСЛО"},
    {010001, 19, "АРГУМЕНТ - НЕ ЧИСЛО"},
    {010002, 19, "АРГУМЕНТ * НЕ ЧИСЛО"},
    {010003, 19, "АРГУМЕНТ / НЕ ЧИСЛО"},
    {010010, 26, "INСНАRIТЕМ(Х);Х-НЕ ФУНКЦИЯ"},
    {010030, 25, "МАСRЕSULТS(Х);Х-НЕ СПИСОК"},
    {010040, 28, "НЕВЕРНАЯ СПЕЦИФИКАЦИЯ ЗАПИСИ"},
    {010045, 51, "ПОПЫТКА РАЗЛОЖИТЬ НЕ ЗАПИСЬ ИЛИ ЗАПИСЬ НЕ ТОГО ТИПА"},
    {010050, 52, "ПОПЫТКА ВЫБОРКИ НЕ ИЗ ЗАПИСИ ИЛИ ЗАПИСИ НЕ ТОГО ТИПА"},
    {010055, 50, "ПОПЫТКА ИЗМЕНИТЬ НЕ ЗАПИСЬ ИЛИ ЗАПИСЬ НЕ ТОГО ТИПА"},
    {010060, 23, "НЕТ МЕСТА В ПОЛЕ ЗАПИСИ"},
    {010070, 28, "НЕВЕРНЫЙ АРГУМЕНТ ИНИЦИАТОРА"},
    {010075, 27, "НЕТ МЕСТА В ЭЛЕМЕНТЕ СТРИПА"},
    {010100, 34, "НЕДОПУСТИМЫЙ НОМЕР ЭЛЕМЕНТА СТРИПА"},
    {010105, 37, "НЕВЕРНЫЙ АРГУМЕНТ ФУНКЦИИ ТИПА SUВSСR"},
    {010110, 24, "SТRIРFNS(Х,У) ОШИБКА В У"},
    {010120, 30, "СОNТ(Х)/DЕSТRЕF(Х) Х НЕ ССЫЛКА"},
    {010125, 21, "->СОNТ(Х) Х НЕ ССЫЛКА"},
    {010130, 18, "SIGN(Х) Х НЕ ЧИСЛО"},
    {010140, 24, "DАТАLЕNGТН(Х) Х НЕ СТРИП"},
    {010150, 28, "VАLОF(Х)/RЕFОF(Х) Х НЕ СЛОВО"},
    {010155, 30, "RЕFОF(Х)/ ->VАLОF(Х) Х ЗАЩИЩЕН"},
    {010170, 42, "НЕВЕРНЫЙ АРГУМЕНТ ФУНКЦИЙ DАТАLISТ/АРРDАТА"},
    {010200, 30, "НЕВЕРНЫЙ АРГУМЕНТ ФУНКЦИИ СОРУ"},
    {010210, 29, "Х FNСОМР У;Х ИЛИ У НЕ ФУНКЦИЯ"},
    {012001, 24, "ПРИСВАИВАНИЕ ЗАЩИЩЕННОМУ"},
    {012002, 31, "ВЫ ВЗЯЛИ СО СТЕКА СЛИШКОМ МНОГО"},
    {012010, 22, "ВЫПОЛНЯЕТСЯ НЕ ФУНКЦИЯ"},
    {012011, 19, "->F(Х),F-НЕ ФУНКЦИЯ"},
    {012020, 24, "АРГ.НD,ТL,NULL НЕ СПИСОК"},
    {012021, 19, "НD(NIL) ИЛИ ТL(NIL)"},
    {012022, 23, "АРГ.FNТОLISТ НЕ ФУНКЦИЯ"},
    {012023, 9, "DЕSТ(NIL)"},
    {012024, 20, "АРГ.DЕSТРАIR НЕ ПАРА"},
    {012030, 34, "АРГ.UРDАТЕR ИЛИ FNРRОРS НЕ ФУНКЦИЯ"},
    {012031, 42, "АРГ.FNРАRТ ИЛИ FRОZVАL НЕ СLОSURЕ FUNСТIОN"},
    {012032, 31, "ПРИСВАИВАНИЕ ЗАЩИЩЕННОЙ ФУНКЦИИ"},
    {012033, 24, "В FRОZVАL ЗАСЫЛ.НЕ СТРИП"},
    {012040, 22, "ЧТЕНИЕ ЗАКРЫТОГО ФАЙЛА"},
    {012041, 22, "ЗАПИСЬ В ЗАКРЫТЫЙ ФАЙЛ"},
    {012042, 21, "НЕТ ЛИСТОВ ДЛЯ ФАЙЛОВ"},
    {012043, 11, "ЗАПРЕЩ.ЗОНА"},
    {012044, 13, "ЗАПРЕЩ.ЗАПИСЬ"},
    {012045, 30, "АРГ.ПОТРЕБИТЕЛЯ ЛИТЕР НЕ ЦЕЛОЕ"},
    {012046, 16, "НЕПР.АРГ.РОРМЕSS"},
    {012050, 36, "АРГ.FRОNТ ИЛИ ВАСК НЕ ПАРА И НЕ LINК"},
    {012060, 16, "ПРЕРЫВАНИЕ ВВОДА"},
    {012070, 24, "АРГ.РАRТАРРLУ НЕ ФУНКЦИЯ"},
    {012071, 23, "АРГ.РАRТАРРLУ НЕ СПИСОК"},
    {012100, 21, "ИСПОРЧЕН ВОUNDSRЕСОRD"},
    {012101, 22, "НЕДОПУСТ.ИНД.В МАССИВЕ"},
    {012102, 17, "НЕКОРР.ВОUNDSLISТ"},
    {012110, 22, "АРГ.РRRЕАL НЕ ВЕЩЕСТВ."},
    {012111, 32, "РRRЕАL(Х,N1,N2):НЕКОРР.N1 ИЛИ N2"},
    {012120, 21, "ЕRRFUN:МНОГО СИМВОЛОВ"},
    {012130, 12, "ОШ.В ФОРМИР."},
    {012131, 14, "ПАСП.НЕ СSТRIР"},
    {012132, 19, "НЕТ МЕСТА ДЛЯ ПАСП."},
    {012150, 18, "ГР.ОБМ.:НЕПР.1 АРГ"},
    {012151, 18, "ГР.ОБМ.:НЕПР.2 АРГ"},
    {012152, 18, "ГР.ОБМ.:НЕПР.3 АРГ"},
    {012153, 22, "ГР.ОБМ.:МАЛА СТРУКТУРА"},
    {013016, 15, "ДЕЛЕНИЕ НА НУЛЬ"},
    {013017, 15, "ПЕРЕПОЛНЕНИЕ АУ"},
    {013020, 19, "ЧИСЛО В ЧУЖОМ ЛИСТЕ"},
    {013023, 16, "СНЯТА ОПЕРАТОРОМ"},
    {013024, 16, "КОНТРОЛЬ КОМАНДЫ"},
    {013025, 16, "ОСТАНОВ ПО СЧИТ."},
    {013026, 17, "ОСТАНОВ ПО ЗАПИСИ"},
    {013027, 14, "ОСТАНОВ ПО КРА"},
    {013040, 11, "ДАЙ ТРАКТЫ!"},
    {013041, 16, "ОБРАЩ.К НЕЗАК.МЛ"},
    {013045, 9, "ОШИБКА МЛ"},
    {013046, 18, "ИСТЕК.ВРЕМЯ ПО ЭК."},
    {013047, 15, "ДАЙ МЕТРЫ АЦПУ!"},
    {013052, 9, "ОШИБКА МД"},
    {013060, 9, "ОШИБКА МБ"},
    {013062, 16, "ДАЙ ЗАПИСЬ НА МЛ"},
    {013064, 18, "АRСSIN(Х):АВS(Х)>1"},
    {013065, 13, "КОРЕНЬ(Х):Х<0"},
    {013066, 16, "ЛОГАРИФМ(Х):Х=<0"},
    {013067, 12, "ЕХР(Х):Х>=44"},
    {014000, 26, "INТОF(Х),Х НЕ ВЕЩЕСТВЕННОЕ"},
    {014010, 21, "А//В,А ИЛИ В НЕ ЦЕЛОЕ"},
    {014030, 14, "Х<У,Х НЕ ЧИСЛО"},
    {014031, 14, "Х<У,У НЕ ЧИСЛО"},
    {014040, 14, "Х>У,Х НЕ ЧИСЛО"},
    {014041, 14, "Х>У,У НЕ ЧИСЛО"},
    {014050, 15, "Х=<У,Х НЕ ЧИСЛО"},
    {014051, 15, "Х=<У,У НЕ ЧИСЛО"},
    {014060, 15, "Х>=У,Х НЕ ЧИСЛО"},
    {014061, 15, "Х>=У,У НЕ ЧИСЛО"},
    {014100, 26, "SР(Х) ИЛИ NL(Х),Х НЕ ЦЕЛОЕ"},
    {014120, 22, "DЕSТWОRD(Х),Х НЕ СЛОВО"},
    {014130, 27, "НЕВЕРНЫЕ АРГУМЕНТЫ У СНАRWО"},
    {014130, 31, "СНАRWОRD(Х,У),У=<0 ИЛИ НЕ ЦЕЛОЕ"},
    {014140, 27, "НЕВЕРНЫЕ АРГУМЕНТЫ У СОNSWО"},
    {014150, 21, "МЕАNING(Х),Х НЕ СЛОВО"},
    {014160, 21, "РRВIN(Х,У),У НЕ ЦЕЛОЕ"},
    {014170, 21, "РRОСТ(Х,У),У НЕ ЧИСЛО"},
    {014230, 24, "СНАRWОRD(Х,У),Х НЕ СЛОВО"},
    {014250, 24, "Х->МЕАNING(У),У НЕ СЛОВО"},
    {014260, 24, "РRSТRING(Х),Х НЕ ЦЕПОЧКА"},
    {014300, 21, "ПЕРЕД SWIТСН НЕ ЦЕЛОЕ"},
    {014310, 39, "ПОСЛЕ SWIТСН НЕТ МЕТКИ С НУЖНЫМ НОМЕРОМ"},
    {014400, 18, "SQRТ(Х),Х НЕ ЧИСЛО"},
    {014410, 17, "SIN(Х),Х НЕ ЧИСЛО"},
    {014420, 17, "СОS(Х),Х НЕ ЧИСЛО"},
    {014430, 20, "АRСТАN(Х),Х НЕ ЧИСЛО"},
    {014440, 17, "ТАN(Х),Х НЕ ЧИСЛО"},
    {014450, 17, "LОG(Х),Х НЕ ЧИСЛО"},
    {014460, 17, "ЕХР(Х),Х НЕ ЧИСЛО"},
    {014470, 21, "Х!У, Х ИЛИ У НЕ ЧИСЛО"},
    {014500, 37, "->DАТАWОRD(Х),Х СТАНДАРТНАЯ СТРУКТУРА"},
    {014600, 31, "LОGSНIFТ(Х,У),Х<0 ИЛИ НЕ ЦЕЛОЕ "},
    {014610, 24, "LОGSНIFТ(Х,У),У НЕ ЦЕЛОЕ"},
    {014620, 27, "LОGNОТ(Х), Х<0 ИЛИ НЕ ЦЕЛОЕ"},
    {014630, 28, "LОGАND(Х,У),Х<0 ИЛИ НЕ ЦЕЛОЕ"},
    {014640, 27, "LОGОR(Х,У),Х<0 ИЛИ НЕ ЦЕЛОЕ"},
    {014700, 21, "СНАRОUТ(Х),Х НЕ ЦЕЛОЕ"},
    {014710, 31, "NUМВЕRRЕАD(), ЧИТАЕТСЯ НЕ ЧИСЛО"},
    {014730, 28, "LОGАND(Х,У),У<0 ИЛИ НЕ ЦЕЛОЕ"},
    {014740, 27, "LОGОR(Х,У),У<0 ИЛИ НЕ ЦЕЛОЕ"},
    {015000, 19, "МЕТКИ НЕ ОПРЕДЕЛЕНЫ"},
    {015100, 20, "МЕТКА УЖЕ ОПРЕДЕЛЕНА"},
    {015100, 20, "МЕТКА УЖЕ ОПРЕДЕЛЕНА"},
}};

constexpr bool error_messages_are_ordered()
{
    for (std::size_t index = 1; index != error_messages.size(); ++index) {
        if (error_messages[index].code < error_messages[index - 1].code) {
            return false;
        }
    }
    return true;
}

static_assert(error_messages_are_ordered(),
              "native POPLAN error catalog must remain ordered");

const ErrorMessage *find_error_message(std::uint16_t code)
{
    const auto found = std::lower_bound(
        error_messages.begin(), error_messages.end(), code,
        [](const ErrorMessage &message, std::uint16_t value) {
            return message.code < value;
        });
    return found != error_messages.end() && found->code == code
        ? &*found : nullptr;
}

} // namespace

std::uint16_t Machine::p16672()
{
    // 16672 begins conversion after the scanner has accepted a decimal point.
    const std::uint16_t saved = address_add(registers_[017], -7);
    accumulator_ = memory_[saved];
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074475)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    alu_mode_ = 006;
    arithmetic_add(memory_[0], false, false);
    memory_[saved] = accumulator_;

    accumulator_ = memory_[address_add(registers_[001], 074456)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 074460)] = accumulator_;
    registers_[015] = 016677;
    return 016605;
}

std::uint16_t Machine::p16675()
{
    accumulator_ = memory_[address_add(registers_[001], 074456)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 074460)] = accumulator_;
    registers_[015] = 016677;
    return 016605;
}

std::uint16_t Machine::p16676()
{
    registers_[015] = 016677;
    return 016605;
}

std::uint16_t Machine::p16677()
{
    // Accumulate one fractional digit while the independent record and
    // tagged-byte routines remain explicit call boundaries.
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074475)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    alu_mode_ = 006;
    arithmetic_add(memory_[0], false, false);
    multiply(memory_[address_add(registers_[001], 074460)]);
    arithmetic_add(
        memory_[address_add(registers_[017], -7)], false, false);
    memory_[address_add(registers_[017], -7)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[001], 074460)];
    select_alu_group(rau_logical);
    multiply(memory_[address_add(registers_[001], 074456)]);
    memory_[address_add(registers_[001], 074460)] = accumulator_;
    registers_[015] = 016705;
    return 016742;
}

std::uint16_t Machine::p16705()
{
    // A zero lookup result is another fractional digit.  The other accepted
    // result is the `$` exponent marker; anything else finishes the literal.
    remainder_ = accumulator_;
    if (!accumulator_condition()) {
        return address_add(registers_[001], 074415);
    }

    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074516)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_condition()) {
        return address_add(registers_[001], 074364);
    }

    registers_[005] = 016736;
    registers_[015] = 016710;
    return 016505;
}

std::uint16_t Machine::p16710()
{
    registers_[015] = 016711;
    return 016742;
}

std::uint16_t Machine::p16711()
{
    // Select the positive or negative decimal-exponent multiplier.  With no
    // explicit sign the current character is the first exponent digit.
    remainder_ = accumulator_;
    if (!accumulator_condition()) {
        return address_add(registers_[001], 074437);
    }

    accumulator_ = memory_[address_add(registers_[003], 1)];
    select_alu_group(rau_logical);
    Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074517)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (!accumulator_condition()) {
        return address_add(registers_[001], 074434);
    }

    old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074520)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    registers_[016] = 0300;
    remainder_ = accumulator_;
    if (accumulator_condition()) {
        return 03014;
    }

    registers_[005] = 016737;
    registers_[015] = 016716;
    return 016505;
}

std::uint16_t Machine::p16715()
{
    registers_[015] = 016716;
    return 016505;
}

std::uint16_t Machine::p16716()
{
    registers_[015] = 016717;
    return 016742;
}

std::uint16_t Machine::p16717()
{
    registers_[016] = 0300;
    remainder_ = accumulator_;
    if (accumulator_condition()) {
        return 03014;
    }
    return 016720;
}

std::uint16_t Machine::p16720()
{
    accumulator_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 074457)] = accumulator_;
    registers_[015] = 016722;
    return 016605;
}

std::uint16_t Machine::p16721()
{
    registers_[015] = 016722;
    return 016605;
}

std::uint16_t Machine::p16722()
{
    // exponent = exponent * 10 + digit, preserving the original cyclic-add
    // sequence and the independent tagged-byte lookup call.
    const std::uint16_t scratch =
        address_add(registers_[001], 074457);
    accumulator_ = memory_[scratch];
    select_alu_group(rau_logical);
    shift_accumulator(-3);
    accumulator_ = cyclic_add(accumulator_, memory_[scratch]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    accumulator_ = cyclic_add(accumulator_, memory_[scratch]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    accumulator_ = cyclic_add(accumulator_, memory_[registers_[003]]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[scratch] = accumulator_;
    alu_mode_ = 006;
    registers_[015] = 016726;
    return 016742;
}

std::uint16_t Machine::p16726()
{
    remainder_ = accumulator_;
    if (!accumulator_condition()) {
        return address_add(registers_[001], 074440);
    }

    accumulator_ = memory_[address_add(registers_[001], 074457)];
    select_alu_group(rau_logical);
    registers_[002] = accumulator_.address();
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 074521)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    registers_[016] = 0271;
    remainder_ = accumulator_;
    if (accumulator_condition()) {
        return 03014;
    }
    return 016731;
}

std::uint16_t Machine::p16731()
{
    // Apply 10 or 0.1 once per decimal exponent digit.  This loop remains in
    // BESM arithmetic so the low-bit truncation is identical to the image.
    while (registers_[002] != 0) {
        accumulator_ = memory_[address_add(registers_[017], -7)];
        select_alu_group(rau_logical);
        multiply(memory_[registers_[005]]);
        memory_[address_add(registers_[017], -7)] = accumulator_;
        registers_[002] = address_add(registers_[002], -1);
        remainder_ = accumulator_;
        if (accumulator_condition()) {
            continue;
        }

        accumulator_ = cyclic_add(
            accumulator_, memory_[address_add(registers_[001], 074522)]);
        remainder_ = Word48();
        select_alu_group(rau_multiplicative);
        remainder_ = accumulator_;
        if (!accumulator_condition()) {
            continue;
        }
        registers_[016] = 0271;
        return 03014;
    }
    return 016645;
}

std::uint16_t Machine::p16645()
{
    // 16645 is the common successful exit from the real-number scanner at
    // 16672..16735.  The scanner has copied every source character to the
    // packed external-code buffer beginning at r1+74276; r1+74275 holds its
    // character count in the upper half-word.  Parse that retained spelling
    // natively instead of treating the value accumulated at r17-7 as the
    // authoritative result.
    const std::size_t length = static_cast<std::size_t>(
        memory_[address_add(registers_[001], 074275)].raw() >> 24);
    std::string text;
    if (length <= core_words * 6) {
        text.reserve(length);
        const std::uint16_t first_word = address_add(registers_[001], 074276);
        for (std::size_t index = 0; index != length; ++index) {
            text.push_back(static_cast<char>(memory_byte(first_word, index)));
        }
    }

    const auto parse_real = [this](std::string_view spelling)
        -> std::optional<Word48> {
        const std::size_t point = spelling.find('.');
        if (point == std::string_view::npos) {
            return std::nullopt;
        }
        const std::size_t exponent_mark = spelling.find('$', point + 1);
        const std::size_t fraction_end = exponent_mark == std::string_view::npos
            ? spelling.size() : exponent_mark;
        if (fraction_end == point + 1) {
            return std::nullopt;
        }

        std::uint64_t whole = 0;
        for (std::size_t index = 0; index != point; ++index) {
            const unsigned char character =
                static_cast<unsigned char>(spelling[index]);
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            whole = whole * 10 + character - '0';
        }
        for (std::size_t index = point + 1; index != fraction_end; ++index) {
            if (spelling[index] < '0' || spelling[index] > '9') {
                return std::nullopt;
            }
        }

        bool negative_exponent = false;
        std::uint16_t exponent = 0;
        if (exponent_mark != std::string_view::npos) {
            std::size_t index = exponent_mark + 1;
            if (index != spelling.size()
                && (spelling[index] == '+' || spelling[index] == '-')) {
                negative_exponent = spelling[index] == '-';
                ++index;
            }
            if (index == spelling.size()) {
                return std::nullopt;
            }
            for (; index != spelling.size(); ++index) {
                const unsigned char character =
                    static_cast<unsigned char>(spelling[index]);
                if (character < '0' || character > '9') {
                    return std::nullopt;
                }
                exponent = static_cast<std::uint16_t>(
                    exponent * 10 + character - '0');
            }
        }

        // Replay the scanner's BESM arithmetic at routine granularity.  The
        // decimal constants are read from the image, so the native parser
        // retains the original truncation rather than host binary64 rounding.
        constexpr std::uint64_t integer_tag = 06400000000000000ULL;
        const auto real_of_integer = [this](std::uint64_t integer) {
            accumulator_ = Word48(integer_tag | integer);
            alu_mode_ = 006;
            arithmetic_add(memory_[0], false, false);
            return accumulator_;
        };

        Word48 value = real_of_integer(whole);
        const Word48 tenth =
            memory_[address_add(registers_[001], 074456)];
        Word48 place = tenth;
        for (std::size_t index = point + 1; index != fraction_end; ++index) {
            const Word48 digit = real_of_integer(
                static_cast<unsigned char>(spelling[index]) - '0');
            accumulator_ = digit;
            multiply(place);
            arithmetic_add(value, false, false);
            value = accumulator_;

            accumulator_ = place;
            multiply(tenth);
            place = accumulator_;
        }

        const Word48 exponent_factor = memory_[
            negative_exponent ? 016737 : 016736];
        for (std::uint16_t index = 0; index != exponent; ++index) {
            accumulator_ = value;
            multiply(exponent_factor);
            value = accumulator_;
        }
        return value;
    };

    // Native conversion uses the machine arithmetic helpers as scratch.  At
    // this exit the original code performs only XTA -7, so retain its RMR and
    // non-group RAU bits exactly while replacing the resulting accumulator.
    const Word48 saved_remainder = remainder_;
    const std::uint8_t saved_alu_mode = alu_mode_;
    const std::optional<Word48> parsed = parse_real(text);
    remainder_ = saved_remainder;
    alu_mode_ = saved_alu_mode;
    accumulator_ = parsed.value_or(
        memory_[address_add(registers_[017], -7)]);
    select_alu_group(rau_logical);
    registers_[015] = 016376;
    return 03275;
}

std::uint16_t Machine::p03106_print_error_text()
{
    // 03106..03107 suppresses the explanatory line when either diagnostic
    // flag is set. The original branch rejoins at 03124.
    accumulator_ = memory_[03203];
    select_alu_group(rau_logical);
    const Word48 first_flag = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() | memory_[03205].raw());
    remainder_ = first_flag;
    select_alu_group(rau_logical);
    if (accumulator_.raw() != 0) {
        return 03124;
    }

    // 03110..03113 uses this word while locating zone 01200 and clears it
    // before searching the diagnostic descriptors.  The native catalog does
    // not need the transient zone base, but the final memory effect remains.
    memory_[03200] = Word48();

    const ErrorMessage *message = find_error_message(
        memory_[03174].address());
    if (message == nullptr) {
        // This is the unsuccessful 16254 lookup result tested by 03116.
        accumulator_ = memory_[016305];
        select_alu_group(rau_logical);
        return 03124;
    }

    const std::vector<std::uint8_t> bytes = encode_gost_text(message->text);
    if (bytes.size() != message->length) {
        throw MachineError("native POPLAN error message length mismatch");
    }
    for (const std::uint8_t byte : bytes) {
        append_native_output_byte(byte);
    }

    // CHAR_SEQUENCE returns the character count in ACC with its VJM link at
    // 03122. Keep the following newline and source-object formatter visible.
    accumulator_ = Word48(message->length);
    select_alu_group(rau_logical);
    registers_[015] = 03122;
    return 03122;
}

} // namespace poplan
