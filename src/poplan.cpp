#include "poplan/machine.hpp"
#include "poplan/native.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <istream>
#include <iterator>
#include <ostream>
#include <stdexcept>

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

// Image-independent POP execution foundation. This code does not call Machine.
namespace poplan {

std::vector<NativeWord> native_bootstrap_dictionary()
{
    std::vector<NativeWord> words;
    words.reserve(keyword_descriptors.size());
    for (const auto &descriptor : keyword_descriptors) {
        words.push_back({descriptor.record, descriptor.name,
                         descriptor.class_word, descriptor.property_word, {}});
    }
    return words;
}

NativeAllocator::NativeAllocator(std::uint16_t first, std::uint16_t limit)
    : first_(first), limit_(limit)
{
    if (first == 0 || first > limit || limit > 077777) {
        throw std::invalid_argument("native allocator: invalid logical arena");
    }
    // 20660 constructs one common-list extent from the two allocation bounds.
    if (first != limit) {
        state_.common.push_back({first, static_cast<std::uint16_t>(limit - first)});
    }
}

NativeAllocation NativeAllocator::try_allocate(std::uint32_t words)
{
    if (words > 077777) {
        throw std::invalid_argument("native allocator: request exceeds 15 bits");
    }
    // 05430 returns immediately for zero; do not enter the initializing loop.
    if (words == 0) {
        return {NativeAllocation::Status::zero_size, {}};
    }
    const auto size = static_cast<std::uint16_t>(words);
    NativeBlock result;
    if (size < 021 && !state_.small[size].empty()) {
        // 05451..05454: take the head of the exact-size chain.
        result = state_.small[size].front();
        state_.live.push_back(result);
        state_.small[size].erase(state_.small[size].begin());
    } else {
        // 05463..05500: preserve list order, take the first sufficient extent,
        // and split from its HIGH end. No best-fit search or coalescing occurs.
        auto found = std::find_if(state_.common.begin(), state_.common.end(),
            [size](const NativeBlock &block) { return block.words >= size; });
        if (found == state_.common.end()) {
            // This is 05462's failed attempt, not a final out-of-memory error.
            return {NativeAllocation::Status::collection_required, {}};
        }
        const auto remainder = static_cast<std::uint16_t>(found->words - size);
        result = {static_cast<std::uint16_t>(found->address + remainder), size};
        state_.live.push_back(result);
        if (remainder == 0) {
            state_.common.erase(found);
        } else {
            found->words = remainder;
        }
    }
    // Object storage must initialize each returned word to the unbound value
    // (05455..05461). This metadata allocator does not own object payloads.
    return {NativeAllocation::Status::allocated, result};
}

void NativeAllocator::release(NativeBlock block)
{
    // 03506's zero-size path leaves all lists unchanged.
    if (block.words == 0) return;
    const auto found = std::find_if(state_.live.begin(), state_.live.end(),
        [block](const NativeBlock &live) {
            return live.address == block.address && live.words == block.words;
        });
    if (found == state_.live.end()) {
        // Native ownership violations are implementation errors, not POP errors.
        throw std::logic_error("native allocator: release is not a live allocation");
    }
    // 03506/03516 push at the head. Small sizes 1..020 have distinct chains;
    // size 021 and above prepend the common list, without merging neighbors.
    auto &chain = block.words < 021 ? state_.small[block.words] : state_.common;
    chain.insert(chain.begin(), block);
    state_.live.erase(found);
}

NativeCoreUsage NativeAllocator::core_usage() const
{
    // 10402..10414 deliberately omits size-specific lists. Thus a release of
    // a small object does not reduce COREUSED even though its space is reusable.
    std::uint32_t common_free = 0;
    for (const auto &block : state_.common) common_free += block.words;
    return {static_cast<std::uint16_t>(limit_ - first_ - common_free),
            static_cast<std::uint16_t>(common_free)};
}

void NativeAllocator::collect(const std::vector<std::uint16_t> &marked)
{
    NativeAllocatorSnapshot next;
    for (const auto address : marked) {
        const auto found = std::find_if(state_.live.begin(), state_.live.end(),
            [address](NativeBlock block) { return block.address == address; });
        if (found == state_.live.end())
            throw std::logic_error("native collector: root block is not allocated");
        if (std::none_of(next.live.begin(), next.live.end(),
                [address](NativeBlock block) { return block.address == address; }))
            next.live.push_back(*found);
    }
    std::sort(next.live.begin(), next.live.end(), [](NativeBlock a, NativeBlock b) {
        return a.address < b.address;
    });
    // 05640..05701 scans the mark bitmap in ascending address order. A free
    // run is linked only when a marked word ends it (05667..05672). The scan
    // stops before the bitmap word containing the upper bound (05654..05655).
    const unsigned scan_end = limit_ / 48 * 48;
    unsigned cursor = first_;
    for (const auto block : next.live) {
        if (cursor < block.address) {
            NativeBlock gap{static_cast<std::uint16_t>(cursor),
                            static_cast<std::uint16_t>(block.address - cursor)};
            (block.address < scan_end ? next.common : next.unavailable).push_back(gap);
        }
        cursor = block.address + block.words;
    }
    if (cursor < limit_)
        next.unavailable.push_back({static_cast<std::uint16_t>(cursor),
                                    static_cast<std::uint16_t>(limit_ - cursor)});
    // Collection rebuilds all free storage in the common list, not size bins.
    state_ = std::move(next);
}

std::uint8_t NativeDictionary::hash(Word48 name)
{
    // 01107..01121: successive masked left shifts followed by right shifts
    // 40,39,...,35. Later samples include high bits of the following byte;
    // replacing this with a simple weighted sum of bytes changes collisions.
    std::uint64_t sum = 0;
    for (unsigned index = 0; index < 6; ++index) {
        const auto sample = ((name.raw() << (8 * index)) & Word48::mask)
            >> (40 - index);
        if (sample == 0) break;
        sum += sample;
    }
    return static_cast<std::uint8_t>(sum & 0377);
}

const NativeWord &NativeDictionary::word(std::uint32_t handle) const
{
    const auto found = std::find_if(words_.begin(), words_.end(),
        [handle](const NativeWord &word) { return word.handle == handle; });
    if (found == words_.end()) {
        throw std::logic_error("native dictionary: unknown word handle");
    }
    return *found;
}

NativeWord &NativeDictionary::mutable_word(std::uint32_t handle)
{
    // The public view is read-only so names and collision links cannot be
    // accidentally mutated without the corresponding dictionary operation.
    return const_cast<NativeWord &>(static_cast<const NativeDictionary &>(*this).word(handle));
}

NativeDictionary::NativeDictionary(NativeAllocator &allocator)
    : allocator_(allocator), words_(native_bootstrap_dictionary())
{
    const auto check_arena = [](const auto &blocks) {
        for (const auto &block : blocks) {
            if (block.address < 02740 && block.address + block.words > 01400) {
                throw std::invalid_argument("native dictionary: arena overlaps resident records");
            }
        }
    };
    check_arena(allocator.snapshot().common);
    check_arena(allocator.snapshot().live);
    for (const auto &chain : allocator.snapshot().small) check_arena(chain);
    // Reconstruct heads from the resident data links, preserving the original
    // collision order. Do not reorder records by name or descriptor address.
    std::vector<std::uint32_t> linked;
    for (const auto &record : words_) {
        const auto next = record.properties.address();
        if (next != 0) {
            if (hash(record.packed_name) != hash(word(next).packed_name)) {
                throw std::logic_error("native dictionary: inconsistent resident collision link");
            }
            linked.push_back(next);
        }
    }
    for (const auto &record : words_) {
        if (std::find(linked.begin(), linked.end(), record.handle) == linked.end()) {
            auto &head = heads_[hash(record.packed_name)];
            if (head != 0) {
                throw std::logic_error("native dictionary: multiple resident bucket heads");
            }
            head = record.handle;
        }
    }
    for (const auto &record : words_) {
        if (lookup(record.packed_name) != record.handle) {
            throw std::logic_error("native dictionary: unreachable resident word");
        }
    }
}

std::uint32_t NativeDictionary::lookup(Word48 name) const
{
    auto handle = heads_[hash(name)];
    std::size_t remaining = words_.size();
    while (handle != 0) {
        if (remaining-- == 0) {
            throw std::logic_error("native dictionary: cyclic collision chain");
        }
        const auto &record = word(handle);
        if (record.packed_name == name) return handle;
        handle = record.properties.address();
    }
    return 0;
}

NativeInternResult NativeDictionary::intern(Word48 name)
{
    const auto existing = lookup(name);
    if (existing != 0) return {NativeInternResult::Status::found, existing};
    // Reserve host metadata before changing logical allocation state.
    words_.reserve(words_.size() + 1);
    const auto allocation = allocator_.try_allocate(4); // 01137 / 01150
    if (allocation.status != NativeAllocation::Status::allocated) {
        return {NativeInternResult::Status::collection_required, 0};
    }
    const auto handle = allocation.block.address;
    // 01154..01157 initializes name, property word and two unbound fields.
    words_.push_back({handle, name, Word48(06500000000000000ULL),
                      Word48(03000000000000000ULL), {}});
    auto &head = heads_[hash(name)];
    if (head == 0) {
        head = handle; // 01140: previously empty hash bucket
    } else {
        auto tail = head;
        while (word(tail).properties.address() != 0) {
            tail = word(tail).properties.address();
        }
        auto &property = mutable_word(tail).properties;
        property = Word48((property.raw() & ~std::uint64_t{077777}) | handle);
    }
    return {NativeInternResult::Status::created, handle};
}

void NativeDictionary::bind(std::uint32_t handle, NativeValue value)
{
    mutable_word(handle).binding = value;
}

NativeEmissionCost native_emission_cost(NativeEmission emission)
{
    // 05052..05076 selects one output word per operation, except the label
    // at 05070. 17774/20002 counts labels without advancing the sizing cursor.
    // 04447 and 17762 allocate two-word intermediate nodes independently.
    switch (emission) {
    case NativeEmission::evaluate: return {05052, 2, 1};
    case NativeEmission::literal: return {05053, 2, 1};
    case NativeEmission::branch_zero: return {05054, 2, 1};
    case NativeEmission::branch_nonzero: return {05055, 2, 1};
    case NativeEmission::jump: return {05056, 2, 1};
    case NativeEmission::store_binding: return {05057, 2, 1};
    case NativeEmission::return_value: return {05060, 2, 1};
    case NativeEmission::evaluate_stack: return {05061, 2, 1};
    case NativeEmission::load_capture: return {05062, 2, 1};
    case NativeEmission::embedded_value: return {05063, 2, 1};
    case NativeEmission::call_binding: return {05064, 2, 1};
    case NativeEmission::update_binding: return {05065, 2, 1};
    case NativeEmission::transfer_arguments: return {05066, 2, 1};
    case NativeEmission::rebuild_arguments: return {05067, 2, 1};
    case NativeEmission::label: return {05070, 2, 0};
    case NativeEmission::continuation: return {05071, 2, 1};
    case NativeEmission::load_binding: return {05072, 2, 1};
    case NativeEmission::capture_function: return {05073, 2, 1};
    case NativeEmission::jump_capture: return {05076, 2, 1};
    }
    throw std::logic_error("native compiler: unknown emission kind");
}

NativeFrameCost native_binding_frame_cost(const std::vector<NativeBindingSlot> &slots)
{
    if (slots.size() > 077777 / 2)
        throw std::overflow_error("native binding frame size");
    // 03206 saves previous function and return link; 03217..03222 saves two
    // words per binding in reverse order. Arguments/results borrow three words.
    NativeFrameCost cost{2, 2, 2};
    for (auto slot = slots.rbegin(); slot != slots.rend(); ++slot) {
        cost.retained += 2;
        cost.entry_peak = std::max(cost.entry_peak, cost.retained + (slot->argument ? 3u : 0u));
    }
    auto remaining = cost.retained;
    cost.exit_peak = remaining;
    for (const auto slot : slots) {
        cost.exit_peak = std::max(cost.exit_peak, remaining + (slot.result ? 3u : 0u));
        remaining -= 2;
    }
    return cost;
}

NativeFrameCost native_argument_frame_cost(std::uint16_t arguments)
{
    if (arguments > 077777) throw std::overflow_error("native argument count");
    if (!arguments) return {};
    // 20111 reserves one and 20113 adds n. Restoration pops four words,
    // including one reserved slot: net n, not n+1. Rebuilding at 20124
    // temporarily saves four words above the retained argument area.
    return {arguments, arguments == 1 ? 1u : unsigned(arguments) + 4,
            unsigned(arguments) + 4};
}

NativeAllocation NativeRuntime::allocate_storage(std::uint32_t words)
{
    auto allocation = allocator_.try_allocate(words);
    if (allocation.status == NativeAllocation::Status::collection_required) {
        collect();
        allocation = allocator_.try_allocate(words);
        if (allocation.status == NativeAllocation::Status::collection_required)
            allocation.status = NativeAllocation::Status::exhausted;
    }
    return allocation;
}

NativeAllocation NativeRuntime::intern_literal(Word48 value)
{
    // 06134/01167 uses full-word equality and two-word records. The collector
    // roots every entry of this table at 05554..05564, like interned names.
    auto [entry, inserted] = literals_.emplace(value.raw(), NativeBlock{});
    if (!inserted) return {NativeAllocation::Status::allocated, entry->second};
    try {
        const auto allocation = allocate_storage(2);
        if (allocation.status != NativeAllocation::Status::allocated) {
            literals_.erase(entry);
            return allocation;
        }
        entry->second = allocation.block;
        return allocation;
    } catch (...) {
        literals_.erase(entry);
        throw;
    }
}

NativeCompilationResult NativeRuntime::compile_storage(NativeCompilation compilation)
{
    if (!compilation.literals.empty() && compilation.literals.size() != compilation.emissions.size())
        throw std::logic_error("native compiler literal/operation count mismatch");
    std::uint32_t words = 1; // 05143 terminator, sizing seed 05175
    for (const auto emission : compilation.emissions) {
        words += native_emission_cost(emission).code_words;
        if (words > 077777) return {NativeAllocation::Status::exhausted, 0};
    }
    if (words > 077777 || compilation.bindings.size() >= 077777 || compilation.captures.size() >= 077777)
        return {NativeAllocation::Status::exhausted, 0};
    for (const auto value : compilation.bindings) validate(value);
    for (const auto value : compilation.captures) validate(value);
    const auto id = compilations_.size();
    compilations_.push_back(CompilationRecord{std::move(compilation), {}, {}});
    auto &record = *compilations_.back();
    // Publish pending roots before any allocation that can collect. Reserving
    // metadata first keeps host allocation failure from leaking a logical block.
    const auto allocate = [&](std::uint32_t count, NativeBlock &block) {
        const auto allocation = allocate_storage(count);
        block = allocation.block;
        return allocation.status == NativeAllocation::Status::allocated
            || allocation.status == NativeAllocation::Status::zero_size;
    };
    const auto discard = [&] {
        // Failed construction keeps no root. Its partial extents become garbage
        // for collection, rather than being silently returned to another bucket.
        compilations_[id].reset();
        return NativeCompilationResult{NativeAllocation::Status::exhausted, id};
    };
    try {
        record.temporary.reserve(record.source.emissions.size() + record.source.bindings.size()
                                 + record.source.captures.size());
        const auto count = record.source.emissions.size() + record.source.bindings.size()
            + record.source.captures.size();
        for (std::size_t i = 0; i < count; ++i) {
            if (i < record.source.literals.size() && record.source.literals[i]
                && intern_literal(*record.source.literals[i]).status != NativeAllocation::Status::allocated)
                return discard();
            NativeBlock block;
            if (!allocate(2, block)) return discard();
            record.temporary.push_back(block);
        }
        // 05021 descriptor, then 05160/11464 copies the two environment chains.
        if (!allocate(5, record.storage.descriptor)
            || !allocate(record.source.bindings.empty() ? 0 : record.source.bindings.size() + 1,
                         record.storage.bindings)
            || !allocate(record.source.captures.empty() ? 0 : record.source.captures.size() + 1,
                         record.storage.captures)) return discard();
        // 05033/03531 frees the capture chain before allocating executable space.
        const auto capture_begin = record.source.emissions.size() + record.source.bindings.size();
        for (std::size_t i = capture_begin; i < record.temporary.size(); ++i) {
            allocator_.release(record.temporary[i]);
            record.temporary[i] = {};
        }
        if (!allocate(words, record.storage.code)) return discard();
        // 05147/03531 releases the operation chain in forward list order.
        for (std::size_t i = 0; i < record.source.emissions.size(); ++i) {
            allocator_.release(record.temporary[i]);
            record.temporary[i] = {};
        }
        // The binding chain is retained by the compiler's 17560 table; unlike
        // the capture chain it is not freed by this finalizer.
        return {NativeAllocation::Status::allocated, id};
    } catch (...) {
        compilations_[id].reset();
        throw;
    }
}

const NativeCompiledStorage &NativeRuntime::compiled_storage(std::size_t id) const
{
    if (!compilations_.at(id)) throw std::logic_error("native compiled storage was released");
    return compilations_[id]->storage;
}

void NativeRuntime::finish_compilation_context(std::size_t id)
{
    compiled_storage(id);
    // Restoring the enclosing compiler table removes roots, not free-list
    // entries. The finalized environment arrays remain owned by the program.
    compilations_[id]->temporary.clear();
}

void NativeRuntime::release_compiled_storage(std::size_t id)
{
    compiled_storage(id);
    compilations_[id].reset();
}

bool NativeRuntime::enter_frame(NativeFrameCost cost, std::vector<NativeValue> roots)
{
    if (cost.entry_peak < cost.retained || cost.exit_peak < cost.retained)
        throw std::logic_error("native frame peak below retained size");
    for (const auto root : roots) validate(root);
    // Startup hardware area 066001..066777; 067000 is the collision watchpoint.
    constexpr std::uint32_t capacity = 067000 - 066001;
    const auto peak = cost.entry_peak;
    if (peak > capacity || activation_words_ > capacity - peak) return false;
    accounting_frames_.push_back({cost, std::move(roots)});
    activation_peak_ = std::max(activation_peak_, activation_words_ + peak);
    activation_words_ += cost.retained;
    return true;
}

bool NativeRuntime::leave_frame()
{
    if (accounting_frames_.empty()) throw std::logic_error("native frame stack underflow");
    const auto cost = accounting_frames_.back().cost;
    const auto base = activation_words_ - cost.retained;
    constexpr std::uint32_t capacity = 067000 - 066001;
    if (cost.exit_peak > capacity || base > capacity - cost.exit_peak) return false;
    activation_peak_ = std::max(activation_peak_, base + cost.exit_peak);
    activation_words_ = base;
    accounting_frames_.pop_back();
    return true;
}

void NativeRuntime::unwind_frames(std::size_t depth)
{
    if (depth > accounting_frames_.size()) throw std::logic_error("native unwind depth");
    while (accounting_frames_.size() > depth) {
        activation_words_ -= accounting_frames_.back().cost.retained;
        accounting_frames_.pop_back();
    }
}

NativeRuntime::NativeRuntime() : NativeRuntime(033064, 066000)
{
    // Startup's sole 05447 request is two words at 065776. Its contents belong
    // to the reference compiler's startup state; reserve the logical extent
    // without inventing a native callable object or instruction payload.
    const auto block = allocator_.try_allocate(2);
    if (block.status != NativeAllocation::Status::allocated)
        throw std::logic_error("native startup reservation failed");
    reservations_.push_back(block.block.address);
}

NativeRuntime::NativeRuntime(std::uint16_t first, std::uint16_t limit)
    : allocator_(first, limit), dictionary_(allocator_)
{}

void NativeRuntime::validate(NativeValue value) const
{
    switch (value.kind) {
    case NativeValue::Kind::unbound: case NativeValue::Kind::number: return;
    case NativeValue::Kind::word: dictionary_.word(value.handle); return;
    default: object(value); return;
    }
}

const NativeObject &NativeRuntime::object(NativeValue value) const
{
    const auto found = objects_.find(value.handle);
    if (found == objects_.end() || found->second.kind != value.kind)
        throw std::logic_error("native storage: invalid or reclaimed object handle");
    return found->second;
}

void NativeRuntime::store(NativeValue target, std::size_t field, NativeValue value)
{
    object(target);
    validate(value);
    objects_.at(target.handle).fields.at(field) = value;
}

NativeObjectAllocation NativeRuntime::allocate_object(NativeValue::Kind kind,
                                                      std::vector<NativeValue> fields)
{
    for (const auto value : fields) validate(value);
    if (next_handle_ == std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("native object handle overflow");
    // Allocate host metadata before changing logical storage. Pending fields
    // are roots during a collection triggered by this very allocation.
    const auto handle = next_handle_;
    auto node = objects_.emplace(handle, NativeObject{kind, {}, std::move(fields)}).first;
    try {
        auto allocation = allocator_.try_allocate(node->second.fields.size());
        if (allocation.status == NativeAllocation::Status::collection_required) {
            collect(node->second.fields);
            allocation = allocator_.try_allocate(node->second.fields.size());
        }
        if (allocation.status != NativeAllocation::Status::allocated) {
            objects_.erase(handle);
            return {NativeAllocation::Status::exhausted, {}};
        }
        node->second.block = allocation.block;
        ++next_handle_;
        return {NativeAllocation::Status::allocated, {kind, {}, handle}};
    } catch (...) {
        objects_.erase(handle);
        throw;
    }
}

NativeObjectAllocation NativeRuntime::make_reference(NativeValue value)
{
    return allocate_object(NativeValue::Kind::reference, {value}); // 10277: one word
}

NativeObjectAllocation NativeRuntime::make_pair(NativeValue first, NativeValue second)
{
    return allocate_object(NativeValue::Kind::pair, {first, second}); // 05215: two words
}

NativeInternResult NativeRuntime::intern(Word48 name)
{
    auto result = dictionary_.intern(name);
    if (result.status == NativeInternResult::Status::collection_required) {
        collect();
        result = dictionary_.intern(name);
        if (result.status == NativeInternResult::Status::collection_required)
            result.status = NativeInternResult::Status::exhausted;
    }
    return result;
}

void NativeRuntime::bind(std::uint32_t word, NativeValue value)
{
    validate(value);
    dictionary_.bind(word, value);
}

std::size_t NativeRuntime::retain_program(NativeProgram program)
{
    for (const auto root : program.roots) validate(root);
    const auto id = programs_.size();
    programs_.push_back(ProgramRecord{std::move(program), std::nullopt});
    auto &record = *programs_.back();
    if (!record.program.error.empty() || record.program.operations.empty()) return id;
    try {
        NativeCompilation compilation;
        for (const auto &operation : record.program.operations) {
            compilation.literals.push_back(operation.kind == NativeOperation::Kind::constant
                ? std::optional<Word48>(operation.constant) : std::nullopt);
            switch (operation.kind) {
            case NativeOperation::Kind::constant:
                compilation.emissions.push_back(NativeEmission::literal); break;
            case NativeOperation::Kind::binary: case NativeOperation::Kind::print:
                compilation.emissions.push_back(NativeEmission::call_binding); break;
            default: throw std::logic_error("native program: unknown operation kind");
            }
        }
        const auto allocation = compile_storage(std::move(compilation));
        if (allocation.status == NativeAllocation::Status::allocated) {
            record.storage = allocation.id;
            finish_compilation_context(allocation.id);
        }
        else record.program.error = "native compiler storage exhausted";
    } catch (...) {
        programs_[id].reset();
        throw;
    }
    return id;
}

void NativeRuntime::release_program(std::size_t program)
{
    if (!programs_.at(program)) throw std::logic_error("native program already released");
    if (programs_[program]->storage) release_compiled_storage(*programs_[program]->storage);
    programs_[program].reset();
}

void NativeRuntime::collect(const std::vector<NativeValue> &temporary_roots)
{
    std::vector<NativeValue> pending = temporary_roots;
    pending.insert(pending.end(), values_.begin(), values_.end());
    std::vector<std::uint16_t> marked = reservations_;
    for (const auto &[bits, block] : literals_)
        if (block.words) marked.push_back(block.address);
    for (const auto &word : dictionary_.words()) {
        if (std::any_of(allocator_.snapshot().live.begin(), allocator_.snapshot().live.end(),
                [&](NativeBlock block) { return block.address == word.handle; }))
            marked.push_back(static_cast<std::uint16_t>(word.handle));
        pending.push_back(word.binding);
    }
    for (const auto &program : programs_)
        if (program) pending.insert(pending.end(), program->program.roots.begin(), program->program.roots.end());
    for (const auto &compilation : compilations_) {
        if (!compilation) continue;
        const auto &s = compilation->storage;
        for (const auto block : {s.descriptor, s.bindings, s.captures, s.code})
            if (block.words) marked.push_back(block.address);
        for (const auto block : compilation->temporary)
            if (block.words) marked.push_back(block.address);
        pending.insert(pending.end(), compilation->source.bindings.begin(), compilation->source.bindings.end());
        pending.insert(pending.end(), compilation->source.captures.begin(), compilation->source.captures.end());
    }
    for (const auto &frame : accounting_frames_)
        pending.insert(pending.end(), frame.roots.begin(), frame.roots.end());
    for (const auto &frame : frame_roots_)
        pending.insert(pending.end(), frame.begin(), frame.end());
    std::vector<std::uint32_t> visited;
    while (!pending.empty()) {
        const auto value = pending.back(); pending.pop_back();
        validate(value);
        if (value.kind == NativeValue::Kind::unbound || value.kind == NativeValue::Kind::number
            || value.kind == NativeValue::Kind::word) continue;
        if (std::find(visited.begin(), visited.end(), value.handle) != visited.end()) continue;
        visited.push_back(value.handle);
        const auto &payload = object(value);
        marked.push_back(payload.block.address);
        pending.insert(pending.end(), payload.fields.begin(), payload.fields.end());
    }
    allocator_.collect(marked);
    for (auto it = objects_.begin(); it != objects_.end();) {
        // A zero extent is the pending host-only record of allocate_object.
        if (it->second.block.words && std::find(visited.begin(), visited.end(), it->first) == visited.end())
            it = objects_.erase(it);
        else ++it;
    }
    ++collections_;
}

NativeNumberResult native_real_from_double(double value)
{
    static_assert(std::numeric_limits<double>::is_iec559
                  && std::numeric_limits<double>::radix == 2
                  && std::numeric_limits<double>::digits == 53
                  && std::numeric_limits<double>::max_exponent == 1024,
                  "native real literals require IEEE binary64 double");
    if (!std::isfinite(value)) {
        return {{}, "native real conversion requires a finite double"};
    }
    if (value == 0) return {{}, {}}; // BESM has one zero, including input -0.0.
    int exponent = 0;
    const double fraction = std::frexp(value, &exponent);
    // BESM's signed 41-bit mantissa has 40 fractional bits. ldexp is exact
    // here; this cast deliberately truncates rather than rounds to nearest.
    auto mantissa = static_cast<std::int64_t>(std::ldexp(fraction, 40));
    if (mantissa == -(std::int64_t{1} << 39)) {
        // Negative powers of two normalize as -1, not -0.5. Check AFTER
        // truncation too, since a nearby double can truncate to this boundary.
        mantissa *= 2;
        --exponent;
    }
    exponent += 64;
    if (exponent < 0) return {{}, {}};
    if (exponent > 0177) return {{}, "native real conversion: BESM exponent overflow"};
    return {Word48((static_cast<std::uint64_t>(exponent) << 41)
                   | (static_cast<std::uint64_t>(mantissa) & ((std::uint64_t{1} << 41) - 1))), {}};
}

NativeNumberResult native_parse_real_literal(std::string_view spelling)
{
    // Retain the lexical shape established at 16672..16735. Decimal formation
    // now intentionally goes through IEEE double as requested; it does not
    // replay the reference scanner's per-digit BESM arithmetic/rounding.
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    std::size_t position = 0;
    bool negative = false;
    if (position < spelling.size() && (spelling[position] == '+' || spelling[position] == '-')) {
        negative = spelling[position++] == '-';
    }
    const auto start = position;
    while (position < spelling.size() && digit(spelling[position])) ++position;
    if (position == spelling.size() || spelling[position++] != '.') {
        return {{}, "native real literal requires a decimal point"};
    }
    const auto fraction = position;
    while (position < spelling.size() && digit(spelling[position])) ++position;
    if (position == fraction) return {{}, "native real literal requires fractional digits"};
    const auto exponent_mark = position;
    if (position < spelling.size() && spelling[position] == '$') {
        ++position;
        if (position < spelling.size() && (spelling[position] == '+' || spelling[position] == '-')) ++position;
        const auto exponent_digits = position;
        while (position < spelling.size() && digit(spelling[position])) ++position;
        if (position == exponent_digits) return {{}, "native real literal requires exponent digits"};
    }
    if (position != spelling.size()) return {{}, "invalid native real literal suffix"};
    std::string decimal(spelling.substr(start));
    if (exponent_mark < spelling.size()) decimal[exponent_mark - start] = 'e';
    double value = 0;
    const auto parsed = std::from_chars(decimal.data(), decimal.data() + decimal.size(), value,
                                        std::chars_format::general);
    if (parsed.ec == std::errc::result_out_of_range) {
        return {{}, "native real literal is outside IEEE double range"};
    }
    if (parsed.ec != std::errc{} || parsed.ptr != decimal.data() + decimal.size()) {
        return {{}, "invalid native real literal"};
    }
    auto result = native_real_from_double(negative ? -value : value);
    // The full BESM format has a larger exponent range than POP real objects:
    // exponent 104 is the integer tag, and higher exponents are other objects.
    if (result.error.empty() && (result.value.raw() >> 41) >= 104) {
        return {{}, "native real literal exceeds POPLAN real range"};
    }
    return result;
}

NativeProgram native_compile_probe(const std::string &source)
{
    NativeProgram program;
    std::size_t position = 0;
    const auto space = [&] {
        while (position < source.size()
               && (source[position] == ' ' || source[position] == '\t'
                   || source[position] == '\n' || source[position] == '\r')) {
            ++position;
        }
    };
    std::string literal_error;
    const auto number = [&](Word48 &value) {
        space();
        const auto start = position;
        const auto digit = [](char c) { return c >= '0' && c <= '9'; };
        if (position < source.size() && (source[position] == '+' || source[position] == '-')) ++position;
        while (position < source.size() && digit(source[position])) ++position;
        if (position < source.size() && source[position] == '.') {
            ++position;
            while (position < source.size() && digit(source[position])) ++position;
            if (position < source.size() && source[position] == '$') {
                ++position;
                if (position < source.size() && (source[position] == '+' || source[position] == '-')) ++position;
                while (position < source.size() && digit(source[position])) ++position;
            }
            const auto real = native_parse_real_literal(std::string_view(source).substr(start, position - start));
            literal_error = real.error;
            value = real.value;
            return real.error.empty();
        }
        // Keep the existing unsigned integer token/range contract.
        if (start == position || !digit(source[start])) return false;
        std::uint64_t payload = 0;
        constexpr std::uint64_t maximum = (std::uint64_t{1} << 39) - 1;
        for (auto index = start; index < position; ++index) {
            const unsigned digit_value = source[index] - '0';
            if (payload > (maximum - digit_value) / 10) return false;
            payload = payload * 10 + digit_value;
        }
        value = Word48(06400000000000000ULL | payload);
        return true;
    };
    const auto unsupported = [&] {
        // Compile the entire probe before execution; unsupported text cannot
        // leave partial output or cause a fallback into the reference backend.
        program.operations.clear();
        program.error = "native compiler incomplete at byte "
            + std::to_string(position)
            + ": " + (literal_error.empty()
                ? "supported development probe is a numeric arithmetic expression followed by =>"
                : literal_error);
        return program;
    };
    // 03724..03734 drains pending operators according to the masked resident
    // property field (04377). Lower fields bind more tightly; equal fields
    // drain first. Keep this restricted numeric lowering separate from mutable
    // syntax and the general compiler, whose accounting/recovery is not ported.
    struct Pending {
        bool parenthesis;
        NativeBinary binary;
        std::uint64_t priority;
        std::uint16_t provenance;
    };
    std::vector<Pending> pending;
    const auto emit = [&] {
        const auto op = pending.back();
        pending.pop_back();
        program.operations.push_back({NativeOperation::Kind::binary, {}, op.binary, op.provenance});
    };
    const auto operator_character = [](char c) {
        return c == '+' || c == '-' || c == '*' || c == '/';
    };
    const auto operator_info = [](char c) {
        Pending result{false, NativeBinary::add, 0, 06672};
        Word48 name("+");
        switch (c) {
        case '-': result.binary = NativeBinary::subtract; result.provenance = 06702; name = Word48("-"); break;
        case '*': result.binary = NativeBinary::multiply; result.provenance = 06676; name = Word48("*"); break;
        case '/': result.binary = NativeBinary::divide; result.provenance = 06706; name = Word48("/"); break;
        }
        for (const auto &descriptor : keyword_descriptors) {
            if (descriptor.name == name) {
                result.priority = descriptor.property_word.raw() & 03600000000000000ULL;
                break;
            }
        }
        return result;
    };
    bool operand = true;
    bool expression = false;
    bool after_binary = false;
    space();
    while (position < source.size()) {
        if (operand) {
            if (source[position] == '(') {
                // Parenthesized subexpressions retain their own reduction boundary.
                pending.push_back({true, {}, 0, 03570});
                ++position;
                after_binary = false;
            } else {
                // Unary operators after infix operators have historical stack
                // effects not covered by this slice. Do not guess their grammar.
                if (after_binary && (source[position] == '+' || source[position] == '-'))
                    return unsupported();
                Word48 value;
                if (!number(value)) return unsupported();
                program.operations.push_back({NativeOperation::Kind::constant, value, {}, 016645});
                operand = false;
                expression = true;
            }
        } else if (source[position] == ')') {
            while (!pending.empty() && !pending.back().parenthesis) emit();
            if (pending.empty()) return unsupported();
            pending.pop_back();
            ++position;
        } else if (source.compare(position, 2, "=>") == 0) {
            while (!pending.empty() && !pending.back().parenthesis) emit();
            if (!pending.empty()) return unsupported();
            position += 2;
            program.operations.push_back({NativeOperation::Kind::print, {}, {}, 012674});
            operand = true;
            expression = false;
            after_binary = false;
        } else if (operator_character(source[position])) {
            const auto next = operator_info(source[position++]);
            // The resident reader interns adjacent operator characters as one
            // word: **, +-, etc. are not two arithmetic tokens.
            if (position < source.size() && operator_character(source[position]))
                return unsupported();
            while (!pending.empty() && !pending.back().parenthesis
                   && pending.back().priority <= next.priority) emit();
            pending.push_back(next);
            operand = true;
            after_binary = true;
        } else {
            return unsupported();
        }
        space();
    }
    if (expression || !pending.empty()) return unsupported();
    for (auto &operation : program.operations) {
        const auto cost = native_emission_cost(operation.kind == NativeOperation::Kind::constant
            ? NativeEmission::literal : NativeEmission::call_binding);
        operation.emission_site = cost.site;
        operation.legacy_words = cost.code_words;
    }
    return program;
}

NativeExecutionResult NativeRuntime::execute(std::size_t program_index, std::ostream &output)
{
    if (!programs_.at(program_index)) throw std::logic_error("native program was released");
    const auto &program = programs_[program_index]->program;
    NativeExecutionResult result;
    if (!program.error.empty()) {
        result.error = program.error;
        return result;
    }
    result.storage_accounting_complete = programs_[program_index]->storage.has_value()
        || program.operations.empty();
    auto &values = values_;
    // The linear probe has one explicit operation cursor. General activation
    // frames and legacy code/activation accounting remain separate open gates.
    for (std::size_t operation = 0; operation < program.operations.size(); ++operation) {
        const auto &instruction = program.operations[operation];
        ++result.operations;
        switch (instruction.kind) {
        case NativeOperation::Kind::constant:
            values.push_back({NativeValue::Kind::number, instruction.constant, 0});
            break;
        case NativeOperation::Kind::binary: {
            if (values.size() < 2) {
                result.error = "invalid native graph: binary stack underflow";
                return result;
            }
            if (values.back().kind != NativeValue::Kind::number
                || values[values.size() - 2].kind != NativeValue::Kind::number) {
                result.error = "native numeric operation requires numbers";
                return result;
            }
            const Word48 right = values.back().number;
            values.pop_back();
            const auto number = native_number_binary(instruction.binary, values.back().number, right);
            if (!number.error.empty()) {
                result.error = number.error;
                return result;
            }
            values.back().number = number.value;
            break;
        }
        case NativeOperation::Kind::print:
            if (values.empty()) {
                result.error = "invalid native graph: print stack underflow";
                return result;
            }
            if (std::any_of(values.begin(), values.end(), [](NativeValue value) {
                    return value.kind != NativeValue::Kind::number; })) {
                result.error = "native printing of nonnumeric values is not implemented";
                return result;
            }
            output << "**";
            for (const auto value : values) {
                const auto text = native_format_number(value.number);
                output << text;
            }
            output << '\n';
            values.clear();
            break;
        }
    }
    return result;
}

NativeExecutionResult native_execute(const NativeProgram &program, std::ostream &output)
{
    NativeRuntime runtime;
    return runtime.execute(runtime.retain_program(program), output);
}

int run_native_shell(std::istream &input, std::ostream &output, std::ostream &errors)
{
    for (const char *control : {"POPLAN_INTERPRET_ONLY", "POPLAN_DISABLE_TRANSLATED_ROUTINES",
                               "POPLAN_CPU_TRACE", "POPLAN_CPU_DETAIL_TRACE",
                               "POPLAN_ROUTINE_TRACE", "POPLAN_E70_TRACE"}) {
        if (std::getenv(control) != nullptr) {
            errors << "poplan: --native is incompatible with " << control << '\n';
            return 2;
        }
    }
    const std::string source{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
    if (input.bad()) {
        errors << "poplan: native input read failed\n";
        return 1;
    }
    const auto result = native_execute(native_compile_probe(source), output);
    if (!result.error.empty()) {
        errors << "poplan: " << result.error << '\n';
        return 1;
    }
    if (!output) {
        errors << "poplan: native output write failed\n";
        return 1;
    }
    return 0;
}

} // namespace poplan
