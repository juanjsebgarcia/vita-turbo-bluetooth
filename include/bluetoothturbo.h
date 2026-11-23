/*
 * bluetoothturbo.h - shared by the kernel module and the Settings page.
 *
 * Settings are small integers identified by a BttKey. The Settings page reads
 * and writes them through the SceBluetoothTurbo syscalls (bttGet / bttSet); the
 * kernel module owns the values, saves them to ur0:data/bluetoothturbo/ and
 * applies them to the Bluetooth audio sender.
 */
#ifndef BLUETOOTHTURBO_H
#define BLUETOOTHTURBO_H

/* The numbers are what the Settings module reports to the kernel module at
 * start (BTT_KEY_SETTINGS_VER), so a mismatched pair shows in the log. */
#define BTT_VER_MAJOR   0
#define BTT_VER_MINOR   2
#define BTT_VER_PATCH   0
#define BTT_VER_NUM     ((BTT_VER_MAJOR << 16) | (BTT_VER_MINOR << 8) | BTT_VER_PATCH)
#define BTT_STR_(x)     #x
#define BTT_STR(x)      BTT_STR_(x)
#define BTT_VERSION     BTT_STR(BTT_VER_MAJOR) "." BTT_STR(BTT_VER_MINOR) "." BTT_STR(BTT_VER_PATCH)

#define BTT_DIR         "ur0:data/bluetoothturbo"
#define BTT_SETTINGS    BTT_DIR "/settings.bin"
#define BTT_CODECS_FILE BTT_DIR "/codecs.bin"   /* the codec lists of up to 16 headphones (kernel/btcodecs.h) */
#define BTT_KLOG_PATH   BTT_DIR "/bluetoothturbo_kernel.log"

/* Errors (negative), in the same style as the other Turbo plugins'. */
#define BTT_OK           0
#define BTT_ERR_ARG     -1
#define BTT_ERR_RANGE   -2
#define BTT_ERR_BUSY    -3   /* not now: a request of this kind is in hand, or the start-up period is not over */

/* Setting keys. Keep the order: settings.bin stores them by index. */
enum BttKey {
	BTT_KEY_ENABLE = 0,  /* 0/1 master switch: off = the console's original Bluetooth audio   */
	BTT_KEY_LOWLAT,      /* BttLatency: Bluetooth latency (the send queue's limit). The saved key keeps the
	                      * name of 0.1's on/off switch, so a saved 1 ("on") is Low                    */
	BTT_KEY_SOUND,       /* BttSound: the sound preset for Bluetooth headphones               */
	BTT_KEY_BAND1,       /* custom bands, 0..BTT_BAND_STEPS-1, BTT_BAND_FLAT = 0 dB           */
	BTT_KEY_BAND2,
	BTT_KEY_BAND3,
	BTT_KEY_BAND4,
	BTT_KEY_BAND5,
	BTT_KEY_CHANNELS,    /* BttChannels: Stereo / Mono / Swap Left and Right                  */
	BTT_KEY_BALANCE,     /* 0..BTT_BAL_STEPS-1, BTT_BAL_CENTRE = centre (L4 .. R4)            */
	BTT_KEY_VOLCURVE,    /* BttVolCurve: the volume steps sent to the headphones               */
	BTT_KEY_VOLMAX,      /* BttVolMax: the highest volume sent, 100 % down to 50 % in 10s      */
	BTT_KEY_CTRL,        /* BttCtrl: wireless controller response                              */
	BTT_KEY_QUALITY,     /* BttQuality: SBC Audio Quality (named Bluetooth Audio Quality before 0.2.0), applied from the next connection of the headphones */
	BTT_KEY_DISCONNECT,  /* BttDisconnect: what the console does when Bluetooth headphones disconnect */
	BTT_KEY_RECONNECT,   /* 0/1: reconnect the headphones last played on after the console wakes from standby */
	BTT_KEY_HFP,         /* 0..4: the hands-free connection's internal mode: 0 off, 4 the whole of it. No Settings row of its own: the
	                      * row BTT_KEY_READBATT sets it. Never read from the file (every start takes it from BTT_KEY_READBATT, which
	                      * the master switch gates); it keeps its place in the file                                              */
	BTT_KEY_LOWBATT,     /* 0/1: notify when the headset's battery runs low (read by the Home screen module, bluetoothturbo.suprx) */
	BTT_KEY_READBATT,    /* 0/1: the hands-free connection to a headset (its battery level and its microphone). On sets BTT_KEY_HFP to its
	                      * whole mode, off to 0; a file written before this key has it on                                               */
	BTT_KEY_CODEC,       /* BttCodec: SBC (the console's own, the default) or AAC, from the next connection of headphones that offer it. A file
	                      * written before this key has SBC                                                                              */
	BTT_KEY_AAC_ACCEL,   /* 0/1: the AAC encoder's NEON build (default on). It is used only if the start-up self-test passed; a file written
	                      * before this key has it on                                                                                     */
	BTT_KEY_AAC_BITRATE, /* BttAacBitrate: the bit rate asked of the headphones for AAC (default 256 kb/s), brought under their highest by the
	                      * kernel module; from the next connection. A file written before this key has the default                         */
	BTT_KEY_LDAC_RATE,   /* BttLdacRate: the LDAC bit rate: 330 kb/s (default), 660 kb/s or adaptive; from the next connection, and at once on a running LDAC stream. A file
	                      * written before this key has the default                                                                                                 */
	BTT_KEY_APTX_HD,     /* 0/1: aptX HD When Available (default on): with the aptX codec, aptX HD (experimental) is offered to headphones that list it; off keeps standard
	                      * aptX. Applies like the codec: the headphones reconnect when the link is aptX and the choice changes. A file written before this key has it on   */
	BTT_KEY_SOURCE_ONLY, /* 0/1: Music Source Only (default on): the Vita offers to send sound but not to be a speaker: the endpoint list it answers a device's DISCOVER with has
	                      * no sink and a configuration of the sink is rejected, so two-way devices (smart speakers) connect the right way round. Off = the console's own list.
	                      * Takes effect at the next connection; devices remember the role they chose, so the device is removed and paired again. A file written before this key has it on */
	BTT_KEY_COUNT,

