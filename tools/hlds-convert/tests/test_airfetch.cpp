// What a console asks the host for over the radio (HlAirBank.hpp): that it
// comes, whole and right, whatever is lost on the way -- and that one who
// goes away, or is sent something wrong, costs nobody else anything.

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "HlAirBank.hpp"
#include "HlNetGame.hpp"
#include "HlLevel.hpp"
#include "HlLevelReader.hpp"
#include "TestGame.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    Bytes made(std::size_t size, unsigned seed)
    {
        Bytes out(size);
        for (std::size_t i = 0; i < size; ++i)
            out[i] = static_cast<std::uint8_t>((i * 31 + seed * 17 + (i >> 7)) & 0xFF);
        return out;
    }

    struct Guest
    {
        FetchTaking taking;
        Bytes room;
        bool done = false, tooBad = false;
    };

    /// One thing, sent to `count` guests: each frame some pieces, each lost
    /// to each guest `loss` times in a hundred; bad ones are `bad` in a
    /// hundred. How many frames it took (-1: it never came).
    int send(const Bytes &file, int count, int loss, int bad, int leaver, std::vector<Guest> &guests, FetchHost &host)
    {
        const std::uint32_t crc = crc32(file.data(), static_cast<std::uint32_t>(file.size()));
        guests.assign(static_cast<std::size_t>(count), Guest());
        for (int g = 0; g < count; ++g)
        {
            guests[static_cast<std::size_t>(g)].taking.want(FetchFrom::Map, 77);
            check(host.ask(g + 1, FetchFrom::Map, 77), "asked");
        }
        int who = 0;
        FetchHost::Asked asked;
        if (host.nextAsked(who, asked))
            host.begin(who, asked, 77, static_cast<std::uint32_t>(file.size()), static_cast<std::uint32_t>(file.size()), crc, 0);
        for (int frame = 0; frame < 20000; ++frame)
        {
            bool all = true;
            for (int g = 0; g < count; ++g)
                all = all && (guests[static_cast<std::size_t>(g)].done || g == leaver);
            if (all)
                return frame;
            for (int n = 0; n < 3; ++n)
            {
                FetchHost::Sending *s = nullptr;
                std::uint32_t index = 0;
                if (!host.next(s, index))
                    break;
                const std::uint32_t at = index * kFetchPiece;
                const int size = file.size() - at < kFetchPiece ? static_cast<int>(file.size() - at) : kFetchPiece;
                FetchHead head = s->head;
                head.index = static_cast<std::uint16_t>(index);
                head.count = size;
                std::uint8_t bytes[kNetCmdBytes];
                Bytes piece(file.begin() + at, file.begin() + at + size);
                const int length = fetchWrite(head, piece.data(), bytes);
                check(length > 0 && length <= kNetCmdBytes, "a piece fits a frame");
                for (int g = 0; g < count; ++g)
                {
                    Guest &guest = guests[static_cast<std::size_t>(g)];
                    if (guest.done || g == leaver || std::rand() % 100 < loss)
                        continue;
                    Bytes heard(bytes, bytes + length);
                    if (std::rand() % 100 < bad)
                        heard[kFetchHeadBytes + 1] = static_cast<std::uint8_t>(heard[kFetchHeadBytes + 1] ^ 0x55);
                    FetchHead read;
                    if (!fetchRead(heard.data(), length, read))
                        continue;
                    if (guest.taking.needsRoom(read))
                    {
                        guest.taking.begin(read);
                        guest.room.assign(read.packed, 0);
                    }
                    guest.taking.piece(read, guest.room.data());
                }
            }
            for (int g = 0; g < count; ++g)
            {
                Guest &guest = guests[static_cast<std::size_t>(g)];
                if (guest.done || g == leaver)
                    continue;
                if (guest.taking.whole())
                {
                    if (crc32(guest.room.data(), static_cast<std::uint32_t>(guest.room.size())) == guest.taking.crc)
                    {
                        guest.done = true;
                        host.have(g + 1, guest.taking.id, guest.taking.base, 0);
                        continue;
                    }
                    // Bad: all of it again, from nothing.
                    guest.taking.base = 0;
                    guest.taking.have = 0;
                    host.again(g + 1, guest.taking.id);
                }
                // (A few times a second, its word may be lost too.)
                else if (guest.taking.known && frame % 12 == 0 && std::rand() % 100 >= loss)
                    host.have(g + 1, guest.taking.id, guest.taking.base, guest.taking.have);
            }
            if (leaver >= 0 && frame == 40)
                host.forget(leaver + 1);
            host.frame();
        }
        return -1;
    }
} // namespace

