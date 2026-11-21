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
#define BTT_VER_MINOR   1
#define BTT_VER_PATCH   0
#define BTT_VER_NUM     ((BTT_VER_MAJOR << 16) | (BTT_VER_MINOR << 8) | BTT_VER_PATCH)
#define BTT_STR_(x)     #x
#define BTT_STR(x)      BTT_STR_(x)
#define BTT_VERSION     BTT_STR(BTT_VER_MAJOR) "." BTT_STR(BTT_VER_MINOR) "." BTT_STR(BTT_VER_PATCH)

#define BTT_DIR         "ur0:data/bluetoothturbo"
#define BTT_SETTINGS    BTT_DIR "/settings.bin"
#define BTT_KLOG_PATH   BTT_DIR "/bluetoothturbo_kernel.log"

/* Errors (negative), in the same style as the other Turbo plugins'. */
#define BTT_OK           0
#define BTT_ERR_ARG     -1
#define BTT_ERR_RANGE   -2

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
	BTT_KEY_QUALITY,     /* BttQuality: Bluetooth Audio Quality, applied from the next connection of the headphones */
	BTT_KEY_DISCONNECT,  /* BttDisconnect: what the console does when Bluetooth headphones disconnect */
	BTT_KEY_RECONNECT,   /* 0/1: reconnect the headphones last played on after the console wakes from standby */
	BTT_KEY_HFP,         /* 0..4: the hands-free connection's internal mode: 0 off, 4 the whole of it. No Settings row of its own: the
	                      * row BTT_KEY_READBATT sets it. Never read from the file (every start takes it from BTT_KEY_READBATT, which
	                      * the master switch gates); it keeps its place in the file                                              */
	BTT_KEY_LOWBATT,     /* 0/1: notify when the headset's battery runs low (read by the Home screen module, bluetoothturbo.suprx) */
	BTT_KEY_READBATT,    /* 0/1: the hands-free connection to a headset (its battery level and its microphone). On sets BTT_KEY_HFP to its
	                      * whole mode, off to 0; a file written before this key has it on                                               */
	BTT_KEY_COUNT,

	/* Write only, not stored. */
	BTT_KEY_SETTINGS_VER = 100,  /* value BTT_VER_NUM of the Settings module: the worker logs
	                              * it once, flagged if it differs from the kernel module's own */
	BTT_KEY_PAGE_SKIP = 101,     /* a BttPageSkip reason: the Settings module saw the Devices page and did not add its
	                              * entry; the worker logs each reason once */
};

/* Why the Settings module left the Devices page as it was (the values of BTT_KEY_PAGE_SKIP). */
enum BttPageSkip { BTT_SKIP_ANCHOR = 1, BTT_SKIP_SIZE, BTT_SKIP_ALLOC, BTT_SKIP_COUNT };

/* Custom sound bands: 2 dB steps from -12 to +12 dB. */
#define BTT_BAND_COUNT  5
#define BTT_BAND_STEPS  13
#define BTT_BAND_FLAT   6
#define BTT_BAND_DB(v)  (((v) - BTT_BAND_FLAT) * 2)

/* Bluetooth latency: the number of sound packets the sender lets queue for the radio before it
 * backs off (stock 23, about 245 ms). */
enum BttLatency { BTT_LAT_STANDARD = 0, BTT_LAT_LOW, BTT_LAT_LOWEST, BTT_LAT_COUNT };

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
	unsigned int connected;   /* 1 while the headset's link is up                                                    */
} BttHeadsetBattery;

#define BTT_BATT_VERSION   1
#define BTT_BATT_UNKNOWN   (-1)
#define BTT_BATT_SIZE_MIN  8                    /* size and version: the least a caller can pass                    */
enum BttBattSource { BTT_BATT_SRC_NONE = 0, BTT_BATT_SRC_HFIND = 1, BTT_BATT_SRC_VENDOR = 2 };
#define BTT_BATT_KEEP_S    600                  /* a level is kept this long after the headset's link goes down     */

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

#endif /* BLUETOOTHTURBO_H */