	/* Write only, not stored. */
	BTT_KEY_SETTINGS_VER = 100,  /* value BTT_VER_NUM of the Settings module: the worker logs
	                              * it once, flagged if it differs from the kernel module's own */
	BTT_KEY_LDAC_AVAIL = 112,    /* read only (bttGet): BTT_LDAC_AVAIL_* bits, what the Settings page may offer of LDAC; bttSet refuses it */
	BTT_KEY_APTX_AVAIL = 111,    /* read only (bttGet): BTT_APTX_AVAIL_* bits, what the Settings page may offer of aptX; bttSet refuses it */
	BTT_KEY_AAC_AVAIL = 110,     /* read only (bttGet): BTT_AAC_AVAIL_* bits, what the Settings page may offer of AAC; bttSet refuses it */
	BTT_KEY_PAGE_SKIP = 101,     /* a BttPageSkip reason: the Settings module saw the Devices page and did not add its
	                              * entry; the worker logs each reason once */
	BTT_KEY_REFRESH_CODECS = 113, /* write only, value 1: the Refresh Codecs button on a device's page. Ask for the headphones' codecs to be read
	                              * again: the console disconnects them and connects them again (as an apply at once does), which makes it open the
	                              * stream and read the list. BTT_ERR_BUSY while a request is in hand or the start-up period is not over; the other
	                              * guards (a call or microphone, the list already known, Bluetooth Turbo off) are the worker's and are logged */
};

/* BTT_KEY_AAC_AVAIL: 0 while the AAC module (bluetoothturbo_aac.skprx, optional) is not loaded and found: the Settings page then offers SBC only in the
 * Bluetooth Codec row (its hint points to the add-on) and has no other AAC row. With the module, AAC Bit Rate is shown, and the codec row offers AAC
 * once the self-test has passed for the plain build (TESTED) and AAC is not held off (HELD); Hardware Accelerated AAC only with BTT_AAC_AVAIL_NEON.
 * The last two are greyed unless the codec is AAC. The bits are read each time: they change when the self-test finishes (from about 12 s after boot). */
#define BTT_AAC_AVAIL_MODULE  1   /* the AAC module is loaded and its interface matches                                           */
#define BTT_AAC_AVAIL_TESTED  2   /* the self-test has passed for the plain C encoder at least: AAC can be sent                    */
#define BTT_AAC_AVAIL_NEON    4   /* the self-test passed for both builds, the bytes equal: the NEON build may be used             */
#define BTT_AAC_AVAIL_HELD    8   /* the crash record holds AAC off this start (the last AAC session did not end cleanly)           */

