// (A console that was sent the game only: HLDS_CHILD.)
#ifdef HLDS_CHILD
#include "net/GuestLoad.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

#include <nds.h>

#include "Global.hpp"
#include "net/AirDevice.hpp"
#include "net/NetLink.hpp"

namespace hlrt::guest
{
    namespace
    {
        struct Kept
        {
            char name[hldata::kGuestNameMost + 1];
            std::uint8_t *data;
            std::uint32_t size;
        };
        constexpr int kBaseMost = 160;
        Kept base[kBaseMost];
        int baseCount = 0;

        struct Offer
        {
            std::string map;
            const std::uint8_t *data = nullptr;
            std::uint32_t size = 0;
            std::string path; ///< or, the file on a card it is in
            bool air = false; ///< or, the host is sending it
        };
        constexpr int kOffersMost = 4;
        Offer offers[kOffersMost];
        int offerCount = 0;

        // The map's stream, while it is being read: one at a time.
        MemorySupply mapSupply;
        FileSupply mapFile;
        hldata::GuestCursor *mapCursor = nullptr;
        alignas(4) std::uint8_t cursorRoom[sizeof(hldata::GuestCursor)];
        Held heldNow;

        // The stream being taken from the air: its pieces in a window of
        // their own, on the heap only while one is (38 KB).
        hldata::AirReceiver receiver;
        bool airLost = false;
        bool mapOnAir = false; ///< the map's cursor reads from the air
        char heldBase[hldata::kAirNameBytes] = {};
        /// The map last taken whole (to say so, if the host is still
        /// sending it to others), and its sending.
        char takenMap[hldata::kAirNameBytes] = {};
        std::uint16_t takenCharacter = 0;
        std::uint16_t takenServe = 0;
        std::uint32_t takenLength = 0;
        std::uint8_t takenSkill = 2;
        /// How long (vblanks) a guest waits for the host to say anything.
        constexpr std::uint32_t kAirQuietVblanks = 600;
        constexpr std::uint32_t kAirStoppedVblanks = 240;
        std::uint32_t pumpedAt = 0; ///< when the radio was last listened to
        /// ... and how many times it says it has all of a stream once it
        /// has, before it goes back to the game's frames (one lost answer
        /// would otherwise leave the host waiting for it).
        constexpr int kAirLingerVblanks = 30;

        /// When the stream was last heard of, and when the host was last
        /// answered.
        volatile std::uint32_t quietSince = 0;
        std::uint32_t askedAt = 0;
        void (*mapProgress)(int) = nullptr;
        std::uint32_t streamSince = 0; ///< (when the stream being taken began)

        /// (The receiver is the interrupt's as well as this code's -- sinkTake
        /// -- so what is done to it here is done with interrupts held.)
        struct Hold
        {
            int ime = enterCriticalSection();
            ~Hold() { leaveCriticalSection(ime); }
        };

        volatile bool heardInIrq = false; ///< a piece taken by sinkTake since pump last looked

        /// What the radio's interrupt does with a frame of the host's: a
        /// piece of the stream being taken goes where it belongs, at once
        /// (a loader busy for a few frames would otherwise see the radio's
        /// ring overflow, and the piece sent again). Anything else -- the
        /// game's, another stream's, this one's before the host has been
        /// heard sending it -- to the ring, for whoever reads it.
        bool sinkTake(const std::uint8_t *bytes, int size)
        {
            if (receiver.ring == nullptr || !receiver.known || size < 4 || bytes[3] != hldata::kNetFrameAirData)
                return false;
            hldata::AirHead head;
            if (!hldata::airRead(bytes, size, head) || head.stream != receiver.stream || head.serve != receiver.serve)
                return false;
            receiver.frame(head);
            quietSince = Global::vblanks();
            heardInIrq = true;
            return true;
        }

        bool ringOn(hldata::AirStream stream, const char *name)
        {
            streamSince = Global::vblanks();
            auto *ring = static_cast<std::uint8_t *>(std::malloc(hldata::kAirWindow * hldata::kAirPiece));
            std::uint8_t *was;
            {
                const Hold hold;
                was = receiver.ring;
                receiver.want(stream, name, ring);
            }
            std::free(was);
            airLost = false;
            netlink::setSink(sinkTake);
            return ring != nullptr;
        }
        void ringOff()
        {
            std::uint8_t *was;
            {
                const Hold hold;
                was = receiver.ring;
                receiver.ring = nullptr;
            }
            std::free(was);
        }