int main()
{
    std::srand(7);
    const Bytes file = made(90000, 3); // (150 pieces: more than a window)

    {
        FetchHost host;
        std::vector<Guest> guests;
        const int clean = send(file, 1, 0, 0, -1, guests, host);
        check(clean > 0 && guests[0].room == file, "one console gets it, whole");
        check(clean < 90, "... with hardly a frame over what it takes to send");
        check(!host.busy(), "and the host has let it go");
    }
    for (const int loss : {5, 20, 50})
    {
        FetchHost host;
        std::vector<Guest> guests;
        const int frames = send(file, 3, loss, 0, -1, guests, host);
        bool right = frames > 0;
        for (const Guest &guest : guests)
            right = right && guest.room == file;
        check(right, "three share it, whatever is lost");
        check(!host.busy(), "... and it is let go of once all have it");
    }
    {
        FetchHost host;
        std::vector<Guest> guests;
        const int frames = send(file, 3, 10, 0, 1, guests, host);
        check(frames > 0 && guests[0].room == file && guests[2].room == file, "one who goes away costs the others nothing");
    }
    {
        FetchHost host;
        std::vector<Guest> guests;
        const int frames = send(made(3000, 4), 1, 0, 1, -1, guests, host);
        const Bytes small = made(3000, 4);
        check(frames > 0 && guests[0].room == small, "a piece that came wrong is found out, and it is sent again");
    }
    {
        // Silent: let go of, and nothing goes on being sent.
        FetchHost host;
        check(host.ask(1, FetchFrom::People, 5), "asks");
        int who = 0;
        FetchHost::Asked asked;
        check(host.nextAsked(who, asked) && who == 1, "its turn");
        host.begin(who, asked, 5, 6000, 6000, 0, 0);
        check(host.busy(), "sent");
        for (int n = 0; n <= kFetchSilentFrames + 1; ++n)
            host.frame();
        check(!host.busy(), "one who says nothing for long enough is let go of");
    }
    {
        // Turn by turn: one console's long list doesn't keep another waiting.
        FetchHost host;
        for (std::uint32_t t = 1; t <= 4; ++t)
            host.ask(1, FetchFrom::Map, t);
        host.ask(2, FetchFrom::Map, 100);
        int who = 0;
        FetchHost::Asked asked;
        check(host.nextAsked(who, asked) && who == 1, "the first");
        host.begin(who, asked, 1, 600, 600, 0, 0);
        check(host.nextAsked(who, asked) && who == 2, "then the other's, before the first's next");
        host.begin(who, asked, 100, 600, 600, 0, 0);
        check(!host.nextAsked(who, asked), "two at once at most");
    }
    {
        std::uint8_t bytes[kNetCmdBytes];
        std::uint8_t data[kFetchPiece] = {1, 2, 3};
        FetchHead head;
        head.id = 9;
        head.from = FetchFrom::PictureFull;
        head.what = 3;
        head.name = 0x12345678;
        head.packed = 24588;
        head.raw = 24588;
        head.crc = 99;
        head.index = 40;
        head.count = kFetchPiece;
        const int size = fetchWrite(head, data, bytes);
        check(size == kFetchHeadBytes + kFetchPiece && size <= kNetCmdBytes, "the whole of a piece fits a frame");
        FetchHead back;
        check(fetchRead(bytes, size, back) && back.id == 9 && back.from == FetchFrom::PictureFull && back.what == 3 &&
                  back.name == 0x12345678 && back.index == 40 && back.data[2] == 3,
              "and reads back");
        bytes[3] = 1;
        check(!fetchRead(bytes, size, back), "a frame of another kind is not one");
    }
    {
        // A client with much to say: a frame the radio carries (a larger one
        // is lost whole, and the next the same, for ever), the rest after.
        NetClient client;
        NetPlayer mine;
        for (int n = 0; n < 40; ++n)
            client.out.add(netEvent(NetEventType::Room));
        std::uint8_t reply[kNetReplyBytes];
        const int size = netClientWrite(client, mine, reply);
        check(size > 0 && size <= 193, "what a client says goes in frames the radio carries");
        NetHost host;
        netHostJoin(host, 1);
        check(netHostRead(host, 1, reply, size) && host.clients[1].in.count == kNetClientEvents, "and all of it is heard");
    }
    if (g_failures == 0)
        std::printf("test_airfetch: ok\n");
    return g_failures == 0 ? 0 : 1;
}