/* BTT_KEY_APTX_AVAIL: the same for the aptX add-on (bluetoothturbo_aptx.skprx, optional): 0 while it is not loaded and found; MODULE when it is, TESTED once its
 * self-test passed, HELD when the crash record holds aptX off. The codec row offers aptX with MODULE and TESTED and not HELD. */
#define BTT_APTX_AVAIL_MODULE  1
#define BTT_APTX_AVAIL_TESTED  2
#define BTT_APTX_AVAIL_HD      4   /* the HD encoder's self-test passed too: aptX HD (experimental) may be used with headphones that offer it */
#define BTT_APTX_AVAIL_HELD    8

/* BTT_KEY_LDAC_AVAIL: the same for the LDAC add-on (bluetoothturbo_ldac.skprx, optional): 0 while it is not loaded and found; MODULE when it is, TESTED once its
 * self-test passed, HELD when the crash record holds LDAC off. The codec row offers LDAC with MODULE and TESTED and not HELD. */
#define BTT_LDAC_AVAIL_MODULE  1
#define BTT_LDAC_AVAIL_TESTED  2
#define BTT_LDAC_AVAIL_HELD    8

/* Why the Settings module left a page as it was (the values of BTT_KEY_PAGE_SKIP): the Devices page (ANCHOR, SIZE, ALLOC), or the page of one
 * Bluetooth device, whose rows it did not add although the page matched (DEVROWS_CLOSE: no closing tag to add them before; DEVROWS_ROOM: no room). */
enum BttPageSkip { BTT_SKIP_ANCHOR = 1, BTT_SKIP_SIZE, BTT_SKIP_ALLOC, BTT_SKIP_DEVROWS_CLOSE, BTT_SKIP_DEVROWS_ROOM, BTT_SKIP_COUNT };

/* Custom sound bands: 2 dB steps from -12 to +12 dB. */
#define BTT_BAND_COUNT  5
#define BTT_BAND_STEPS  13
#define BTT_BAND_FLAT   6
#define BTT_BAND_DB(v)  (((v) - BTT_BAND_FLAT) * 2)

/* Bluetooth latency: the number of sound packets the sender lets queue for the radio before it
 * backs off (stock 23, about 245 ms). */
enum BttLatency { BTT_LAT_STANDARD = 0, BTT_LAT_LOW, BTT_LAT_LOWEST, BTT_LAT_COUNT };

/* The codec Bluetooth headphones are sent: the console's own SBC, or AAC (the engine in kernel/btaac.c; applied from the next
 * connection, and only to headphones that offer AAC). */
enum BttCodec { BTT_CODEC_SBC = 0, BTT_CODEC_AAC, BTT_CODEC_APTX, BTT_CODEC_LDAC, BTT_CODEC_COUNT };

/* The LDAC bit rate, in the order of the Settings list (the key stores the index): 330 kb/s (the default), 660 kb/s, adaptive (between them, never above 660). */
enum BttLdacRate { BTT_LDACBR_330 = 0, BTT_LDACBR_660, BTT_LDACBR_ADAPTIVE, BTT_LDACBR_COUNT };

/* The AAC bit rate, kb/s, in the order of the Settings list (the key stores the index): 128, 192, 256 (the default), 320. The kernel module
 * never sends more than the headphones accept, whatever is asked for. */
enum BttAacBitrate { BTT_AACBR_128 = 0, BTT_AACBR_192, BTT_AACBR_256, BTT_AACBR_320, BTT_AACBR_COUNT };
#define BTT_AACBR_KBPS(i) (128u + 64u * (unsigned)(i))

enum BttChannels { BTT_CH_STEREO = 0, BTT_CH_MONO, BTT_CH_SWAP, BTT_CH_COUNT };

/* Balance: nine steps, 0 = L4 (the right side lowered most) .. 4 = centre .. 8 = R4. */
#define BTT_BAL_STEPS   9
#define BTT_BAL_CENTRE  4

/* Volume curve and maximum: how the Vita's 31 volume steps (0..30) map onto the number sent to
 * the headphones (AVRCP absolute volume, 0..127). Standard with 100 % is the console's own. */
enum BttVolCurve { BTT_VC_STANDARD = 0, BTT_VC_FINER, BTT_VC_LOUDER, BTT_VC_COUNT };
#define BTT_VOLMAX_COUNT  6                    /* 100, 90, 80, 70, 60, 50 % */
#define BTT_VOLMAX_PCT(i) (100 - 10 * (i))

