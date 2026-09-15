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

struct KeywordDescriptor {
    std::uint16_t record;
    Word48 name;
    Word48 class_word;
    Word48 property_word;
    Word48 value_word;
};

// Static POPLAN dictionary records 01400..02737.  These are raw descriptor
// data words, not BESM instructions.  The corresponding memory remains live:
// recognition uses this snapshot only for the immutable packed names.
constexpr std::array<KeywordDescriptor, 0270> keyword_descriptors{{
    {01400, Word48("<"), Word48(06500000000000000ULL), Word48(05600000000002054ULL), Word48(06600000000006626ULL)},
    {01404, Word48(">"), Word48(06500000000000000ULL), Word48(05600000000002534ULL), Word48(06600000000006620ULL)},
    {01410, Word48("=<"), Word48(06500000000000000ULL), Word48(05600000000000000ULL), Word48(06600000000006634ULL)},
    {01414, Word48(">="), Word48(06500000000000000ULL), Word48(05600000000001460ULL), Word48(06600000000006642ULL)},
    {01420, Word48("+"), Word48(06500000000000000ULL), Word48(05200000000002420ULL), Word48(06600000000006672ULL)},
    {01424, Word48("-"), Word48(06500000000000000ULL), Word48(05200000000002520ULL), Word48(06600000000006702ULL)},
    {01430, Word48("*"), Word48(06500000000000000ULL), Word48(05000000000000000ULL), Word48(06600000000006676ULL)},
    {01434, Word48("/"), Word48(06500000000000000ULL), Word48(05000000000000000ULL), Word48(06600000000006706ULL)},
    {01440, Word48("!"), Word48(06500000000000000ULL), Word48(04600000000000000ULL), Word48(06600000000007022ULL)},
    {01444, Word48("//"), Word48(06500000000000000ULL), Word48(05000000000002540ULL), Word48(06600000000003424ULL)},
    {01450, Word48("="), Word48(06500000000000000ULL), Word48(05600000000000000ULL), Word48(06600000000003374ULL)},
    {01454, Word48("::"), Word48(06500000000000000ULL), Word48(04400000000001744ULL), Word48(06600000000003337ULL)},
    {01460, Word48("<>"), Word48(06500000000000000ULL), Word48(04400000000000000ULL), Word48(06600000000007134ULL)},
    {01464, Word48("ATOM"), Word48(06500000000000000ULL), Word48(04000000000002644ULL), Word48(06600000000007361ULL)},
    {01470, Word48("BACK"), Word48(06500000000000000ULL), Word48(04000000000002224ULL), Word48(06600707300007100ULL)},
    {01474, Word48("BOOLAN"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000007421ULL)},
    {01500, Word48("BOOLOR"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000007410ULL)},
    {01504, Word48("BOUNDS"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600742600007433ULL)},
    {01510, Word48("CHARIN"), Word48(06500000000000000ULL), Word48(04000000000001520ULL), Word48(06500000000000000ULL)},
    {01514, Word48("CHAROU"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000007475ULL)},
    {01520, Word48("CHARWO"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000010071ULL)},
    {01524, Word48("COMPIL"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000010217ULL)},
    {01530, Word48("CONS"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000003337ULL)},
    {01534, Word48("CONSPA"), Word48(06500000000000000ULL), Word48(04000000000001624ULL), Word48(06600000000007125ULL)},
    {01540, Word48("CONSRE"), Word48(06500000000000000ULL), Word48(04000000000001630ULL), Word48(06600000000010277ULL)},
    {01544, Word48("CONSWO"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000010113ULL)},
    {01550, Word48("CONT"), Word48(06500000000000000ULL), Word48(04000000000002350ULL), Word48(06601025200010257ULL)},
    {01554, Word48("COPY"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000010305ULL)},
    {01560, Word48("CUCHIN"), Word48(06500000000000000ULL), Word48(00100000000000000ULL), Word48(06600000000007472ULL)},
    {01564, Word48("CUCHOU"), Word48(06500000000000000ULL), Word48(00100000000000000ULL), Word48(06600000000007514ULL)},
    {01570, Word48("DATALE"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000007142ULL)},
    {01574, Word48("DATALI"), Word48(06500000000000000ULL), Word48(04000000000002564ULL), Word48(06600715400007170ULL)},
    {01600, Word48("DATAWO"), Word48(06500000000000000ULL), Word48(04000000000001774ULL), Word48(06601045600010463ULL)},
    {01604, Word48("DEST"), Word48(06500000000000000ULL), Word48(04000000000002430ULL), Word48(06600000000010527ULL)},
    {01610, Word48("DESTPA"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000010535ULL)},
    {01614, Word48("DESTRE"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000010257ULL)},
    {01620, Word48("DESTWO"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000010052ULL)},
    {01624, Word48("ERASE"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000003277ULL)},
    {01630, Word48("ERRFUN"), Word48(06500000000000000ULL), Word48(00100000000000000ULL), Word48(06600000000003051ULL)},
    {01634, Word48("FALSE"), Word48(06500000000000000ULL), Word48(04000000000002620ULL), Word48(06400000000000000ULL)},
    {01640, Word48("FNPART"), Word48(06500000000000000ULL), Word48(04000000000002064ULL), Word48(06601057700010604ULL)},
    {01644, Word48("FNPROP"), Word48(06500000000000000ULL), Word48(04000000000001714ULL), Word48(06601056700010574ULL)},
    {01650, Word48("FNTOLI"), Word48(06500000000000000ULL), Word48(04000000000002550ULL), Word48(06600000000010725ULL)},
    {01654, Word48("FORALL"), Word48(06500000000000000ULL), Word48(06600000000000000ULL), Word48(06600000000010737ULL)},
    {01660, Word48("FRONT"), Word48(06500000000000000ULL), Word48(04000000000002464ULL), Word48(06600705700007064ULL)},
    {01664, Word48("FROZVA"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06601060700010614ULL)},
    {01670, Word48("GENOUT"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06601212000011027ULL)},
    {01674, Word48("HD"), Word48(06500000000000000ULL), Word48(04000000000002510ULL), Word48(06600334300003350ULL)},
    {01700, Word48("IDENTF"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000003235ULL)},
    {01704, Word48("IDENTP"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000010161ULL)},
    {01710, Word48("INCHAR"), Word48(06500000000000000ULL), Word48(04000000000002060ULL), Word48(06600000000011053ULL)},
    {01714, Word48("INIT"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000011506ULL)},
    {01720, Word48("INITC"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000011746ULL)},
    {01724, Word48("INTOF"), Word48(06500000000000000ULL), Word48(04000000000002044ULL), Word48(06600000000003442ULL)},
    {01730, Word48("ISCOMP"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000003310ULL)},
    {01734, Word48("ISFUNC"), Word48(06500000000000000ULL), Word48(04000000000001754ULL), Word48(06600000000006600ULL)},
    {01740, Word48("ISINTE"), Word48(06500000000000000ULL), Word48(04000000000001760ULL), Word48(06600000000003403ULL)},
    {01744, Word48("ISLINK"), Word48(06500000000000000ULL), Word48(04000000000002364ULL), Word48(06600000000010350ULL)},
    {01750, Word48("ISLIST"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000003325ULL)},
    {01754, Word48("ISREAL"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006605ULL)},
    {01760, Word48("ISWORD"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006611ULL)},
    {01764, Word48("ITEMRE"), Word48(06500000000000000ULL), Word48(04000000000002170ULL), Word48(06600000000006132ULL)},
    {01770, Word48("JUMPOU"), Word48(06500000000000000ULL), Word48(04000000000002344ULL), Word48(06600000000012022ULL)},
    {01774, Word48("LOGAND"), Word48(06500000000000000ULL), Word48(04000000000002074ULL), Word48(06600000000012036ULL)},
    {02000, Word48("LOGNOT"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000012063ULL)},
    {02004, Word48("LOGOR"), Word48(06500000000000000ULL), Word48(04000000000002730ULL), Word48(06600000000012040ULL)},
    {02010, Word48("LOGSHI"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000012071ULL)},
    {02014, Word48("MACRES"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006410ULL)},
    {02020, Word48("MAPLIS"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06601212000012125ULL)},
    {02024, Word48("MEANIN"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06601013700010144ULL)},
    {02030, Word48("NEWANY"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000012164ULL)},
    {02034, Word48("NEWARR"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06641223600000000ULL)},
    {02040, Word48("NEXTCH"), Word48(06500000000000000ULL), Word48(00100000000000000ULL), Word48(06440000000002044ULL)},
    {02044, Word48("NIL"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06440000000002044ULL)},
    {02050, Word48("NL"), Word48(06500000000000000ULL), Word48(04000000000002714ULL), Word48(06600000000007601ULL)},
    {02054, Word48("NOT"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000003371ULL)},
    {02060, Word48("NULL"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000003407ULL)},
    {02064, Word48("PARTAP"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000012246ULL)},
    {02070, Word48("POPMES"), Word48(06500000000000000ULL), Word48(04000000000002154ULL), Word48(06600000000012304ULL)},
    {02074, Word48("POPVAL"), Word48(06500000000000000ULL), Word48(04000000000002204ULL), Word48(06600000000001032ULL)},
    {02100, Word48("PR"), Word48(06500000000000000ULL), Word48(04000000000002214ULL), Word48(06600000000007667ULL)},
    {02104, Word48("PRINT"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000012630ULL)},
    {02110, Word48("PRREAL"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000012635ULL)},
    {02114, Word48("PROGLI"), Word48(06500000000000000ULL), Word48(00100000000000000ULL), Word48(07200000000006130ULL)},
    {02120, Word48("PRSTRI"), Word48(06500000000000000ULL), Word48(04000000000002570ULL), Word48(06600000000007773ULL)},
    {02124, Word48("REALOF"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000003316ULL)},
    {02130, Word48("RECORD"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000011102ULL)},
    {02134, Word48("SAMEDA"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000007367ULL)},
    {02140, Word48("SETPOP"), Word48(06500000000000000ULL), Word48(04000000000002264ULL), Word48(06600000000013362ULL)},
    {02144, Word48("SIGN"), Word48(06500000000000000ULL), Word48(04000000000002230ULL), Word48(06600000000010365ULL)},
    {02150, Word48("SP"), Word48(06500000000000000ULL), Word48(04000000000002424ULL), Word48(06600000000007603ULL)},
    {02154, Word48("STACKL"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000013451ULL)},
    {02160, Word48("STRIPF"), Word48(06500000000000000ULL), Word48(04000000000002330ULL), Word48(06600000000011553ULL)},
    {02164, Word48("SUBSCR"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06601151700011524ULL)},
    {02170, Word48("SUBSCC"), Word48(06500000000000000ULL), Word48(04000000000002600ULL), Word48(06601175000011755ULL)},
    {02174, Word48("TERMIN"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06540000000000000ULL)},
    {02200, Word48("TL"), Word48(06500000000000000ULL), Word48(04000000000002320ULL), Word48(06600335200003357ULL)},
    {02204, Word48("TRUE"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06400000000000001ULL)},
    {02210, Word48("UNDEF"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06500000000000000ULL)},
    {02214, Word48("UPDATE"), Word48(06500000000000000ULL), Word48(04000000000002660ULL), Word48(06601055700010564ULL)},
    {02220, Word48("("), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000005ULL)},
    {02224, Word48(")"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000006ULL)},
    {02230, Word48("(%"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02234, Word48("%)"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02240, Word48("."), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000004ULL)},
    {02244, Word48(","), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000002ULL)},
    {02250, Word48(";"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000003ULL)},
    {02254, Word48("["), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000007ULL)},
    {02260, Word48("]"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000010ULL)},
    {02264, Word48("[%"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02270, Word48("%]"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02274, Word48(":"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02300, Word48("->"), Word48(06500000000000000ULL), Word48(06400000000002670ULL), Word48(06500000000000000ULL)},
    {02304, Word48("=>"), Word48(06500000000000000ULL), Word48(06400000000002450ULL), Word48(06600000000013454ULL)},
    {02310, Word48("&"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02314, Word48("AND"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02320, Word48("CANCEL"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000101ULL)},
    {02324, Word48("CLOSE"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02330, Word48("ELSE"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02334, Word48("ELSEIF"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02340, Word48("END"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000107ULL)},
    {02344, Word48("ENDSEC"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000111ULL)},
    {02350, Word48("EXIT"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02354, Word48("FUNCTI"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000104ULL)},
    {02360, Word48("GOON"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02364, Word48("GOTO"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02370, Word48("IF"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02374, Word48("LAMBDA"), Word48(06500000000000000ULL), Word48(06400000000002470ULL), Word48(00000000000000103ULL)},
    {02400, Word48("LOOPIF"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02404, Word48("MACRO"), Word48(06500000000000000ULL), Word48(06400000000002640ULL), Word48(00000000000000106ULL)},
    {02410, Word48("NONMAC"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02414, Word48("NONOP"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02420, Word48("OPERAT"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000105ULL)},
    {02424, Word48("OR"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02430, Word48("RETURN"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02434, Word48("SECTIO"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000110ULL)},
    {02440, Word48("SWITCH"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02444, Word48("THEN"), Word48(06500000000000000ULL), Word48(06400000000002734ULL), Word48(06500000000000000ULL)},
    {02450, Word48("VARS"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000102ULL)},
    {02454, Word48("\""), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(00000000000000011ULL)},
    {02460, Word48("COMMEN"), Word48(06500000000000000ULL), Word48(06400000000000000ULL), Word48(06500000000000000ULL)},
    {02464, Word48("APPDAT"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600715400007165ULL)},
    {02470, Word48("APPLIS"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06601212000012145ULL)},
    {02474, Word48("APPLY"), Word48(06500000000000000ULL), Word48(04000000000002504ULL), Word48(06600274000002745ULL)},
    {02500, Word48("ARCTAN"), Word48(06500000000000000ULL), Word48(04000000000002724ULL), Word48(06600000000006765ULL)},
    {02504, Word48("CARRYO"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600715400010235ULL)},
    {02510, Word48("COPYLI"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000007131ULL)},
    {02514, Word48("COREUS"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000010402ULL)},
    {02520, Word48("COS"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006763ULL)},
    {02524, Word48("EQUAL"), Word48(06500000000000000ULL), Word48(04000000000002554ULL), Word48(06601212000013526ULL)},
    {02530, Word48("EXP"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006771ULL)},
    {02534, Word48("FNCOMP"), Word48(06500000000000000ULL), Word48(04400000000000000ULL), Word48(06600000000010421ULL)},
    {02540, Word48("LENGTH"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000011432ULL)},
    {02544, Word48("LIBRAR"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000014651ULL)},
    {02550, Word48("LISTRE"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000015314ULL)},
    {02554, Word48("LOG"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006767ULL)},
    {02560, Word48("NUMBER"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000015354ULL)},
    {02564, Word48("POPTIM"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006476ULL)},
    {02570, Word48("PRBIN"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000007620ULL)},
    {02574, Word48("PROCT"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000007622ULL)},
    {02600, Word48("REV"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600715400007337ULL)},
    {02604, Word48("SIN"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006761ULL)},
    {02610, Word48("SQRT"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006757ULL)},
    {02614, Word48("TAN"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006773ULL)},
    {02620, Word48("VALOF"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600644400006451ULL)},
    {02624, Word48("SYNTAX"), Word48(06500000000000000ULL), Word48(03000000000000000ULL), Word48(06500000000000000ULL)},
    {02630, Word48("REAL"), Word48(06500000000000000ULL), Word48(03000000000000000ULL), Word48(06500000000000000ULL)},
    {02634, Word48("INTEGE"), Word48(06500000000000000ULL), Word48(03000000000000000ULL), Word48(06500000000000000ULL)},
    {02640, Word48("WORD"), Word48(06500000000000000ULL), Word48(03000000000000000ULL), Word48(06500000000000000ULL)},
    {02644, Word48("STRIP"), Word48(06500000000000000ULL), Word48(03000000000000000ULL), Word48(06500000000000000ULL)},
    {02650, Word48("CSTRIP"), Word48(06500000000000000ULL), Word48(03000000000000000ULL), Word48(06500000000000000ULL)},
    {02654, Word48("PAIR"), Word48(06500000000000000ULL), Word48(03000000000000000ULL), Word48(06500000000000000ULL)},
    {02660, Word48("REF"), Word48(06500000000000000ULL), Word48(03000000000000000ULL), Word48(06500000000000000ULL)},
    {02664, Word48("ClYv"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000015377ULL)},
    {02670, Word48("/="), Word48(06500000000000000ULL), Word48(05600000000000000ULL), Word48(06600000000010226ULL)},
    {02674, Word48("REFOF"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000006457ULL)},
    {02700, Word48("NTERM"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06500000000000000ULL)},
    {02704, Word48("NUMERR"), Word48(06500000000000000ULL), Word48(00100000000000000ULL), Word48(06400000000000000ULL)},
    {02710, Word48("INDEC"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06400000000000000ULL)},
    {02714, Word48("POPDAT"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000015667ULL)},
    {02720, Word48("CODIPC"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000015754ULL)},
    {02724, Word48("CODPIC"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000015755ULL)},
    {02730, Word48("CODIPS"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000015712ULL)},
    {02734, Word48("CODPIS"), Word48(06500000000000000ULL), Word48(04000000000000000ULL), Word48(06600000000015713ULL)},
}};

constexpr bool keyword_descriptors_are_valid()
{
    constexpr Word48 keyword_class(06500000000000000ULL);
    for (std::size_t index = 0; index != keyword_descriptors.size(); ++index) {
        const KeywordDescriptor &descriptor = keyword_descriptors[index];
        if (descriptor.record != 01400 + index * 4
            || descriptor.class_word != keyword_class) {
            return false;
        }
        for (std::size_t previous = 0; previous != index; ++previous) {
            if (keyword_descriptors[previous].name == descriptor.name) {
                return false;
            }
        }
    }
    return true;
}

static_assert(keyword_descriptors_are_valid(),
              "native keyword descriptors must mirror 01400..02737");

const KeywordDescriptor *find_keyword_descriptor(Word48 name)
{
    const auto found = std::find_if(
        keyword_descriptors.begin(), keyword_descriptors.end(),
        [name](const KeywordDescriptor &descriptor) {
            return descriptor.name == name;
        });
    return found == keyword_descriptors.end() ? nullptr : &*found;
}


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

std::uint16_t Machine::find_static_keyword(Word48 identifier) const
{
    const KeywordDescriptor *descriptor =
        find_keyword_descriptor(identifier);
    return descriptor == nullptr ? 0 : descriptor->record;
}

std::uint16_t Machine::p16672()
{
    // 16672 begins conversion after the scanner has accepted a decimal point.
    const std::uint16_t saved = address_add(registers_[017], -7);
    accumulator_ = memory_[saved];
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074475)].raw());
    remainder_ = old_accumulator;
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
    Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074475)].raw());
    remainder_ = old_accumulator;
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
    shift_accumulator(-3);
    accumulator_ = cyclic_add(accumulator_, memory_[scratch]);
    remainder_ = Word48();
    accumulator_ = cyclic_add(accumulator_, memory_[scratch]);
    remainder_ = Word48();
    accumulator_ = cyclic_add(accumulator_, memory_[registers_[003]]);
    remainder_ = Word48();
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
