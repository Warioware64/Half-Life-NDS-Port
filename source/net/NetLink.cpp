#include "net/NetLink.hpp"

#include <cstdio>
#include <cstring>

#include <nds.h>

#include <dswifi9.h>
#include <dswifi_dlplay.h>

#include "HlNet.hpp"

// All of dswifi is in this file. What it took Skyjo-DS to find out, kept:
//
//   - A mode change is asked for and happens later (the other processor does
//     it): nothing that depends on it is done before Wifi_LibraryModeReady().
//   - Going idle alone leaves the frame sizes of the last session set: a
//     second session as host then says the wrong ones in its beacon, and
//     nobody can join. Hence the pass through client mode in shutdown().
//   - Entering scan mode forgets the handler for the host's frames: it is
//     set again at every join.
//   - Asking how the association stands is what keeps one going.

namespace hlrt::netlink
{
    namespace
    {
        using hldata::kNetCmdBytes;
        using hldata::kNetPlayers;
        using hldata::kNetReplyBytes;

        constexpr int kModeWaitFrames = 90;
        constexpr const char *kSsid = "HALFLIFE";

        bool started = false;
        bool hosting = false;
        bool client = false;
        bool offering = false;

        // What has come, and has not been read: a ring that the interrupt
        // fills and the frame empties. (One writer, one reader: the indices
        // are each written by one side only.) One pool, in slots as big as
        // what comes: a client's, twelve of the host's frames; a host's,
        // the clients' smaller ones -- thirty-nine of them, room for what
        // seven answer to five frames of its own.
        struct Frame
        {
            std::uint16_t size;
            std::uint8_t from;
            std::uint8_t bytes[1]; ///< (as many as the slot has room for)
        };
        constexpr int kSlotHead = 4;
        constexpr int kPoolBytes = 12 * (kSlotHead + kNetCmdBytes);
        alignas(4) std::uint8_t pool[kPoolBytes];
        int slotBytes = kSlotHead + kNetCmdBytes;
        int slotCount = kPoolBytes / (kSlotHead + kNetCmdBytes);
        volatile int ringHead = 0; ///< the next to read
        volatile int ringTail = 0; ///< the next to write
        volatile unsigned ringDropped = 0;
        Frame &slot(int n) { return *reinterpret_cast<Frame *>(pool + n * slotBytes); }

        void keep(int from, int base, int len)
        {
            const int next = (ringTail + 1) % slotCount;
            if (len <= 0 || len > slotBytes - kSlotHead || next == ringHead)
            {
                if (next == ringHead)
                    ++ringDropped;
                return;
            }
            Frame &frame = slot(ringTail);
            Wifi_RxRawReadPacket(base, len, frame.bytes);
            frame.size = static_cast<std::uint16_t>(len);
            frame.from = static_cast<std::uint8_t>(from);
            ringTail = next;
        }
        // (In an interrupt: nothing here may allocate or print.)
        void fromClient(Wifi_MPPacketType type, int aid, int base, int len)
        {
            if (type == WIFI_MPTYPE_REPLY && aid >= 1 && aid < kNetPlayers)
                keep(aid, base, len);
        }
        void fromHost(Wifi_MPPacketType type, int base, int len)
        {
            if (type == WIFI_MPTYPE_CMD)
                keep(0, base, len);
        }

        void waitForMode()
        {
            for (int i = 0; i < kModeWaitFrames && !Wifi_LibraryModeReady(); ++i)
                swiWaitForVBlank();
        }
        bool start()
        {
            if (started)
                return true;
            if (!Wifi_InitDefault(INIT_ONLY | WIFI_LOCAL_ONLY))
                return false;
            started = true;
            return true;
        }
        void forget()
        {
            ringHead = ringTail = 0;
        }
        /// The pool in slots for frames of `bytes` (before the radio is
        /// told to give any).
        void shape(int bytes)
        {
            forget();
            slotBytes = (kSlotHead + bytes + 3) & ~3;
            slotCount = kPoolBytes / slotBytes;
        }
        Wifi_AccessPoint hostNamed(const std::uint8_t address[6])
        {
            Wifi_AccessPoint ap = {};
            std::memcpy(ap.bssid, address, sizeof(ap.bssid));
            ap.channel = static_cast<u8>(kChannel);
            std::strncpy(ap.ssid, kSsid, sizeof(ap.ssid) - 1);
            ap.ssid_len = static_cast<u8>(std::strlen(kSsid));
            return ap;
        }