enum BttCtrl { BTT_CTRL_STANDARD = 0, BTT_CTRL_FAST, BTT_CTRL_COUNT };

/* Bluetooth audio quality, from the next connection of the headphones. The numbers run in the order of the Settings list.
 * Stable and Balanced cap the highest SBC bitpool (37 and 45, never below the headphones' minimum); Standard is the
 * console's own (nothing is installed); High sends the two channels separately at about 490 kb/s (bitpool 38, one
 * 2-DH5 packet) and raises the packet cap of headphones limited to the 2 Mb/s radio rate; Highest (Experimental)
 * sends them separately at the headphones' highest bitpool (up to 59) in packets up to 3-DH5. Settings files written
 * before this order (0 Best, 1 Balanced, 2 Stable) are converted when loaded (kernel/main.c). */
enum BttQuality { BTT_Q_STABLE = 0, BTT_Q_BALANCED, BTT_Q_STANDARD, BTT_Q_HIGH, BTT_Q_HIGHEST, BTT_Q_COUNT };

/* What the console does when Bluetooth headphones disconnect. Mute is the console's own: it mutes itself and
 * stays silent until the volume is changed. Pause, Keep Volume does not mute; Play on Speaker does not mute
 * and goes on playing on the speaker. */
enum BttDisconnect { BTT_DISC_MUTE = 0, BTT_DISC_PAUSE, BTT_DISC_SPEAKER, BTT_DISC_COUNT };

enum BttSound { BTT_SOUND_ORIGINAL = 0, BTT_SOUND_WARM, BTT_SOUND_BRIGHT, BTT_SOUND_BASS, BTT_SOUND_LIVELY, BTT_SOUND_CUSTOM, BTT_SOUND_COUNT };

/* The Bluetooth name the console shows to other devices (the one string setting): up to BTT_NAME_MAX printable
 * ASCII characters, "" meaning the console's own. It applies the next time Bluetooth is switched on. */
#define BTT_NAME_MAX      16
#define BTT_NAME_DEFAULT  "PlayStation Vita"

/* 1 if `s` can be the name: empty, or at most BTT_NAME_MAX characters, every one printable ASCII (space to
 * tilde). The Settings page checks the text it is given with this and the kernel module checks it again. */
static inline int btt_name_valid(const char *s)
{
	int n = 0;

	for (; s && s[n]; n++)
		if (n >= BTT_NAME_MAX || s[n] < 0x20 || s[n] > 0x7E)
			return 0;
	return s != 0;
}

/* The battery level a Bluetooth headset reported to the console, for another module to show (the one call that
 * reads it is bttGetHeadsetBattery). The caller sets `size` to sizeof its own copy of the structure before the call;
 * the kernel module fills that many bytes (at most its own sizeof) and sets `size` to the number it wrote, so a later
 * version can add fields at the end and an older caller still works. */
typedef struct BttHeadsetBattery {
	unsigned int size;        /* in: the caller's sizeof (at least 8); out: the bytes written                        */
	unsigned int version;     /* BTT_BATT_VERSION                                                                    */
	int          level;       /* 0..100 percent, or BTT_BATT_UNKNOWN when the headset has not reported one           */
	unsigned int source;      /* BTT_BATT_SRC_*: how the level came, BTT_BATT_SRC_NONE when unknown                  */
	unsigned int age_ms;      /* milliseconds since the level arrived (0 when unknown)                               */
	unsigned int connected;   /* 1 while the headset's SOUND link (the headphones' audio connection with the console) is up. It is not the
	                           * hands-free link: a headset can be connected with no hands-free link, and then no level arrives             */
} BttHeadsetBattery;

#define BTT_BATT_VERSION   1
#define BTT_BATT_UNKNOWN   (-1)
#define BTT_BATT_SIZE_MIN  8                    /* size and version: the least a caller can pass                    */
enum BttBattSource { BTT_BATT_SRC_NONE = 0, BTT_BATT_SRC_HFIND = 1, BTT_BATT_SRC_VENDOR = 2 };
#define BTT_BATT_KEEP_S    600                  /* a level is kept this long after the sound link goes down, so a headset that is only away for a
                                                 * moment still shows its level. When the sound link comes back, the level is forgotten (level
                                                 * BTT_BATT_UNKNOWN) until the headset reports again, unless it reported in that same moment; a
                                                 * level can otherwise be as old as the headset's last report (age_ms says how old)              */

