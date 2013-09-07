/*
** Copyright (c) 2008 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains the code to do mnemonic encoding of commit IDs. The
** wordlist and algorithm were originally taken from:
**
**   https://github.com/singpolyma/mnemonicode
**
** ...which is BSD licensed. However, the algorithm has since been heavily
** modified to more fit Fossil's special needs.
**
** The new algorithm works by reading hex characters one at a time and
** accumulating bits into a ring buffer. When available, 10 bits are consumed
** and used as a word index. This only uses 1024 of the available 1626 words
** but preserves the property that encoded strings may be truncated at any
** point and the resulting hex string, when decoded, is a truncated form of
** the original hex string. It is, unfortunately, slightly less efficient, and
** three words can only encode seven hex digits.
*/

#include "config.h"
#include "mnemonic.h"

#define MN_BASE 1626

/* Number of bits to encode as a word. 10 is the maximum. */
#define CHUNK_SIZE 10

static const char* basewordlist[MN_BASE] =
{
  "ACADEMY",  "ACROBAT",  "ACTIVE",   "ACTOR",    "ADAM",     "ADMIRAL",
  "ADRIAN",   "AFRICA",   "AGENDA",   "AGENT",    "AIRLINE",  "AIRPORT",
  "ALADDIN",  "ALARM",    "ALASKA",   "ALBERT",   "ALBINO",   "ALBUM",
  "ALCOHOL",  "ALEX",     "ALGEBRA",  "ALIBI",    "ALICE",    "ALIEN",
  "ALPHA",    "ALPINE",   "AMADEUS",  "AMANDA",   "AMAZON",   "AMBER",
  "AMERICA",  "AMIGO",    "ANALOG",   "ANATOMY",  "ANGEL",    "ANIMAL",
  "ANTENNA",  "ANTONIO",  "APOLLO",   "APRIL",    "ARCHIVE",  "ARCTIC",
  "ARIZONA",  "ARNOLD",   "AROMA",    "ARTHUR",   "ARTIST",   "ASIA",
  "ASPECT",   "ASPIRIN",  "ATHENA",   "ATHLETE",  "ATLAS",    "AUDIO",
  "AUGUST",   "AUSTRIA",  "AXIOM",    "AZTEC",    "BALANCE",  "BALLAD",
  "BANANA",   "BANDIT",   "BANJO",    "BARCODE",  "BARON",    "BASIC",
  "BATTERY",  "BELGIUM",  "BERLIN",   "BERMUDA",  "BERNARD",  "BIKINI",
  "BINARY",   "BINGO",    "BIOLOGY",  "BLOCK",    "BLONDE",   "BONUS",
  "BORIS",    "BOSTON",   "BOXER",    "BRANDY",   "BRAVO",    "BRAZIL",
  "BRONZE",   "BROWN",    "BRUCE",    "BRUNO",    "BURGER",   "BURMA",
  "CABINET",  "CACTUS",   "CAFE",     "CAIRO",    "CAKE",     "CALYPSO",
  "CAMEL",    "CAMERA",   "CAMPUS",   "CANADA",   "CANAL",    "CANNON",
  "CANOE",    "CANTINA",  "CANVAS",   "CANYON",   "CAPITAL",  "CARAMEL",
  "CARAVAN",  "CARBON",   "CARGO",    "CARLO",    "CAROL",    "CARPET",
  "CARTEL",   "CASINO",   "CASTLE",   "CASTRO",   "CATALOG",  "CAVIAR",
  "CECILIA",  "CEMENT",   "CENTER",   "CENTURY",  "CERAMIC",  "CHAMBER",
  "CHANCE",   "CHANGE",   "CHAOS",    "CHARLIE",  "CHARM",    "CHARTER",
  "CHEF",     "CHEMIST",  "CHERRY",   "CHESS",    "CHICAGO",  "CHICKEN",
  "CHIEF",    "CHINA",    "CIGAR",    "CINEMA",   "CIRCUS",   "CITIZEN",
  "CITY",     "CLARA",    "CLASSIC",  "CLAUDIA",  "CLEAN",    "CLIENT",
  "CLIMAX",   "CLINIC",   "CLOCK",    "CLUB",     "COBRA",    "COCONUT",
  "COLA",     "COLLECT",  "COLOMBO",  "COLONY",   "COLOR",    "COMBAT",
  "COMEDY",   "COMET",    "COMMAND",  "COMPACT",  "COMPANY",  "COMPLEX",
  "CONCEPT",  "CONCERT",  "CONNECT",  "CONSUL",   "CONTACT",  "CONTEXT",
  "CONTOUR",  "CONTROL",  "CONVERT",  "COPY",     "CORNER",   "CORONA",
  "CORRECT",  "COSMOS",   "COUPLE",   "COURAGE",  "COWBOY",   "CRAFT",
  "CRASH",    "CREDIT",   "CRICKET",  "CRITIC",   "CROWN",    "CRYSTAL",
  "CUBA",     "CULTURE",  "DALLAS",   "DANCE",    "DANIEL",   "DAVID",
  "DECADE",   "DECIMAL",  "DELIVER",  "DELTA",    "DELUXE",   "DEMAND",
  "DEMO",     "DENMARK",  "DERBY",    "DESIGN",   "DETECT",   "DEVELOP",
  "DIAGRAM",  "DIALOG",   "DIAMOND",  "DIANA",    "DIEGO",    "DIESEL",
  "DIET",     "DIGITAL",  "DILEMMA",  "DIPLOMA",  "DIRECT",   "DISCO",
  "DISNEY",   "DISTANT",  "DOCTOR",   "DOLLAR",   "DOMINIC",  "DOMINO",
  "DONALD",   "DRAGON",   "DRAMA",    "DUBLIN",   "DUET",     "DYNAMIC",
  "EAST",     "ECOLOGY",  "ECONOMY",  "EDGAR",    "EGYPT",    "ELASTIC",
  "ELEGANT",  "ELEMENT",  "ELITE",    "ELVIS",    "EMAIL",    "ENERGY",
  "ENGINE",   "ENGLISH",  "EPISODE",  "EQUATOR",  "ESCORT",   "ETHNIC",
  "EUROPE",   "EVEREST",  "EVIDENT",  "EXACT",    "EXAMPLE",  "EXIT",
  "EXOTIC",   "EXPORT",   "EXPRESS",  "EXTRA",    "FABRIC",   "FACTOR",
  "FALCON",   "FAMILY",   "FANTASY",  "FASHION",  "FIBER",    "FICTION",
  "FIDEL",    "FIESTA",   "FIGURE",   "FILM",     "FILTER",   "FINAL",
  "FINANCE",  "FINISH",   "FINLAND",  "FLASH",    "FLORIDA",  "FLOWER",
  "FLUID",    "FLUTE",    "FOCUS",    "FORD",     "FOREST",   "FORMAL",
  "FORMAT",   "FORMULA",  "FORTUNE",  "FORUM",    "FRAGILE",  "FRANCE",
  "FRANK",    "FRIEND",   "FROZEN",   "FUTURE",   "GABRIEL",  "GALAXY",
  "GALLERY",  "GAMMA",    "GARAGE",   "GARDEN",   "GARLIC",   "GEMINI",
  "GENERAL",  "GENETIC",  "GENIUS",   "GERMANY",  "GLOBAL",   "GLORIA",
  "GOLF",     "GONDOLA",  "GONG",     "GOOD",     "GORDON",   "GORILLA",
  "GRAND",    "GRANITE",  "GRAPH",    "GREEN",    "GROUP",    "GUIDE",
  "GUITAR",   "GURU",     "HAND",     "HAPPY",    "HARBOR",   "HARMONY",
  "HARVARD",  "HAVANA",   "HAWAII",   "HELENA",   "HELLO",    "HENRY",
  "HILTON",   "HISTORY",  "HORIZON",  "HOTEL",    "HUMAN",    "HUMOR",
  "ICON",     "IDEA",     "IGLOO",    "IGOR",     "IMAGE",    "IMPACT",
  "IMPORT",   "INDEX",    "INDIA",    "INDIGO",   "INPUT",    "INSECT",
  "INSTANT",  "IRIS",     "ITALIAN",  "JACKET",   "JACOB",    "JAGUAR",
  "JANET",    "JAPAN",    "JARGON",   "JAZZ",     "JEEP",     "JOHN",
  "JOKER",    "JORDAN",   "JUMBO",    "JUNE",     "JUNGLE",   "JUNIOR",
  "JUPITER",  "KARATE",   "KARMA",    "KAYAK",    "KERMIT",   "KILO",
  "KING",     "KOALA",    "KOREA",    "LABOR",    "LADY",     "LAGOON",
  "LAPTOP",   "LASER",    "LATIN",    "LAVA",     "LECTURE",  "LEFT",
  "LEGAL",    "LEMON",    "LEVEL",    "LEXICON",  "LIBERAL",  "LIBRA",
  "LIMBO",    "LIMIT",    "LINDA",    "LINEAR",   "LION",     "LIQUID",
  "LITER",    "LITTLE",   "LLAMA",    "LOBBY",    "LOBSTER",  "LOCAL",
  "LOGIC",    "LOGO",     "LOLA",     "LONDON",   "LOTUS",    "LUCAS",
  "LUNAR",    "MACHINE",  "MACRO",    "MADAM",    "MADONNA",  "MADRID",
  "MAESTRO",  "MAGIC",    "MAGNET",   "MAGNUM",   "MAJOR",    "MAMA",
  "MAMBO",    "MANAGER",  "MANGO",    "MANILA",   "MARCO",    "MARINA",
  "MARKET",   "MARS",     "MARTIN",   "MARVIN",   "MASTER",   "MATRIX",
  "MAXIMUM",  "MEDIA",    "MEDICAL",  "MEGA",     "MELODY",   "MELON",
  "MEMO",     "MENTAL",   "MENTOR",   "MENU",     "MERCURY",  "MESSAGE",
  "METAL",    "METEOR",   "METER",    "METHOD",   "METRO",    "MEXICO",
  "MIAMI",    "MICRO",    "MILLION",  "MINERAL",  "MINIMUM",  "MINUS",
  "MINUTE",   "MIRACLE",  "MIRAGE",   "MIRANDA",  "MISTER",   "MIXER",
  "MOBILE",   "MODEL",    "MODEM",    "MODERN",   "MODULAR",  "MOMENT",
  "MONACO",   "MONICA",   "MONITOR",  "MONO",     "MONSTER",  "MONTANA",
  "MORGAN",   "MOTEL",    "MOTIF",    "MOTOR",    "MOZART",   "MULTI",
  "MUSEUM",   "MUSIC",    "MUSTANG",  "NATURAL",  "NEON",     "NEPAL",
  "NEPTUNE",  "NERVE",    "NEUTRAL",  "NEVADA",   "NEWS",     "NINJA",
  "NIRVANA",  "NORMAL",   "NOVA",     "NOVEL",    "NUCLEAR",  "NUMERIC",
  "NYLON",    "OASIS",    "OBJECT",   "OBSERVE",  "OCEAN",    "OCTOPUS",
  "OLIVIA",   "OLYMPIC",  "OMEGA",    "OPERA",    "OPTIC",    "OPTIMAL",
  "ORANGE",   "ORBIT",    "ORGANIC",  "ORIENT",   "ORIGIN",   "ORLANDO",
  "OSCAR",    "OXFORD",   "OXYGEN",   "OZONE",    "PABLO",    "PACIFIC",
  "PAGODA",   "PALACE",   "PAMELA",   "PANAMA",   "PANDA",    "PANEL",
  "PANIC",    "PARADOX",  "PARDON",   "PARIS",    "PARKER",   "PARKING",
  "PARODY",   "PARTNER",  "PASSAGE",  "PASSIVE",  "PASTA",    "PASTEL",
  "PATENT",   "PATRIOT",  "PATROL",   "PATRON",   "PEGASUS",  "PELICAN",
  "PENGUIN",  "PEPPER",   "PERCENT",  "PERFECT",  "PERFUME",  "PERIOD",
  "PERMIT",   "PERSON",   "PERU",     "PHONE",    "PHOTO",    "PIANO",
  "PICASSO",  "PICNIC",   "PICTURE",  "PIGMENT",  "PILGRIM",  "PILOT",
  "PIRATE",   "PIXEL",    "PIZZA",    "PLANET",   "PLASMA",   "PLASTER",
  "PLASTIC",  "PLAZA",    "POCKET",   "POEM",     "POETIC",   "POKER",
  "POLARIS",  "POLICE",   "POLITIC",  "POLO",     "POLYGON",  "PONY",
  "POPCORN",  "POPULAR",  "POSTAGE",  "POSTAL",   "PRECISE",  "PREFIX",
  "PREMIUM",  "PRESENT",  "PRICE",    "PRINCE",   "PRINTER",  "PRISM",
  "PRIVATE",  "PRODUCT",  "PROFILE",  "PROGRAM",  "PROJECT",  "PROTECT",
  "PROTON",   "PUBLIC",   "PULSE",    "PUMA",     "PYRAMID",  "QUEEN",
  "RADAR",    "RADIO",    "RANDOM",   "RAPID",    "REBEL",    "RECORD",
  "RECYCLE",  "REFLEX",   "REFORM",   "REGARD",   "REGULAR",  "RELAX",
  "REPORT",   "REPTILE",  "REVERSE",  "RICARDO",  "RINGO",    "RITUAL",
  "ROBERT",   "ROBOT",    "ROCKET",   "RODEO",    "ROMEO",    "ROYAL",
  "RUSSIAN",  "SAFARI",   "SALAD",    "SALAMI",   "SALMON",   "SALON",
  "SALUTE",   "SAMBA",    "SANDRA",   "SANTANA",  "SARDINE",  "SCHOOL",
  "SCREEN",   "SCRIPT",   "SECOND",   "SECRET",   "SECTION",  "SEGMENT",
  "SELECT",   "SEMINAR",  "SENATOR",  "SENIOR",   "SENSOR",   "SERIAL",
  "SERVICE",  "SHERIFF",  "SHOCK",    "SIERRA",   "SIGNAL",   "SILICON",
  "SILVER",   "SIMILAR",  "SIMON",    "SINGLE",   "SIREN",    "SLOGAN",
  "SOCIAL",   "SODA",     "SOLAR",    "SOLID",    "SOLO",     "SONIC",
  "SOVIET",   "SPECIAL",  "SPEED",    "SPIRAL",   "SPIRIT",   "SPORT",
  "STATIC",   "STATION",  "STATUS",   "STEREO",   "STONE",    "STOP",
  "STREET",   "STRONG",   "STUDENT",  "STUDIO",   "STYLE",    "SUBJECT",
  "SULTAN",   "SUPER",    "SUSAN",    "SUSHI",    "SUZUKI",   "SWITCH",
  "SYMBOL",   "SYSTEM",   "TACTIC",   "TAHITI",   "TALENT",   "TANGO",
  "TARZAN",   "TAXI",     "TELEX",    "TEMPO",    "TENNIS",   "TEXAS",
  "TEXTILE",  "THEORY",   "THERMOS",  "TIGER",    "TITANIC",  "TOKYO",
  "TOMATO",   "TOPIC",    "TORNADO",  "TORONTO",  "TORPEDO",  "TOTAL",
  "TOTEM",    "TOURIST",  "TRACTOR",  "TRAFFIC",  "TRANSIT",  "TRAPEZE",
  "TRAVEL",   "TRIBAL",   "TRICK",    "TRIDENT",  "TRILOGY",  "TRIPOD",
  "TROPIC",   "TRUMPET",  "TULIP",    "TUNA",     "TURBO",    "TWIST",
  "ULTRA",    "UNIFORM",  "UNION",    "URANIUM",  "VACUUM",   "VALID",
  "VAMPIRE",  "VANILLA",  "VATICAN",  "VELVET",   "VENTURA",  "VENUS",
  "VERTIGO",  "VETERAN",  "VICTOR",   "VIDEO",    "VIENNA",   "VIKING",
  "VILLAGE",  "VINCENT",  "VIOLET",   "VIOLIN",   "VIRTUAL",  "VIRUS",
  "VISA",     "VISION",   "VISITOR",  "VISUAL",   "VITAMIN",  "VIVA",
  "VOCAL",    "VODKA",    "VOLCANO",  "VOLTAGE",  "VOLUME",   "VOYAGE",
  "WATER",    "WEEKEND",  "WELCOME",  "WESTERN",  "WINDOW",   "WINTER",
  "WIZARD",   "WOLF",     "WORLD",    "XRAY",     "YANKEE",   "YOGA",
  "YOGURT",   "YOYO",     "ZEBRA",    "ZERO",     "ZIGZAG",   "ZIPPER",
  "ZODIAC",   "ZOOM",     "ABRAHAM",  "ACTION",   "ADDRESS",  "ALABAMA",
  "ALFRED",   "ALMOND",   "AMMONIA",  "ANALYZE",  "ANNUAL",   "ANSWER",
  "APPLE",    "ARENA",    "ARMADA",   "ARSENAL",  "ATLANTA",  "ATOMIC",
  "AVENUE",   "AVERAGE",  "BAGEL",    "BAKER",    "BALLET",   "BAMBINO",
  "BAMBOO",   "BARBARA",  "BASKET",   "BAZAAR",   "BENEFIT",  "BICYCLE",
  "BISHOP",   "BLITZ",    "BONJOUR",  "BOTTLE",   "BRIDGE",   "BRITISH",
  "BROTHER",  "BRUSH",    "BUDGET",   "CABARET",  "CADET",    "CANDLE",
  "CAPITAN",  "CAPSULE",  "CAREER",   "CARTOON",  "CHANNEL",  "CHAPTER",
  "CHEESE",   "CIRCLE",   "COBALT",   "COCKPIT",  "COLLEGE",  "COMPASS",
  "COMRADE",  "CONDOR",   "CRIMSON",  "CYCLONE",  "DARWIN",   "DECLARE",
  "DEGREE",   "DELETE",   "DELPHI",   "DENVER",   "DESERT",   "DIVIDE",
  "DOLBY",    "DOMAIN",   "DOMINGO",  "DOUBLE",   "DRINK",    "DRIVER",
  "EAGLE",    "EARTH",    "ECHO",     "ECLIPSE",  "EDITOR",   "EDUCATE",
  "EDWARD",   "EFFECT",   "ELECTRA",  "EMERALD",  "EMOTION",  "EMPIRE",
  "EMPTY",    "ESCAPE",   "ETERNAL",  "EVENING",  "EXHIBIT",  "EXPAND",
  "EXPLORE",  "EXTREME",  "FERRARI",  "FIRST",    "FLAG",     "FOLIO",
  "FORGET",   "FORWARD",  "FREEDOM",  "FRESH",    "FRIDAY",   "FUJI",
  "GALILEO",  "GARCIA",   "GENESIS",  "GOLD",     "GRAVITY",  "HABITAT",
  "HAMLET",   "HARLEM",   "HELIUM",   "HOLIDAY",  "HOUSE",    "HUNTER",
  "IBIZA",    "ICEBERG",  "IMAGINE",  "INFANT",   "ISOTOPE",  "JACKSON",
  "JAMAICA",  "JASMINE",  "JAVA",     "JESSICA",  "JUDO",     "KITCHEN",
  "LAZARUS",  "LETTER",   "LICENSE",  "LITHIUM",  "LOYAL",    "LUCKY",
  "MAGENTA",  "MAILBOX",  "MANUAL",   "MARBLE",   "MARY",     "MAXWELL",
  "MAYOR",    "MILK",     "MONARCH",  "MONDAY",   "MONEY",    "MORNING",
  "MOTHER",   "MYSTERY",  "NATIVE",   "NECTAR",   "NELSON",   "NETWORK",
  "NEXT",     "NIKITA",   "NOBEL",    "NOBODY",   "NOMINAL",  "NORWAY",
  "NOTHING",  "NUMBER",   "OCTOBER",  "OFFICE",   "OLIVER",   "OPINION",
  "OPTION",   "ORDER",    "OUTSIDE",  "PACKAGE",  "PANCAKE",  "PANDORA",
  "PANTHER",  "PAPA",     "PATIENT",  "PATTERN",  "PEDRO",    "PENCIL",
  "PEOPLE",   "PHANTOM",  "PHILIPS",  "PIONEER",  "PLUTO",    "PODIUM",
  "PORTAL",   "POTATO",   "PRIZE",    "PROCESS",  "PROTEIN",  "PROXY",
  "PUMP",     "PUPIL",    "PYTHON",   "QUALITY",  "QUARTER",  "QUIET",
  "RABBIT",   "RADICAL",  "RADIUS",   "RAINBOW",  "RALPH",    "RAMIREZ",
  "RAVIOLI",  "RAYMOND",  "RESPECT",  "RESPOND",  "RESULT",   "RESUME",
  "RETRO",    "RICHARD",  "RIGHT",    "RISK",     "RIVER",    "ROGER",
  "ROMAN",    "RONDO",    "SABRINA",  "SALARY",   "SALSA",    "SAMPLE",
  "SAMUEL",   "SATURN",   "SAVAGE",   "SCARLET",  "SCOOP",    "SCORPIO",
  "SCRATCH",  "SCROLL",   "SECTOR",   "SERPENT",  "SHADOW",   "SHAMPOO",
  "SHARON",   "SHARP",    "SHORT",    "SHRINK",   "SILENCE",  "SILK",
  "SIMPLE",   "SLANG",    "SMART",    "SMOKE",    "SNAKE",    "SOCIETY",
  "SONAR",    "SONATA",   "SOPRANO",  "SOURCE",   "SPARTA",   "SPHERE",
  "SPIDER",   "SPONSOR",  "SPRING",   "ACID",     "ADIOS",    "AGATHA",
  "ALAMO",    "ALERT",    "ALMANAC",  "ALOHA",    "ANDREA",   "ANITA",
  "ARCADE",   "AURORA",   "AVALON",   "BABY",     "BAGGAGE",  "BALLOON",
  "BANK",     "BASIL",    "BEGIN",    "BISCUIT",  "BLUE",     "BOMBAY",
  "BRAIN",    "BRENDA",   "BRIGADE",  "CABLE",    "CARMEN",   "CELLO",
  "CELTIC",   "CHARIOT",  "CHROME",   "CITRUS",   "CIVIL",    "CLOUD",
  "COMMON",   "COMPARE",  "COOL",     "COPPER",   "CORAL",    "CRATER",
  "CUBIC",    "CUPID",    "CYCLE",    "DEPEND",   "DOOR",     "DREAM",
  "DYNASTY",  "EDISON",   "EDITION",  "ENIGMA",   "EQUAL",    "ERIC",
  "EVENT",    "EVITA",    "EXODUS",   "EXTEND",   "FAMOUS",   "FARMER",
  "FOOD",     "FOSSIL",   "FROG",     "FRUIT",    "GENEVA",   "GENTLE",
  "GEORGE",   "GIANT",    "GILBERT",  "GOSSIP",   "GRAM",     "GREEK",
  "GRILLE",   "HAMMER",   "HARVEST",  "HAZARD",   "HEAVEN",   "HERBERT",
  "HEROIC",   "HEXAGON",  "HUSBAND",  "IMMUNE",   "INCA",     "INCH",
  "INITIAL",  "ISABEL",   "IVORY",    "JASON",    "JEROME",   "JOEL",
  "JOSHUA",   "JOURNAL",  "JUDGE",    "JULIET",   "JUMP",     "JUSTICE",
  "KIMONO",   "KINETIC",  "LEONID",   "LIMA",     "MAZE",     "MEDUSA",
  "MEMBER",   "MEMPHIS",  "MICHAEL",  "MIGUEL",   "MILAN",    "MILE",
  "MILLER",   "MIMIC",    "MIMOSA",   "MISSION",  "MONKEY",   "MORAL",
  "MOSES",    "MOUSE",    "NANCY",    "NATASHA",  "NEBULA",   "NICKEL",
  "NINA",     "NOISE",    "ORCHID",   "OREGANO",  "ORIGAMI",  "ORINOCO",
  "ORION",    "OTHELLO",  "PAPER",    "PAPRIKA",  "PRELUDE",  "PREPARE",
  "PRETEND",  "PROFIT",   "PROMISE",  "PROVIDE",  "PUZZLE",   "REMOTE",
  "REPAIR",   "REPLY",    "RIVAL",    "RIVIERA",  "ROBIN",    "ROSE",
  "ROVER",    "RUDOLF",   "SAGA",     "SAHARA",   "SCHOLAR",  "SHELTER",
  "SHIP",     "SHOE",     "SIGMA",    "SISTER",   "SLEEP",    "SMILE",
  "SPAIN",    "SPARK",    "SPLIT",    "SPRAY",    "SQUARE",   "STADIUM",
  "STAR",     "STORM",    "STORY",    "STRANGE",  "STRETCH",  "STUART",
  "SUBWAY",   "SUGAR",    "SULFUR",   "SUMMER",   "SURVIVE",  "SWEET",
  "SWIM",     "TABLE",    "TABOO",    "TARGET",   "TEACHER",  "TELECOM",
  "TEMPLE",   "TIBET",    "TICKET",   "TINA",     "TODAY",    "TOGA",
  "TOMMY",    "TOWER",    "TRIVIAL",  "TUNNEL",   "TURTLE",   "TWIN",
  "UNCLE",    "UNICORN",  "UNIQUE",   "UPDATE",   "VALERY",   "VEGA",
  "VERSION",  "VOODOO",   "WARNING",  "WILLIAM",  "WONDER",   "YEAR",
  "YELLOW",   "YOUNG",    "ABSENT",   "ABSORB",   "ACCENT",   "ALFONSO",
  "ALIAS",    "AMBIENT",  "ANDY",     "ANVIL",    "APPEAR",   "APROPOS",
  "ARCHER",   "ARIEL",    "ARMOR",    "ARROW",    "AUSTIN",   "AVATAR",
  "AXIS",     "BABOON",   "BAHAMA",   "BALI",     "BALSA",    "BAZOOKA",
  "BEACH",    "BEAST",    "BEATLES",  "BEAUTY",   "BEFORE",   "BENNY",
  "BETTY",    "BETWEEN",  "BEYOND",   "BILLY",    "BISON",    "BLAST",
  "BLESS",    "BOGART",   "BONANZA",  "BOOK",     "BORDER",   "BRAVE",
  "BREAD",    "BREAK",    "BROKEN",   "BUCKET",   "BUENOS",   "BUFFALO",
  "BUNDLE",   "BUTTON",   "BUZZER",   "BYTE",     "CAESAR",   "CAMILLA",
  "CANARY",   "CANDID",   "CARROT",   "CAVE",     "CHANT",    "CHILD",
  "CHOICE",   "CHRIS",    "CIPHER",   "CLARION",  "CLARK",    "CLEVER",
  "CLIFF",    "CLONE",    "CONAN",    "CONDUCT",  "CONGO",    "CONTENT",
  "COSTUME",  "COTTON",   "COVER",    "CRACK",    "CURRENT",  "DANUBE",
  "DATA",     "DECIDE",   "DESIRE",   "DETAIL",   "DEXTER",   "DINNER",
  "DISPUTE",  "DONOR",    "DRUID",    "DRUM",     "EASY",     "EDDIE",
  "ENJOY",    "ENRICO",   "EPOXY",    "EROSION",  "EXCEPT",   "EXILE",
  "EXPLAIN",  "FAME",     "FAST",     "FATHER",   "FELIX",    "FIELD",
  "FIONA",    "FIRE",     "FISH",     "FLAME",    "FLEX",     "FLIPPER",
  "FLOAT",    "FLOOD",    "FLOOR",    "FORBID",   "FOREVER",  "FRACTAL",
  "FRAME",    "FREDDIE",  "FRONT",    "FUEL",     "GALLOP",   "GAME",
  "GARBO",    "GATE",     "GIBSON",   "GINGER",   "GIRAFFE",  "GIZMO",
  "GLASS",    "GOBLIN",   "GOPHER",   "GRACE",    "GRAY",     "GREGORY",
  "GRID",     "GRIFFIN",  "GROUND",   "GUEST",    "GUSTAV",   "GYRO",
  "HAIR",     "HALT",     "HARRIS",   "HEART",    "HEAVY",    "HERMAN",
  "HIPPIE",   "HOBBY",    "HONEY",    "HOPE",     "HORSE",    "HOSTEL",
  "HYDRO",    "IMITATE",  "INFO",     "INGRID",   "INSIDE",   "INVENT",
  "INVEST",   "INVITE",   "IRON",     "IVAN",     "JAMES",    "JESTER",
  "JIMMY",    "JOIN",     "JOSEPH",   "JUICE",    "JULIUS",   "JULY",
  "JUSTIN",   "KANSAS",   "KARL",     "KEVIN",    "KIWI",     "LADDER",
  "LAKE",     "LAURA",    "LEARN",    "LEGACY",   "LEGEND",   "LESSON",
  "LIFE",     "LIGHT",    "LIST",     "LOCATE",   "LOPEZ",    "LORENZO",
  "LOVE",     "LUNCH",    "MALTA",    "MAMMAL",   "MARGO",    "MARION",
  "MASK",     "MATCH",    "MAYDAY",   "MEANING",  "MERCY",    "MIDDLE",
  "MIKE",     "MIRROR",   "MODEST",   "MORPH",    "MORRIS",   "NADIA",
  "NATO",     "NAVY",     "NEEDLE",   "NEURON",   "NEVER",    "NEWTON",
  "NICE",     "NIGHT",    "NISSAN",   "NITRO",    "NIXON",    "NORTH",
  "OBERON",   "OCTAVIA",  "OHIO",     "OLGA",     "OPEN",     "OPUS",
  "ORCA",     "OVAL",     "OWNER",    "PAGE",     "PAINT",    "PALMA",
  "PARADE",   "PARENT",   "PAROLE",   "PAUL",     "PEACE",    "PEARL",
  "PERFORM",  "PHOENIX",  "PHRASE",   "PIERRE",   "PINBALL",  "PLACE",
  "PLATE",    "PLATO",    "PLUME",    "POGO",     "POINT",    "POLITE",
  "POLKA",    "PONCHO",   "POWDER",   "PRAGUE",   "PRESS",    "PRESTO",
  "PRETTY",   "PRIME",    "PROMO",    "QUASI",    "QUEST",    "QUICK",
  "QUIZ",     "QUOTA",    "RACE",     "RACHEL",   "RAJA",     "RANGER",
  "REGION",   "REMARK",   "RENT",     "REWARD",   "RHINO",    "RIBBON",
  "RIDER",    "ROAD",     "RODENT",   "ROUND",    "RUBBER",   "RUBY",
  "RUFUS",    "SABINE",   "SADDLE",   "SAILOR",   "SAINT",    "SALT",
  "SATIRE",   "SCALE",    "SCUBA",    "SEASON",   "SECURE",   "SHAKE",
  "SHALLOW",  "SHANNON",  "SHAVE",    "SHELF",    "SHERMAN",  "SHINE",
  "SHIRT",    "SIDE",     "SINATRA",  "SINCERE",  "SIZE",     "SLALOM",
  "SLOW",     "SMALL",    "SNOW",     "SOFIA",    "SONG",     "SOUND",
  "SOUTH",    "SPEECH",   "SPELL",    "SPEND",    "SPOON",    "STAGE",
  "STAMP",    "STAND",    "STATE",    "STELLA",   "STICK",    "STING",
  "STOCK",    "STORE",    "SUNDAY",   "SUNSET",   "SUPPORT",  "SWEDEN",
  "SWING",    "TAPE",     "THINK",    "THOMAS",   "TICTAC",   "TIME",
  "TOAST",    "TOBACCO",  "TONIGHT",  "TORCH",    "TORSO",    "TOUCH",
  "TOYOTA",   "TRADE",    "TRIBUNE",  "TRINITY",  "TRITON",   "TRUCK",
  "TRUST",    "TYPE",     "UNDER",    "UNIT",     "URBAN",    "URGENT",
  "USER",     "VALUE",    "VENDOR",   "VENICE",   "VERONA",   "VIBRATE",
  "VIRGO",    "VISIBLE",  "VISTA",    "VITAL",    "VOICE",    "VORTEX",
  "WAITER",   "WATCH",    "WAVE",     "WEATHER",  "WEDDING",  "WHEEL",
  "WHISKEY",  "WISDOM",   "DEAL",     "NULL",     "NURSE",    "QUEBEC",
  "RESERVE",  "REUNION",  "ROOF",     "SINGER",   "VERBAL",   "AMEN"
};

