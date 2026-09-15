#include <cassert>
#include <iostream>
#include <sstream>
#include "world/area.hpp"

int main() {
    // --- Default construction ---
    Geo::Area32 a_empty;
    if (!a_empty.is_empty()) return 1;
    if (a_empty.width() != 0) return 2;
    if (a_empty.height() != 0) return 3;

    // --- Two-corner construction (auto-sorts) ---
    Geo::Area32 a1{Geo::Coord32{0, 0}, Geo::Coord32{4, 3}};
    if (a1.min() != Geo::Coord32{0, 0}) return 10;
    if (a1.max() != Geo::Coord32{4, 3}) return 11;
    if (a1.width() != 5) return 12;
    if (a1.height() != 4) return 13;
    if (!a1.contains(Geo::Coord32{0, 0})) return 14;
    if (!a1.contains(Geo::Coord32{4, 3})) return 15;
    if (!a1.contains(Geo::Coord32{2, 1})) return 16;
    if (a1.contains(Geo::Coord32{-1, 0})) return 17;
    if (a1.contains(Geo::Coord32{4, 4})) return 18;
    if (a1.is_empty()) return 19;

    // --- Reverse order input (should auto-sort) ---
    Geo::Area32 a_rev{Geo::Coord32{4, 3}, Geo::Coord32{0, 0}};
    if (a_rev.min() != Geo::Coord32{0, 0}) return 20;
    if (a_rev.max() != Geo::Coord32{4, 3}) return 21;

    // --- Equality / Inequality ---
    if (!(a1 == a_rev)) return 22;
    if (a1 != a_rev) return 23;

    // --- Intersection (overlapping) ---
    Geo::Area32 b{Geo::Coord32{2, 1}, Geo::Coord32{6, 5}};
    auto inter = a1.intersection(b);
    if (!inter.has_value()) return 30;
    if (inter->min() != Geo::Coord32{2, 1}) return 31;
    if (inter->max() != Geo::Coord32{4, 3}) return 32;

    // --- Intersection (non-overlapping) ---
    Geo::Area32 c{Geo::Coord32{10, 10}, Geo::Coord32{20, 20}};
    auto inter_nc = a1.intersection(c);
    if (inter_nc.has_value()) return 33;

    // --- Union ---
    Geo::Area32 d{Geo::Coord32{3, 4}, Geo::Coord32{7, 8}};
    auto u = a1.union_area(d);
    if (u.min() != Geo::Coord32{0, 0}) return 40;
    if (u.max() != Geo::Coord32{7, 8}) return 41;

    // --- Union with empty ---
    auto u_empty = a1.union_area(Geo::Area32{});
    if (u_empty != a1) return 42;
    u_empty = Geo::Area32{}.union_area(a1);
    if (u_empty != a1) return 43;

    // --- Contains (area within area) ---
    Geo::Area32 inner{Geo::Coord32{1, 1}, Geo::Coord32{3, 2}};
    if (!a1.contains(inner)) return 50;
    Geo::Area32 outer{Geo::Coord32{-1, -1}, Geo::Coord32{5, 4}};
    if (!outer.contains(a1)) return 51;

    // --- Intersects ---
    if (!a1.intersects(b)) return 52;  // overlap expected
    if (a1.intersects(c)) return 53;   // no overlap expected
    if (a1.intersects(Geo::Area32{})) return 54;  // empty does not intersect

    // --- Clamp ---
    Geo::Area32 e{Geo::Coord32{-2, -2}, Geo::Coord32{6, 6}};
    auto clamped = e.clamp(a1);
    if (clamped.min() != Geo::Coord32{0, 0}) return 60;
    if (clamped.max() != Geo::Coord32{4, 3}) return 61;

    // --- Clamp (outside) ---
    Geo::Area32 f{Geo::Coord32{10, 10}, Geo::Coord32{20, 20}};
    auto clamped_out = f.clamp(a1);
    if (!clamped_out.is_empty()) return 62;

    // --- Split along x (wider than tall) ---
    Geo::Area32 wide{Geo::Coord32{0, 0}, Geo::Coord32{9, 3}};  // 10x4
    auto [w1, w2] = wide.split();
    if (w1.is_empty() || w2.is_empty()) return 70;
    if (w1.max().x() + 1 != w2.min().x()) return 71;
    // Together should cover the original width.
    auto w_union = w1.union_area(w2);
    if (w_union != wide) return 72;

    // --- Split along y (taller than wide) ---
    Geo::Area32 tall{Geo::Coord32{0, 0}, Geo::Coord32{3, 9}};  // 4x10
    auto [t1, t2] = tall.split();
    if (t1.is_empty() || t2.is_empty()) return 80;
    if (t1.max().y() + 1 != t2.min().y()) return 81;
    auto t_union = t1.union_area(t2);
    if (t_union != tall) return 82;

    // --- Split empty area ---
    Geo::Area32 empty;
    auto [e1, e2] = empty.split();
    if (!e1.is_empty() || !e2.is_empty()) return 90;

    // --- from_coords factory ---
    Geo::Area32 fc = Geo::Area32::from_coords(1, 2, 5, 6);
    if (fc.min() != Geo::Coord32{1, 2}) return 100;
    if (fc.max() != Geo::Coord32{5, 6}) return 101;
    if (fc.width() != 5) return 102;
    if (fc.height() != 5) return 103;

    // --- Coord8 Area8 ---
    Geo::Area8 a8{Geo::Coord8{-50, -30}, Geo::Coord8{50, 30}};
    if (a8.width() != 101) return 110;
    if (a8.height() != 61) return 111;
    if (!a8.contains(Geo::Coord8{0, 0})) return 112;
    Geo::Area8 a8_rev{Geo::Coord8{50, 30}, Geo::Coord8{-50, -30}};
    if (a8_rev != a8) return 113;

    // --- Coord16 Area16 ---
    Geo::Area16 a16{Geo::Coord16{-1000, -2000}, Geo::Coord16{1000, 2000}};
    if (a16.width() != 2001) return 120;
    if (a16.height() != 4001) return 121;

    // --- assign (re-normalise) ---
    Geo::Area32 aa;
    aa.assign(Geo::Coord32{10, 20}, Geo::Coord32{5, 15});
    if (aa.min() != Geo::Coord32{5, 15}) return 130;
    if (aa.max() != Geo::Coord32{10, 20}) return 131;

    // --- Stream output compiles ---
    {
        std::ostringstream oss;
        oss << a1;
        std::string s = oss.str();
        if (s.find("0") == std::string::npos || s.find("4") == std::string::npos)
            return 140;
    }

    // --- Intersection at boundary (touching edges) ---
    Geo::Area32 x1{Geo::Coord32{0, 0}, Geo::Coord32{4, 4}};
    Geo::Area32 x2{Geo::Coord32{5, 0}, Geo::Coord32{9, 4}};  // adjacent but not overlapping
    auto touch = x1.intersection(x2);
    if (touch.has_value()) return 150;  // touching should NOT intersect

    // --- Intersection at single-cell overlap ---
    Geo::Area32 y1{Geo::Coord32{0, 0}, Geo::Coord32{4, 4}};
    Geo::Area32 y2{Geo::Coord32{4, 4}, Geo::Coord32{8, 8}};  // shares corner cell
    auto corner = y1.intersection(y2);
    if (!corner.has_value()) return 160;
    if (corner->min() != Geo::Coord32{4, 4}) return 161;
    if (corner->max() != Geo::Coord32{4, 4}) return 162;
    if (corner->width() != 1 || corner->height() != 1) return 163;

    // --- Union of adjacent areas ---
    Geo::Area32 adj1{Geo::Coord32{0, 0}, Geo::Coord32{4, 4}};
    Geo::Area32 adj2{Geo::Coord32{5, 0}, Geo::Coord32{9, 4}};
    auto adj_union = adj1.union_area(adj2);
    if (adj_union.min() != Geo::Coord32{0, 0}) return 170;
    if (adj_union.max() != Geo::Coord32{9, 4}) return 171;

    std::cout << "All tests passed.\n";
    return 0;
}