/* What the audio link to the headphones is now, for another module to show (the HUD, the Home screen, the quick menu, the status bar): the
 * codec, its bit rate or bitpool, whether the AAC encoder runs accelerated, the latency setting. The one call that reads it is bttGetLinkInfo.
 * Same rules as bttGetHeadsetBattery: the caller sets `size` to sizeof its own copy before the call; the kernel module fills that many bytes (at
 * most its own sizeof) and sets `size` to the number it wrote, so a later version can add fields at the end and an older caller still works.
 * Every field is 0 when it is not known: not connected, Bluetooth Turbo off, or the Bluetooth code not recognised. The SBC details are known
 * only while the stream is started and its encoder has been set up. */
typedef struct BttLinkInfo {
	unsigned int size;              /* in: the caller's sizeof (at least 8); out: the bytes written                                    */
	unsigned int version;           /* BTT_LINK_VERSION                                                                                */
	unsigned int connected;         /* 1 while headphones have an audio (A2DP) link with the console                                   */
	unsigned int streaming;         /* 1 while the stream is started, that is, sound is being sent                                     */
	unsigned int codec;             /* BTT_LINK_CODEC_*: the codec of the link (NONE when not connected)                               */
	unsigned int sample_rate;       /* Hz (48000 for both codecs), 0 when not connected                                                */
	unsigned int bitrate_kbps;      /* AAC: the bit rate in force, kb/s; SBC: 0 (see sbc_bitpool)                                      */
	unsigned int sbc_bitpool;       /* SBC: the highest bitpool the stream uses; 0 when not known                                      */
	unsigned int sbc_channel_mode;  /* SBC: BTT_LINK_CH_*; 0 when not known                                                            */
	unsigned int accel;             /* 1 while the AAC encoder runs the hardware accelerated (NEON) build                              */
	unsigned int latency;           /* the Bluetooth latency setting in force: a BttLatency (0 when Bluetooth Turbo is off)             */
	/* version 2: which device the link is with (the address as Sony's Bluetooth pages print it: 12 hex digits of addr_hi, then addr_lo) */
	unsigned int addr_lo;           /* the headphones' Bluetooth address, the low 32 bits; 0 when not connected                        */
	unsigned int addr_hi;           /* the high 16 bits (the upper half of the word is 0)                                              */
	/* version 3: the codecs the headphones offer (what they said when the stream was opened, kept per device) */
	unsigned int codec_mask;        /* BTT_HC_* bits of the codecs the headphones list; 0 when not known (not connected, or never read) */
	/* version 4: the radio link, read by the module's worker every few seconds while the link is up and smoothed. The first reading after a connection is
	 * nothing (the Bluetooth code only starts asking the controller then), so for a few seconds signal_level is NONE while connected is 1: "measuring". */
	unsigned int link_quality;      /* the controller's link quality, 0..255, higher is better; 0 while measuring or not connected                      */
	int          rssi;              /* the controller's signal strength, in dB relative to the receiver's ideal range (NOT dBm): 0 = inside the range, negative = below it,
	                                 * positive = above it; 0 while measuring or not connected (signal_level says which)                              */
	unsigned int signal_level;      /* BTT_LINK_SIGNAL_*: the two readings as a word; NONE while measuring or not connected                           */
	unsigned int voice_profile;     /* BTT_LINK_VOICE_*: the voice profile the headphones have connected to the console next to the sound link; NONE when none, or not connected */
} BttLinkInfo;

#define BTT_LINK_VERSION   4
#define BTT_LINK_SIZE_MIN  8                    /* size and version: the least a caller can pass */
/* BttLinkInfo.signal_level: the radio link's quality as a word (the worse of what link quality and signal strength each say; the thresholds are in kernel/btsignal_core.h). */
enum BttLinkSignal { BTT_LINK_SIGNAL_NONE = 0, BTT_LINK_SIGNAL_WEAK = 1, BTT_LINK_SIGNAL_FAIR = 2, BTT_LINK_SIGNAL_GOOD = 3, BTT_LINK_SIGNAL_EXCELLENT = 4 };
/* BttLinkInfo.voice_profile: the hands-free profile (the connection Bluetooth Turbo opens for the battery level and the microphone) or the headset profile (the console's own, simpler one). */
enum BttLinkVoice { BTT_LINK_VOICE_NONE = 0, BTT_LINK_VOICE_HFP = 1, BTT_LINK_VOICE_HSP = 2 };
enum BttLinkCodec { BTT_LINK_CODEC_NONE = 0, BTT_LINK_CODEC_SBC = 1, BTT_LINK_CODEC_AAC = 2, BTT_LINK_CODEC_APTX = 3, BTT_LINK_CODEC_LDAC = 4 };
/* The codecs a headset can list (BttLinkInfo.codec_mask): one bit each. A codec the headset lists that none of these names is BTT_HC_OTHER. Which of them
 * Bluetooth Turbo can send is the add-ons' business (BTT_KEY_AAC_AVAIL and its siblings); the rest are known only so they can be named. */