        u16 dlDescription[DSWIFI_DLPLAY_DESC_LEN];
        std::uint8_t dlParam[DSWIFI_DLPLAY_USER_PARAM_SIZE];
    } // namespace

    int failStage()
    {
        return Wifi_GetInitFailStage();
    }

    void shutdown()
    {
        dlStop();
        if (started)
        {
            Wifi_MultiplayerFromClientSetPacketHandler(nullptr);
            Wifi_MultiplayerFromHostSetPacketHandler(nullptr);
            Wifi_DisconnectAP();
            Wifi_MultiplayerClientMode(8);
            Wifi_IdleMode();
            waitForMode();
        }
        hosting = client = false;
        forget();
    }

    void powerDown()
    {
        if (!started)
            return;
        shutdown();
        Wifi_DisableWifi();
        for (int i = 0; i < kModeWaitFrames; ++i)
        {
            if (Wifi_Deinit())
                break;
            swiWaitForVBlank();
        }
        started = false;
    }

    bool host(const char *name, int most)
    {
        if (!start())
            return false;
        if (hosting)
        {
            Wifi_MultiplayerAllowNewClients(true);
            return true;
        }
        shape(kNetReplyBytes);
        // (As many clients as the match lets play: the radio refuses the rest.)
        most = most < 2 ? 2 : (most > kNetPlayers ? kNetPlayers : most);
        if (Wifi_MultiplayerHostMode(most - 1, kNetCmdBytes, kNetReplyBytes) != 0)
            return false;
        Wifi_MultiplayerFromClientSetPacketHandler(fromClient);
        waitForMode();
        Wifi_SetChannel(kChannel);
        // (The beacon's name is UTF-16, a whole field of it: what clients
        // see in their list.)
        u16 wide[DSWIFI_BEACON_NAME_SIZE / sizeof(u16)] = {};
        int length = 0;
        for (; name[length] != 0 && length < static_cast<int>(sizeof(wide) / sizeof(wide[0])); ++length)
            wide[length] = static_cast<u8>(name[length]);
        Wifi_MultiplayerHostName(wide, static_cast<u8>(length));
        Wifi_MultiplayerAllowNewClients(true);
        Wifi_BeaconStart(kSsid, kGameId);
        hosting = true;
        client = false;
        return true;
    }

    unsigned clients()
    {
        return hosting ? (Wifi_MultiplayerGetClientMask() & (((1u << kNetPlayers) - 1) & ~1u)) : 0;
    }

    void hostRoom(const std::uint8_t *bytes, int size)
    {
        if (!hosting)
            return;
        // (The beacon made again with it: what is looking for a game sees
        // it at the next one.)
        Wifi_MultiplayerHostGameData(bytes, static_cast<size_t>(size));
        Wifi_BeaconStart(kSsid, kGameId);
    }

    bool hostRoomAt(int index, std::uint8_t *bytes, int &size, int &rssi)
    {
        size = 0;
        rssi = 0;
        if (!client || index < 0 || index >= Wifi_GetNumAP())
            return false;
        Wifi_AccessPoint ap;
        Wifi_GetAPData(index, &ap);
        if (ap.nintendo.game_id != kGameId)
            return false;
        size = ap.nintendo.game_data_len;
        std::memcpy(bytes, ap.nintendo.game_data, static_cast<std::size_t>(size));
        rssi = ap.rssi;
        return true;
    }

    int strength()
    {
        if (!client || !joined())
            return 0;
        // (The value is the answer: no buffer.)
        const int rssi = Wifi_GetData(WIFIGETDATA_RSSI, 0, nullptr);
        return rssi > 0 ? rssi : 0;
    }

    void admit(bool allow)
    {
        if (hosting)
            Wifi_MultiplayerAllowNewClients(allow);
    }

    void kick(int c)
    {
        if (hosting && c >= 1 && c < kNetPlayers)
            Wifi_MultiplayerKickClientByAID(c);
    }