        /// The host's frames, as many as have come: the stream's pieces
        /// kept, the rest let go (the game's: they are sent again). An
        /// answer once a vblank. False once the host has been quiet too long.
        bool pump()
        {
            // (Quiet is counted only while listening: a console busy elsewhere
            // -- a map's game being set up -- has its window full, and the
            // host has nothing to send it until it reads on.)
            const std::uint32_t now = Global::vblanks();
            if (now - pumpedAt > 2)
            {
                // (No later than now: heard while busy, it is sooner than the gap.)
                const std::uint32_t since = now - quietSince, busy = now - pumpedAt;
                quietSince = since > busy ? quietSince + busy : now;
            }
            pumpedAt = now;
            std::uint8_t frame[hldata::kNetCmdBytes];
            int from = 0;
            bool heard = false;
            for (int size; (size = netlink::take(frame, from)) > 0;)
            {
                heard = true;
                hldata::AirHead head;
                const Hold hold;
                if (hldata::airRead(frame, size, head) && receiver.frame(head))
                    quietSince = Global::vblanks();
            }
            heard = heard || heardInIrq;
            heardInIrq = false;
            // (An answer for a frame of the host's that has come, the latest
            // there is: answers sent with nothing to answer wait in a queue,
            // and what reaches the host is then old news.)
            if (heard && askedAt != Global::vblanks())
            {
                askedAt = Global::vblanks();
                std::uint8_t ask[hldata::kNetReplyBytes];
                int size;
                {
                    const Hold hold;
                    size = receiver.ask(ask);
                }
                if (size > 0)
                    netlink::send(ask, size);
            }
            // (Quiet too long -- or the host heard, but no longer sending: it
            // gave up, and said so in what is not read here.)
            const std::uint32_t quiet = Global::vblanks() - quietSince;
            if (quiet > kAirQuietVblanks || (heard && receiver.known && quiet > kAirStoppedVblanks))
                airLost = true;
            return !airLost;
        }
        /// It is all here: said for a while.
        void linger()
        {
            for (int n = 0; n < kAirLingerVblanks; ++n)
            {
                pump();
                swiWaitForVBlank();
            }
        }

        /// The stream from the air, for a GuestCursor: waits (the radio
        /// heard all the while) for the next piece.
        struct AirSupply : hldata::GuestSupply
        {
            void (*progress)(int) = nullptr;
            int shown = -1;
            std::uint32_t shownAt = 0;
            bool more(const std::uint8_t *&out, std::size_t &length) override
            {
                // (Heard and answered once a vblank as the loaders read, not
                // only when they are waiting: a loader that has a window's
                // worth to go through would otherwise say nothing meanwhile,
                // and be left out.)
                if (askedAt != Global::vblanks() && !pump())
                    return false;
                while (true)
                {
                    // (How far, said as it changes -- no more than ten times
                    // a second, and the end always: the page is drawn again
                    // each time, and that is not free.)
                    const int percent = receiver.percent();
                    if (progress != nullptr && percent != shown &&
                        (shown < 0 || percent >= 100 || Global::vblanks() - shownAt >= 6))
                    {
                        shownAt = Global::vblanks();
                        progress(shown = percent);
                    }
                    bool got;
                    {
                        const Hold hold;
                        got = receiver.next(out, length);
                    }
                    if (got)
                        return true;
                    if (receiver.done() || !pump())
                        return false;
                    swiWaitForVBlank();
                }
            }
        };
        AirSupply airSupply;

        /// Every member of a stream, each into a block of its own, handed to
        /// `keep` (which owns it from then on, or frees it). (Its cursor in
        /// the map's room, not on the stack: its window is 4 KB. No map's
        /// stream is being read while one of these is.)
        template <class Keep>
        bool receiveWhole(hldata::GuestSupply &supply, Keep keep)
        {
            endMap();
            hldata::GuestCursor &cursor = *new (cursorRoom) hldata::GuestCursor(supply);
            bool ok = cursor.start();
            while (ok && cursor.next())
            {
                const std::uint32_t size = cursor.member().rawSize;
                auto *block = static_cast<std::uint8_t *>(std::malloc(size > 0 ? size : 1));
                if (block == nullptr || cursor.read(block, size) != size)
                {
                    std::free(block);
                    ok = false;
                    break;
                }
                keep(cursor.name(), block, size);
            }
            ok = ok && cursor.ended();
            cursor.~GuestCursor();
            return ok;
        }
    } // namespace

    bool FileSupply::more(const std::uint8_t *&out, std::size_t &length)
    {
        if (file == nullptr)
            return false;
        length = std::fread(piece, 1, sizeof(piece), file);
        out = piece;
        return length > 0;
    }

    bool MemorySupply::more(const std::uint8_t *&out, std::size_t &length)
    {
        if (at >= size)
            return false;
        out = data + at;
        length = size - at < piece ? size - at : piece;
        at += static_cast<std::uint32_t>(length);
        return true;
    }

