#include <cstdint>
#include <iostream>
#include <cassert>
#include "world/coord.hpp"

int main() {
    // --- Coord8 ---
    Geo::Coord8 c8{5, -3};
    if (c8.x() != 5) return 1;
    if (c8.y() != -3) return 2;

    auto c8add = c8 + Geo::Coord8{2, 4};
    if (c8add.x() != 7 || c8add.y() != 1) return 3;

    auto c8sub = c8 - Geo::Coord8{2, 4};
    if (c8sub.x() != 3 || c8sub.y() != -7) return 4;

    auto cneg = -c8;
    if (cneg.x() != -5 || cneg.y() != 3) return 5;

    if (Geo::Coord8::manhattan({0,0}, {3,4}) != 7) return 6;
    if (Geo::Coord8::chebyshev({0,0}, {3,4}) != 4) return 7;
    if (c8.dist_sq({0,0}) != 34) return 8;
    if (c8.neighbour(Geo::Dir::East) != Geo::Coord8{6, -3}) return 9;
    if (c8.neighbour8(0) != Geo::Coord8{6, -3}) return 10;   // east
    if (c8.neighbour8(1) != Geo::Coord8{6, -2}) return 11;   // se
    if (c8.neighbour8(6) != Geo::Coord8{5, -4}) return 12;   // north

    // scalar mult (commutative both ways)
    Geo::Coord8 sc{5, 10};
    auto sml = sc * 2;
    auto smr = 2 * sc;
    if (sml.x() != 10 || sml.y() != 20) return 13;
    if (smr.x() != 10 || smr.y() != 20) return 14;

    // division & mod
    Geo::Coord8 q{12, 20};
    auto qr = q / 4;
    if (qr.x() != 3 || qr.y() != 5) return 15;
    // Modulus requires Coord RHS (no operator%(T)).
    auto qm = q % Geo::Coord8{5, 20};
    if (qm.x() != 2 || qm.y() != 0) return 16;

    // --- Coord16 clamp ---
    Geo::Coord16 c16{100, 200};
    auto clamped = c16.clamp(-128, 127, -128, 127);
    if (clamped.x() != 100 || clamped.y() != 127) return 17;

    // --- Coord32 ---
    Geo::Coord32 c32{-1000, 2000};
    Geo::Coord32 zero{0, 0};
    if (!zero.is_zero()) return 18;
    if (c32.is_zero()) return 19;
    if (c32.rotate_90ccw() != Geo::Coord32{-2000, -1000}) return 20;
    if (c32.rotate_180() != Geo::Coord32{1000, -2000}) return 21;
    if (c32.rotate_90cw() != Geo::Coord32{2000, 1000}) return 22;

    // toroidal wrap
    Geo::Coord8 mod128{-5, -3};
    auto wrapped = mod128.wrap_mod(Geo::Coord8{127, 127});
    if (wrapped.x() < 0 || wrapped.y() < 0) return 23;

    // reflections
    Geo::Coord16 refl{3, 4};
    if (refl.reflect_x() != Geo::Coord16{3, -4}) return 24;
    if (refl.reflect_y() != Geo::Coord16{-3, 4}) return 25;
    if (refl.reflect_diagonal() != Geo::Coord16{4, 3}) return 26;

    // in-place rotation
    Geo::Coord8 rot{1, 2};
    rot.rotate_90ccw_inplace();
    if (rot != Geo::Coord8{-2, 1}) return 27;
    rot.swap_axes();
    if (rot != Geo::Coord8{1, -2}) return 28;

    // to_array
    auto arr = c8.to_array();
    if (arr[0] != 5 || arr[1] != -3) return 29;

    // static constants
    static_assert(Geo::coord_dim_count == 2);
    static_assert(Geo::coord_origin.x() == 0);
    static_assert(Geo::coord_origin.y() == 0);

    // sign predicates
    if (!Geo::Coord8{1, 1}.non_negative()) return 30;
    if (Geo::Coord8{1, 1}.has_negative()) return 31;
    if (Geo::Coord8{1, 1}.all_negative()) return 32;
    if (!Geo::Coord8{-1, -1}.all_negative()) return 33;
    if (!Geo::Coord8{-1, -1}.has_negative()) return 34;
    if (Geo::Coord8{-1, -1}.non_negative()) return 35;
    if (!Geo::Coord8{1, -1}.has_negative()) return 36;

    // dot product
    if (Geo::Coord8{1, 2}.dot({3, 4}) != 11) return 37;

    // ordering
    if (!(Geo::Coord8{0,0} < Geo::Coord8{1,0})) return 38;
    if (!(Geo::Coord8{0,1} < Geo::Coord8{0,2})) return 39;
    if (!(Geo::Coord8{1,0} > Geo::Coord8{0,0})) return 40;
    if (Geo::Coord8{0,0} == Geo::Coord8{0,0}) {} else return 41;
    if (Geo::Coord8{0,0} != Geo::Coord8{1,0}) {} else return 42;

    // dist_sq for different widths
    auto d8 = Geo::Coord8{0,0}.dist_sq({3,4});
    auto d16 = Geo::Coord16{0,0}.dist_sq({3,4});
    auto d32 = Geo::Coord32{0,0}.dist_sq({3,4});
    if (d8 != 25 || d16 != 25 || d32 != 25) return 43;

    // compound operators
    Geo::Coord8 a{1, 2};
    a += Geo::Coord8{3, 4};
    if (a != Geo::Coord8{4, 6}) return 44;
    a -= Geo::Coord8{1, 1};
    if (a != Geo::Coord8{3, 5}) return 45;
    a *= 2;
    if (a != Geo::Coord8{6, 10}) return 46;
    a /= 3;
    if (a != Geo::Coord8{2, 3}) return 47;

    std::cout << "All " << __LINE__ << " tests passed.\n";
    return 0;
}