#define BTT_HC_SBC               (1u << 0)
#define BTT_HC_AAC               (1u << 1)
#define BTT_HC_APTX              (1u << 2)
#define BTT_HC_APTX_HD           (1u << 3)
#define BTT_HC_LDAC              (1u << 4)
#define BTT_HC_APTX_ADAPTIVE     (1u << 5)
#define BTT_HC_APTX_LL           (1u << 6)
#define BTT_HC_LHDC              (1u << 7)    /* LHDC whose version the listing does not say (a short element) */
#define BTT_HC_LHDC_LL           (1u << 8)    /* LHDC LL */
#define BTT_HC_SAMSUNG_SCALABLE  (1u << 9)    /* Samsung Scalable, also called Seamless */
#define BTT_HC_SAMSUNG_HD        (1u << 10)
#define BTT_HC_LC3PLUS           (1u << 11)
#define BTT_HC_OPUS              (1u << 12)
#define BTT_HC_MP3               (1u << 13)
#define BTT_HC_ATRAC             (1u << 14)
#define BTT_HC_OTHER             (1u << 15)
#define BTT_HC_LHDC_V1           (1u << 16)
#define BTT_HC_LHDC_V2           (1u << 17)
#define BTT_HC_LHDC_V3           (1u << 18)
#define BTT_HC_LHDC_V4           (1u << 19)
#define BTT_HC_LHDC_LLAC         (1u << 20)
#define BTT_HC_LHDC_V5           (1u << 21)
#define BTT_HC_FASTSTREAM        (1u << 22)
#define BTT_HC_APTX_TWS          (1u << 23)
enum BttLinkChannels { BTT_LINK_CH_UNKNOWN = 0, BTT_LINK_CH_MONO = 1, BTT_LINK_CH_DUAL = 2, BTT_LINK_CH_STEREO = 3, BTT_LINK_CH_JOINT = 4 };

/* ---- SceBluetoothTurbo syscalls (kernel/main.c, bluetoothturbo_kernel.yml) ---- */
int bttGet(int key);              /* value, or BTT_ERR_*                    */
int bttSet(int key, int value);   /* BTT_OK or BTT_ERR_*; saved and applied */
int bttGetName(char *out, int size);   /* the name in force (the console's own when none is set), as a string of at
                                        * most size - 1 characters; its length, or BTT_ERR_*                     */
int bttSetName(const char *name);      /* BTT_OK, BTT_ERR_RANGE for a name btt_name_valid refuses; "" = the console's
                                        * own; saved and applied                                                */
int bttGetHeadsetBattery(BttHeadsetBattery *out);   /* BTT_OK, or BTT_ERR_ARG for no buffer or a size under
                                        * BTT_BATT_SIZE_MIN. Read only, takes no lock. level is BTT_BATT_UNKNOWN until a
                                        * headset has reported one.                              */
int bttGetLinkInfo(BttLinkInfo *out);   /* BTT_OK, or BTT_ERR_ARG for no buffer or a size under BTT_LINK_SIZE_MIN. Read only,
                                        * takes no lock. A snapshot the module's worker keeps (twice a second; every 0.1 s while
                                        * an AAC stream is up); the signal fields, every 3 s.                                  */
int bttGetNotice(int lang, char *out, int size);   /* the text of the oldest waiting codec notice (the chosen codec could not be used and SBC is used: kernel/btfall_core.h) in system language
                                        * `lang` (0..19), UTF-8 into `out`; its length in bytes (the notice is taken in every case, also when it does not fit or the copy fails), 0 when none is waiting, BTT_ERR_ARG for no buffer,
                                        * BTT_ERR_RANGE when the text does not fit `size`. The Home screen module asks every two seconds and shows what it gets. */

#endif /* BLUETOOTHTURBO_H */