    bool receiveCommon(hldata::GuestSupply &supply)
    {
        return receiveWhole(supply, [](const char *name, std::uint8_t *block, std::uint32_t size)
                            {
                                if (!air::putOwned(name, block, size))
                                    std::free(block);
                            });
    }

    void dropCommon() { air::dropOwned(); }

    bool receiveBase(hldata::GuestSupply &supply)
    {
        return receiveWhole(supply, [](const char *name, std::uint8_t *block, std::uint32_t size)
                            {
                                if (baseCount == kBaseMost)
                                {
                                    std::free(block);
                                    return;
                                }
                                Kept &kept = base[baseCount++];
                                std::strncpy(kept.name, name, sizeof(kept.name) - 1);
                                kept.name[sizeof(kept.name) - 1] = '\0';
                                kept.data = block;
                                kept.size = size;
                                heldNow.baseBytes += size;
                            });
    }

    bool borrow(const char *name, const std::uint8_t *&data, std::uint32_t &size)
    {
        for (int i = 0; i < baseCount; ++i)
        {
            if (std::strcmp(base[i].name, name) == 0)
            {
                data = base[i].data;
                size = base[i].size;
                return true;
            }
        }
        return false;
    }

    void offerMap(const std::string &map, const std::uint8_t *data, std::uint32_t size)
    {
        if (offerCount < kOffersMost)
            offers[offerCount++] = Offer{map, data, size, std::string()};
    }

    void offerMapFile(const std::string &map, const std::string &path)
    {
        if (offerCount < kOffersMost)
            offers[offerCount++] = Offer{map, nullptr, 0, path};
    }

    bool offered(const std::string &map)
    {
        for (int i = 0; i < offerCount; ++i)
        {
            if (offers[i].map == map)
                return true;
        }
        return false;
    }

    int offeredCount() { return offerCount; }
    std::string offeredName(int index) { return index >= 0 && index < offerCount ? offers[index].map : std::string(); }

    bool beginMap(const std::string &map)
    {
        endMap();
        // (The last map's weapons, kept packed, go with it.)
        air::dropMapFiles();
        for (int i = 0; i < offerCount; ++i)
        {
            if (offers[i].map != map)
                continue;
            hldata::GuestSupply *supply = &mapSupply;
            if (offers[i].air)
            {
                // (Begun when the host's first frame of it was heard:
                // airFrame.)
                if (receiver.stream != hldata::AirStream::Map || receiver.ring == nullptr ||
                    map != receiver.name)
                    return false;
                airSupply.progress = mapProgress;
                airSupply.shown = -1;
                supply = &airSupply;
                mapOnAir = true;
            }
            else if (!offers[i].path.empty())
            {
                mapFile.file = std::fopen(offers[i].path.c_str(), "rb");
                if (mapFile.file == nullptr)
                    return false;
                supply = &mapFile;
            }
            else
            {
                mapSupply = MemorySupply();
                mapSupply.data = offers[i].data;
                mapSupply.size = offers[i].size;
            }
            mapCursor = new (cursorRoom) hldata::GuestCursor(*supply);
            if (!mapCursor->start())
            {
                endMap();
                return false;
            }
            for (std::uint32_t &bytes : heldNow.mapBytes)
                bytes = 0;
            air::stream(mapCursor);
            return true;
        }
        return false;
    }

    void endMap()
    {
        air::drain();
        air::stream(nullptr);
        if (mapCursor != nullptr)
            mapCursor->~GuestCursor();
        mapCursor = nullptr;
        if (mapFile.file != nullptr)
            std::fclose(mapFile.file);
        mapFile.file = nullptr;
        // A map from the air: all of it here, which the host is told. (Not
        // one only begun to be heard: the map about to be read from it lets
        // go of whatever was mounted first.)
        if (mapOnAir && receiver.stream == hldata::AirStream::Map && receiver.ring != nullptr)
        {
            // (The last piece read let go of: then it is all handed on --
            // and what the cursor did not ask for, there being no more of
            // it, waited for, as receiveAir does. Left unheard, the host would
            // go on sending it for ever.)
            const std::uint8_t *rest;
            std::size_t size;
            while (!receiver.done() && airSupply.more(rest, size))
            {
            }
            if (receiver.done())
            {
                std::strncpy(takenMap, receiver.name, sizeof(takenMap) - 1);
                takenServe = receiver.serve;
                takenLength = receiver.length;
                takenSkill = receiver.skill;
                linger();
            }
            ringOff();
            receiver.stream = hldata::AirStream::None;
        }
        mapOnAir = false;
    }

    void airWant(hldata::AirStream stream)
    {
        ringOn(stream, "");
        quietSince = Global::vblanks();
    }