/*
** Special string comparator.
*/

static int strspecialcmp(const char* s1, const char* s2)
{
  int c1, c2;

  do
  {
    c1 = toupper(*s1++);
    c2 = toupper(*s2++);
		if ((c1 == ' ') || (c1 == '-') || (c1 == '_'))
			c1 = 0;
		if ((c2 == ' ') || (c2 == '-') || (c2 == '_'))
			c2 = 0;
		if (!c1 || !c2)
			break;
  }
  while (c1 == c2);

  return c1 - c2;
}

/*
** Look up a word in the base dictionary and return its index; -1 if the word
** is not in the dictionary.
*/

static int baseword_to_index(const char* word)
{
  int i;

  for (i=0; i<sizeof(basewordlist)/sizeof(*basewordlist); i++)
    if (strspecialcmp(basewordlist[i], word) == 0)
      return i;

  return -1;
}

/*
** Look up a word by index in the base dictionary and return the string.
*/

static const char* index_to_baseword(int index)
{
  return basewordlist[index];
}

/*
** Encode a hex-character string using mnemonic encoding, writing the
** result into the supplied blob.
*/

int mnemonic_encode(const char* zString, Blob* xBlob)
{
	unsigned int buffer = 0;
	int buflen = 0;
  int first = 1;
	int value;

	for (;;)
	{
		/* Read hex characters into the buffer until we have at least CHUNK_SIZE
     * bits. */

		while (buflen < CHUNK_SIZE)
		{
			int i = sscanf(zString, "%1x", &value);
			if (i == 0) /* Parse error? */
				return 0;
			if (i == EOF) /* End of line */	
				break;
			buffer <<= 4;
			buffer |= value;
			buflen += 4;
			zString++;
		}

		if (buflen < CHUNK_SIZE)
		{
			/* Run out of available characters --- give up. */
			break;
		}

		/* Extract ten bits of data. */

		value = (buffer >> (buflen-CHUNK_SIZE)) & ((1<<CHUNK_SIZE)-1);
		buflen -= CHUNK_SIZE;

    /* Insert a leading space, if necessary. */

    if (!first)
      blob_appendf(xBlob, " ");
    first = 0;

		/* Actually encode a word. */

		blob_appendf(xBlob, "%s", index_to_baseword(value));
	}

  return 1;
}

