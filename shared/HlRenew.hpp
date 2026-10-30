#pragma once

// A thing made anew where it is: in place of `v = T()`, which builds the new
// one on the stack first and copies it over. The stack is 15 KB of DTCM in
// all, and some of these are not small: ActorWorld 9 KB, NetMatch 13 KB
// (Game::destroy's overflowed it when a map was left, 2026-10-30). The build
// refuses a function that takes over 4 KB of it (-Werror=stack-usage).
//
// std-only.

#include <memory>

namespace hldata
{
    template <class T> void renew(T &v)
    {
        std::destroy_at(&v);
        std::construct_at(&v);
    }
} // namespace hldata