    bool scan()
    {
        if (!start())
            return false;
        shape(kNetCmdBytes);
        if (Wifi_MultiplayerClientMode(kNetReplyBytes) != 0)
            return false;
        Wifi_MultiplayerFromHostSetPacketHandler(fromHost);
        waitForMode();
        Wifi_ScanMode();
        client = true;
        hosting = false;
        return true;
    }

    int found()
    {
        return client ? Wifi_GetNumAP() : 0;
    }

    bool hostAt(int index, char *name, int room, int *players, int *most)
    {
        if (!client || index < 0 || index >= Wifi_GetNumAP())
            return false;
        Wifi_AccessPoint ap;
        Wifi_GetAPData(index, &ap);
        if (ap.nintendo.game_id != kGameId || !ap.nintendo.allows_connections)
            return false;
        if (name != nullptr && room > 0)
        {
            // (The host's own name, as its beacon gives it: eight-bit here,
            // as Wifi_MultiplayerHostName was given it.)
            int n = 0;
            for (; n < room - 1 && n < static_cast<int>(ap.nintendo.name_len); ++n)
                name[n] = static_cast<char>(ap.nintendo.name[n]);
            name[n] = 0;
        }
        if (players != nullptr)
            *players = ap.nintendo.players_current;
        if (most != nullptr)
            *most = ap.nintendo.players_max;
        return true;
    }

    bool join(int index)
    {
        if (!client || index < 0 || index >= Wifi_GetNumAP())
            return false;
        Wifi_AccessPoint ap;
        Wifi_GetAPData(index, &ap);
        Wifi_MultiplayerFromHostSetPacketHandler(fromHost);
        Wifi_ConnectOpenAP(&ap);
        return true;
    }

    bool joinDirect(const std::uint8_t address[6], bool again)
    {
        if (!again)
        {
            if (!start())
                return false;
            shape(kNetCmdBytes);
            if (Wifi_MultiplayerClientMode(kNetReplyBytes) != 0)
                return false;
            Wifi_MultiplayerFromHostSetPacketHandler(fromHost);
            waitForMode();
            client = true;
            hosting = false;
        }
        else if (!client)
            return false;
        Wifi_AccessPoint ap = hostNamed(address);
        Wifi_ConnectOpenAP(&ap);
        return true;
    }

    bool joined()
    {
        return client && Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED;
    }

    bool refused()
    {
        return client && Wifi_AssocStatus() == ASSOCSTATUS_CANNOTCONNECT;
    }

    void send(const void *bytes, int size)
    {
        if (size <= 0)
            return;
        if (hosting)
            Wifi_MultiplayerHostCmdTxFrame(bytes, static_cast<std::size_t>(size));
        else if (client)
            Wifi_MultiplayerClientReplyTxFrame(bytes, static_cast<std::size_t>(size));
    }

    int take(std::uint8_t *into, int &from)
    {
        if (ringHead == ringTail)
            return 0;
        const Frame &frame = slot(ringHead);
        const int size = frame.size;
        std::memcpy(into, frame.bytes, static_cast<std::size_t>(size));
        from = frame.from;
        ringHead = (ringHead + 1) % slotCount;
        return size;
    }

    unsigned dropped()
    {
        return ringDropped;
    }

    void address(std::uint8_t out[6])
    {
        std::memset(out, 0, 6);
        if (start())
            Wifi_GetData(WIFIGETDATA_MACADDRESS, 6, out);
    }

    void ownerName(char *out, int room)
    {
        // The name the console's owner gave it; plain letters of it.
        int n = 0;
        for (int i = 0; i < PersonalData->nameLen && n < room - 1; ++i)
        {
            const u16 c = static_cast<u16>(PersonalData->name[i]);
            out[n++] = c >= 32 && c < 127 ? static_cast<char>(c) : '?';
        }
        if (n == 0 && room > 6)
        {
            std::strcpy(out, "Player");
            n = 6;
        }
        out[n] = 0;
    }

    // ----------------------------------------------------------- Download Play