/*
** Decode a a mnemonic encoded string, writing the resulting hex-character
** string into the supplied blob.
*/

int mnemonic_decode(const char* zString, Blob* xBlob)
{
	unsigned int buffer = 0;
	int buflen = 0;
  int first = 1;
	int value;

	while (zString)
	{
		/* Consume a word, if necessary. */

		while (buflen < 4)
		{
			/* Find the end of the current word. */

			const char* end = strchr(zString, ' ');
			if (!end)
				end = strchr(zString, '-');
			if (!end)
				end = strchr(zString, '_');

			/* Parse. */

			value = baseword_to_index(zString);
			if (value == -1) /* Parse error? */
				return 0;
			buffer <<= CHUNK_SIZE;
			buffer |= value;
			buflen += CHUNK_SIZE;

			/* Advance the pointer to the next word. */

			zString = end;
			if (zString)
				zString++;
		}

		/* Now consume nibbles and produce hex bytes. */

		while (buflen >= 4)
		{
			value = (buffer >> (buflen-4)) & 0xf;
			buflen -= 4;

			blob_appendf(xBlob, "%x", value);
		}
	}

  return 1;
}

/*
** Implementation of the "mnemonic_encode(X)" SQL function.  The argument X
** is an artifact ID prefix (hex characters). It will be mnemonic encoded
** and returned as a string.
*/
static void sqlcmd_mnemonic_encode(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  Blob x;
  const char *zName;
  int rc;
  assert( argc==1 );

  zName = (const char*)sqlite3_value_text(argv[0]);
  if( zName==0 ) return;

  blob_zero(&x);
  rc = mnemonic_encode(zName, &x);
  if ((rc==0) || (blob_size(&x) == 0))
    sqlite3_result_error(context, "could not mnemonic encode value", -1);
  else
    sqlite3_result_text(context, blob_buffer(&x), blob_size(&x),
                                 SQLITE_TRANSIENT);
  blob_reset(&x);
}