    bool airFrame(const std::uint8_t *bytes, int size, std::uint8_t *reply, int &replySize, hldata::AirHead &head)
    {
        replySize = 0;
        if (!hldata::airRead(bytes, size, head))
            return false;
        // What is held already: said so, and nothing taken.
        if (head.stream == hldata::AirStream::Base && std::strcmp(head.name, heldBase) == 0)
        {
            replySize = hldata::airHave(head.stream, head.serve, head.length, reply);
            return false;
        }
        if (head.stream == hldata::AirStream::Map && std::strcmp(head.name, takenMap) == 0 && head.serve == takenServe)
        {
            replySize = hldata::airHave(head.stream, head.serve, head.length, reply);
            return false;
        }
        // Who another chose to be: taken only if there is room for it, twice
        // over for the moment it is read -- else said to be held, and they
        // stay the suit here. (Taken already: the same.)
        if (head.stream == hldata::AirStream::Character)
        {
            if (head.serve == takenCharacter || global.heapHeadroom() < head.length * 2 + hldata::kGuestPlayBytes)
            {
                replySize = hldata::airHave(head.stream, head.serve, head.length, reply);
                return false;
            }
        }
        // (The common stream, to a console that came later: this one has it.)
        else if (head.stream != hldata::AirStream::Base && head.stream != hldata::AirStream::Map)
            return false;
        if (!ringOn(head.stream, head.name))
            return false;
        {
            const Hold hold;
            receiver.frame(head);
        }
        quietSince = Global::vblanks();
        return true;
    }

    bool receiveAir(void (*progress)(int percent))
    {
        if (receiver.ring == nullptr)
            return false;
        airSupply.progress = progress;
        airSupply.shown = -1;
        bool ok = false;
        if (receiver.stream == hldata::AirStream::Common)
            ok = receiveCommon(airSupply);
        else if (receiver.stream == hldata::AirStream::Character)
        {
            takenCharacter = receiver.serve;
            ok = receiveWhole(airSupply, [](const char *name, std::uint8_t *block, std::uint32_t size)
                              {
                                  if (!air::putMapFile(name, block, size))
                                      std::free(block);
                              });
        }
        else if (receiver.stream == hldata::AirStream::Base)
        {
            // (One base at a time: the last group's goes.)
            for (int i = 0; i < baseCount; ++i)
                std::free(base[i].data);
            baseCount = 0;
            heldBase[0] = '\0';
            heldNow.baseBytes = 0;
            ok = receiveBase(airSupply);
            if (ok)
                std::strncpy(heldBase, receiver.name, sizeof(heldBase) - 1);
        }
        // (What the cursor did not ask for, there being no more of it.)
        const std::uint8_t *rest;
        std::size_t size;
        while (ok && !receiver.done() && airSupply.more(rest, size))
        {
        }
        ok = ok && receiver.done();
        if (ok)
            linger();
        ringOff();
        if (ok)
            receiver.stream = hldata::AirStream::None;
        return ok;
    }

    void offerAir(const std::string &map)
    {
        for (int i = 0; i < offerCount; ++i)
        {
            if (offers[i].air)
            {
                offers[i].map = map;
                return;
            }
        }
        if (offerCount < kOffersMost)
            offers[offerCount++] = Offer{map, nullptr, 0, std::string(), true};
    }

    void onMapProgress(void (*progress)(int percent)) { mapProgress = progress; }

    void forgetTaken()
    {
        takenMap[0] = 0;
        takenCharacter = 0;
    }

    void hear()
    {
        if (mapOnAir && receiver.ring != nullptr)
            pump();
    }

    std::uint32_t airLength() { return receiver.known ? receiver.length : 0; }

    int airRate()
    {
        const std::uint32_t frames = Global::vblanks() - streamSince;
        return frames < 30 ? 0 : static_cast<int>(static_cast<std::uint64_t>(receiver.given) * hldata::kAirPiece * 60 / 1024 / frames);
    }

    const char *baseGroup() { return heldBase; }

    void airStatus(char *line, int room)
    {
        std::snprintf(line, static_cast<std::size_t>(room), "%s %lu/%lu of %lu%s", receiver.known ? "had" : "heard none",
                      static_cast<unsigned long>(receiver.given), static_cast<unsigned long>(receiver.base),
                      static_cast<unsigned long>(receiver.pieces()), airLost ? ", host quiet" : "");
    }
    const char *airMap() { return takenMap; }
    int airSkill() { return takenSkill; }

    void count(hldata::GuestKind kind, std::uint32_t bytes) { heldNow.mapBytes[static_cast<int>(kind)] += bytes; }

    const Held &held() { return heldNow; }
} // namespace hlrt::guest
#endif