    bool dlStart(const void *rom, std::size_t size, const char *description, const void *param)
    {
        if (!start())
            return false;
        if (offering)
            return true;
        shutdown();
        Wifi_DlPlaySetSmallFrame(false);
        const std::size_t length = std::strlen(description) > DSWIFI_DLPLAY_DESC_LEN ? DSWIFI_DLPLAY_DESC_LEN
                                                                                    : std::strlen(description);
        for (std::size_t i = 0; i < DSWIFI_DLPLAY_DESC_LEN; ++i)
            dlDescription[i] = i < length ? static_cast<u16>(description[i]) : 0;
        std::memcpy(dlParam, param, sizeof(dlParam));
        Wifi_DlPlayInfo info = {};
        info.description = dlDescription;
        info.description_len = static_cast<u8>(length);
        info.max_players = kNetPlayers;
        info.user_param = dlParam;
        if (Wifi_DlPlayStart(rom, size, &info) != 0)
            return false;
        // (After the start, which sets both back.) Nobody is started until
        // all are: the host cannot offer the program and run the game's own
        // radio at once. And nobody is sent it until all who are coming
        // have come (dlGather).
        Wifi_DlPlaySetBootMode(WIFI_DLPLAY_BOOT_MANUAL);
        Wifi_DlPlaySetGather(true);
        offering = true;
        return true;
    }

    void dlGather(bool hold)
    {
        if (offering)
            Wifi_DlPlaySetGather(hold);
    }

    void dlUpdate()
    {
        if (offering)
            Wifi_DlPlayUpdate();
    }

    void dlStop()
    {
        if (!offering)
            return;
        Wifi_DlPlayStop();
        offering = false;
    }

    bool dlActive()
    {
        return offering;
    }

    unsigned dlGuests()
    {
        return offering ? Wifi_DlPlayGetClientMask() : 0;
    }

    int dlProgress(int n)
    {
        if (!(dlGuests() & (1u << n)))
            return -1;
        if (Wifi_DlPlayGetClientState(n) == WIFI_DLPLAY_BOOTING)
            return 100;
        int current = 0, total = 0;
        Wifi_DlPlayGetClientProgress(n, &current, &total);
        return total <= 0 ? 0 : (current >= total ? 99 : (current * 100) / total);
    }

    bool dlChecking(int n)
    {
        return (dlGuests() & (1u << n)) && Wifi_DlPlayGetClientState(n) == WIFI_DLPLAY_VERIFYING;
    }

    bool dlFailed(int n)
    {
        return (dlGuests() & (1u << n)) && Wifi_DlPlayGetClientState(n) == WIFI_DLPLAY_ERROR;
    }

    const char *dlName(int n)
    {
        const char *name = (dlGuests() & (1u << n)) ? Wifi_DlPlayGetClientNameByAID(n) : nullptr;
        return name != nullptr ? name : "";
    }

    bool dlAllReady()
    {
        int guests = 0;
        for (int n = 1; n < 16; ++n)
        {
            // (One that failed is not waited for.)
            if (!(dlGuests() & (1u << n)) || Wifi_DlPlayGetClientState(n) == WIFI_DLPLAY_ERROR)
                continue;
            if (Wifi_DlPlayGetClientState(n) != WIFI_DLPLAY_BOOTING)
                return false;
            ++guests;
        }
        return guests > 0;
    }

    DlCounts dlCounts()
    {
        DlCounts counts;
        Wifi_MultiplayerHostCounters host = {};
        Wifi_MultiplayerGetHostCounters(&host);
        counts.armed = host.cmd_armed;
        counts.done = host.cmd_done;
        counts.retried = host.cmd_retry;
        counts.refused = Wifi_DlPlayGetDiag()->cmd_tx_failures;
        for (int n = 1; n < 16; ++n)
        {
            int current = 0, total = 0;
            if (dlGuests() & (1u << n))
                Wifi_DlPlayGetClientProgress(n, &current, &total);
            if (current > 0 && static_cast<unsigned>(current) > counts.blocks)
                counts.blocks = static_cast<unsigned>(current);
        }
        return counts;
    }

    void dlLock(bool locked)
    {
        if (offering)
            Wifi_DlPlayLockRoom(locked);
    }

    bool dlBootAll()
    {
        return offering && Wifi_DlPlayBootAll();
    }
} // namespace hlrt::netlink