/*
** Implementation of the "mnemonic_decode(X)" SQL function.  The argument X
** is an mnemonic encoded artifact ID prefix (a string of words). It will be
** decoded and returned as a string of hex characters.
**
** If the string is not a valid mnemonic encoded value, it is returned
** unchanged.
*/
static void sqlcmd_mnemonic_decode(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  Blob x;
  const char *zName;
  int rc;
  assert( argc==1 );

  zName = (const char*)sqlite3_value_text(argv[0]);
  if( zName==0 ) return;

  blob_zero(&x);
  rc = mnemonic_decode(zName, &x);
  if ((rc==0) || (blob_size(&x)==0))
    sqlite3_result_text(context, zName, -1, SQLITE_TRANSIENT);
  else
    sqlite3_result_text(context, blob_buffer(&x), blob_size(&x),
                                 SQLITE_TRANSIENT);
  blob_reset(&x);
}

/*
** Add the SQL functions that wrap mnemonic_encode() and mnemonic_decode().
*/

void mnemonic_add_sql_func(sqlite3 *db)
{
  sqlite3_create_function(db, "mnemonic_encode", 1, SQLITE_ANY, 0,
                          sqlcmd_mnemonic_encode, 0, 0);
  sqlite3_create_function(db, "mnemonic_decode", 1, SQLITE_ANY, 0,
                          sqlcmd_mnemonic_decode, 0, 0);
}

/* vi: set ts=2:sw=2:expandtab: */

