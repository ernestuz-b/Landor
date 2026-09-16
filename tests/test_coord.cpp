#include <cstdint>
#include <iostream>
#include <cassert>
#include "world/coord.hpp"

int main() {
    // --- Coord8 ---
    landor::geo::Coord8 c8{5, -3};
    if (c8.x() != 5) return 1;
    if (c8.y() != -3) return 2;

    auto c8add = c8 + landor::geo::Coord8{2, 4};
    if (c8add.x() != 7 || c8add.y() != 1) return 3;

    auto c8sub = c8 - landor::geo::Coord8{2, 4};
    if (c8sub.x() != 3 || c8sub.y() != -7) return 4;

    auto cneg = -c8;
    if (cneg.x() != -5 || cneg.y() != 3) return 5;

    if (landor::geo::Coord8::manhattan({0,0}, {3,4}) != 7) return 6;
    if (landor::geo::Coord8::chebyshev({0,0}, {3,4}) != 4) return 7;
    if (c8.dist_sq({0,0}) != 34) return 8;
    if (c8.neighbour(landor::geo::Dir::East) != landor::geo::Coord8{6, -3}) return 9;
    if (c8.neighbour8(0) != landor::geo::Coord8{6, -3}) return 10;   // east
    if (c8.neighbour8(1) != landor::geo::Coord8{6, -2}) return 11;   // se
    if (c8.neighbour8(6) != landor::geo::Coord8{5, -4}) return 12;   // north

    // scalar mult (commutative both ways)
    landor::geo::Coord8 sc{5, 10};
    auto sml = sc * 2;
    auto smr = 2 * sc;
    if (sml.x() != 10 || sml.y() != 20) return 13;
    if (smr.x() != 10 || smr.y() != 20) return 14;

    // division & mod
    landor::geo::Coord8 q{12, 20};
    auto qr = q / 4;
    if (qr.x() != 3 || qr.y() != 5) return 15;
    // Modulus requires Coord RHS (no operator%(T)).
    auto qm = q % landor::geo::Coord8{5, 20};
    if (qm.x() != 2 || qm.y() != 0) return 16;

    // --- Coord16 clamp ---
    landor::geo::Coord16 c16{100, 200};
    auto clamped = c16.clamp(-128, 127, -128, 127);
    if (clamped.x() != 100 || clamped.y() != 127) return 17;

    // --- Coord32 ---
    landor::geo::Coord32 c32{-1000, 2000};
    landor::geo::Coord32 zero{0, 0};
    if (!zero.is_zero()) return 18;
    if (c32.is_zero()) return 19;
    if (c32.rotate_90ccw() != landor::geo::Coord32{-2000, -1000}) return 20;
    if (c32.rotate_180() != landor::geo::Coord32{1000, -2000}) return 21;
    if (c32.rotate_90cw() != landor::geo::Coord32{2000, 1000}) return 22;

    // toroidal wrap
    landor::geo::Coord8 mod128{-5, -3};
    auto wrapped = mod128.wrap_mod(landor::geo::Coord8{127, 127});
    if (wrapped.x() < 0 || wrapped.y() < 0) return 23;

    // reflections
    landor::geo::Coord16 refl{3, 4};
    if (refl.reflect_x() != landor::geo::Coord16{3, -4}) return 24;
    if (refl.reflect_y() != landor::geo::Coord16{-3, 4}) return 25;
    if (refl.reflect_diagonal() != landor::geo::Coord16{4, 3}) return 26;

    // in-place rotation
    landor::geo::Coord8 rot{1, 2};
    rot.rotate_90ccw_inplace();
    if (rot != landor::geo::Coord8{-2, 1}) return 27;
    rot.swap_axes();
    if (rot != landor::geo::Coord8{1, -2}) return 28;

    // to_array
    auto arr = c8.to_array();
    if (arr[0] != 5 || arr[1] != -3) return 29;

    // static constants
    static_assert(landor::geo::coord_dim_count == 2);
    static_assert(landor::geo::coord_origin.x() == 0);
    static_assert(landor::geo::coord_origin.y() == 0);

    // sign predicates
    if (!landor::geo::Coord8{1, 1}.non_negative()) return 30;
    if (landor::geo::Coord8{1, 1}.has_negative()) return 31;
    if (landor::geo::Coord8{1, 1}.all_negative()) return 32;
    if (!landor::geo::Coord8{-1, -1}.all_negative()) return 33;
    if (!landor::geo::Coord8{-1, -1}.has_negative()) return 34;
    if (landor::geo::Coord8{-1, -1}.non_negative()) return 35;
    if (!landor::geo::Coord8{1, -1}.has_negative()) return 36;

    // dot product
    if (landor::geo::Coord8{1, 2}.dot({3, 4}) != 11) return 37;

    // ordering
    if (!(landor::geo::Coord8{0,0} < landor::geo::Coord8{1,0})) return 38;
    if (!(landor::geo::Coord8{0,1} < landor::geo::Coord8{0,2})) return 39;
    if (!(landor::geo::Coord8{1,0} > landor::geo::Coord8{0,0})) return 40;
    if (landor::geo::Coord8{0,0} == landor::geo::Coord8{0,0}) {} else return 41;
    if (landor::geo::Coord8{0,0} != landor::geo::Coord8{1,0}) {} else return 42;

    // dist_sq for different widths
    auto d8 = landor::geo::Coord8{0,0}.dist_sq({3,4});
    auto d16 = landor::geo::Coord16{0,0}.dist_sq({3,4});
    auto d32 = landor::geo::Coord32{0,0}.dist_sq({3,4});
    if (d8 != 25 || d16 != 25 || d32 != 25) return 43;

    // compound operators
    landor::geo::Coord8 a{1, 2};
    a += landor::geo::Coord8{3, 4};
    if (a != landor::geo::Coord8{4, 6}) return 44;
    a -= landor::geo::Coord8{1, 1};
    if (a != landor::geo::Coord8{3, 5}) return 45;
    a *= 2;
    if (a != landor::geo::Coord8{6, 10}) return 46;
    a /= 3;
    if (a != landor::geo::Coord8{2, 3}) return 47;

    std::cout << "All " << __LINE__ << " tests passed.\n";
    return 0;
}
