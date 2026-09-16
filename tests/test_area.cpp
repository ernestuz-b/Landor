#include <cassert>
#include <iostream>
#include <sstream>
#include "world/area.hpp"

int main() {
    // --- Default construction ---
    landor::geo::Area32 a_empty;
    if (!a_empty.is_empty()) return 1;
    if (a_empty.width() != 0) return 2;
    if (a_empty.height() != 0) return 3;

    // --- Two-corner construction (auto-sorts) ---
    landor::geo::Area32 a1{landor::geo::Coord32{0, 0}, landor::geo::Coord32{4, 3}};
    if (a1.min() != landor::geo::Coord32{0, 0}) return 10;
    if (a1.max() != landor::geo::Coord32{4, 3}) return 11;
    if (a1.width() != 5) return 12;
    if (a1.height() != 4) return 13;
    if (!a1.contains(landor::geo::Coord32{0, 0})) return 14;
    if (!a1.contains(landor::geo::Coord32{4, 3})) return 15;
    if (!a1.contains(landor::geo::Coord32{2, 1})) return 16;
    if (a1.contains(landor::geo::Coord32{-1, 0})) return 17;
    if (a1.contains(landor::geo::Coord32{4, 4})) return 18;
    if (a1.is_empty()) return 19;

    // --- Reverse order input (should auto-sort) ---
    landor::geo::Area32 a_rev{landor::geo::Coord32{4, 3}, landor::geo::Coord32{0, 0}};
    if (a_rev.min() != landor::geo::Coord32{0, 0}) return 20;
    if (a_rev.max() != landor::geo::Coord32{4, 3}) return 21;

    // --- Equality / Inequality ---
    if (!(a1 == a_rev)) return 22;
    if (a1 != a_rev) return 23;

    // --- Intersection (overlapping) ---
    landor::geo::Area32 b{landor::geo::Coord32{2, 1}, landor::geo::Coord32{6, 5}};
    auto inter = a1.intersection(b);
    if (!inter.has_value()) return 30;
    if (inter->min() != landor::geo::Coord32{2, 1}) return 31;
    if (inter->max() != landor::geo::Coord32{4, 3}) return 32;

    // --- Intersection (non-overlapping) ---
    landor::geo::Area32 c{landor::geo::Coord32{10, 10}, landor::geo::Coord32{20, 20}};
    auto inter_nc = a1.intersection(c);
    if (inter_nc.has_value()) return 33;

    // --- Union ---
    landor::geo::Area32 d{landor::geo::Coord32{3, 4}, landor::geo::Coord32{7, 8}};
    auto u = a1.union_area(d);
    if (u.min() != landor::geo::Coord32{0, 0}) return 40;
    if (u.max() != landor::geo::Coord32{7, 8}) return 41;

    // --- Union with empty ---
    auto u_empty = a1.union_area(landor::geo::Area32{});
    if (u_empty != a1) return 42;
    u_empty = landor::geo::Area32{}.union_area(a1);
    if (u_empty != a1) return 43;

    // --- Contains (area within area) ---
    landor::geo::Area32 inner{landor::geo::Coord32{1, 1}, landor::geo::Coord32{3, 2}};
    if (!a1.contains(inner)) return 50;
    landor::geo::Area32 outer{landor::geo::Coord32{-1, -1}, landor::geo::Coord32{5, 4}};
    if (!outer.contains(a1)) return 51;

    // --- Intersects ---
    if (!a1.intersects(b)) return 52;  // overlap expected
    if (a1.intersects(c)) return 53;   // no overlap expected
    if (a1.intersects(landor::geo::Area32{})) return 54;  // empty does not intersect

    // --- Clamp ---
    landor::geo::Area32 e{landor::geo::Coord32{-2, -2}, landor::geo::Coord32{6, 6}};
    auto clamped = e.clamp(a1);
    if (clamped.min() != landor::geo::Coord32{0, 0}) return 60;
    if (clamped.max() != landor::geo::Coord32{4, 3}) return 61;

    // --- Clamp (outside) ---
    landor::geo::Area32 f{landor::geo::Coord32{10, 10}, landor::geo::Coord32{20, 20}};
    auto clamped_out = f.clamp(a1);
    if (!clamped_out.is_empty()) return 62;

    // --- Split along x (wider than tall) ---
    landor::geo::Area32 wide{landor::geo::Coord32{0, 0}, landor::geo::Coord32{9, 3}};  // 10x4
    auto [w1, w2] = wide.split();
    if (w1.is_empty() || w2.is_empty()) return 70;
    if (w1.max().x() + 1 != w2.min().x()) return 71;
    // Together should cover the original width.
    auto w_union = w1.union_area(w2);
    if (w_union != wide) return 72;

    // --- Split along y (taller than wide) ---
    landor::geo::Area32 tall{landor::geo::Coord32{0, 0}, landor::geo::Coord32{3, 9}};  // 4x10
    auto [t1, t2] = tall.split();
    if (t1.is_empty() || t2.is_empty()) return 80;
    if (t1.max().y() + 1 != t2.min().y()) return 81;
    auto t_union = t1.union_area(t2);
    if (t_union != tall) return 82;

    // --- Split empty area ---
    landor::geo::Area32 empty;
    auto [e1, e2] = empty.split();
    if (!e1.is_empty() || !e2.is_empty()) return 90;

    // --- from_coords factory ---
    landor::geo::Area32 fc = landor::geo::Area32::from_coords(1, 2, 5, 6);
    if (fc.min() != landor::geo::Coord32{1, 2}) return 100;
    if (fc.max() != landor::geo::Coord32{5, 6}) return 101;
    if (fc.width() != 5) return 102;
    if (fc.height() != 5) return 103;

    // --- Coord8 Area8 ---
    landor::geo::Area8 a8{landor::geo::Coord8{-50, -30}, landor::geo::Coord8{50, 30}};
    if (a8.width() != 101) return 110;
    if (a8.height() != 61) return 111;
    if (!a8.contains(landor::geo::Coord8{0, 0})) return 112;
    landor::geo::Area8 a8_rev{landor::geo::Coord8{50, 30}, landor::geo::Coord8{-50, -30}};
    if (a8_rev != a8) return 113;

    // --- Coord16 Area16 ---
    landor::geo::Area16 a16{landor::geo::Coord16{-1000, -2000}, landor::geo::Coord16{1000, 2000}};
    if (a16.width() != 2001) return 120;
    if (a16.height() != 4001) return 121;

    // --- assign (re-normalise) ---
    landor::geo::Area32 aa;
    aa.assign(landor::geo::Coord32{10, 20}, landor::geo::Coord32{5, 15});
    if (aa.min() != landor::geo::Coord32{5, 15}) return 130;
    if (aa.max() != landor::geo::Coord32{10, 20}) return 131;

    // --- Stream output compiles ---
    {
        std::ostringstream oss;
        oss << a1;
        std::string s = oss.str();
        if (s.find("0") == std::string::npos || s.find("4") == std::string::npos)
            return 140;
    }

    // --- Intersection at boundary (touching edges) ---
    landor::geo::Area32 x1{landor::geo::Coord32{0, 0}, landor::geo::Coord32{4, 4}};
    // Adjacent but not overlapping.
    landor::geo::Area32 x2{landor::geo::Coord32{5, 0}, landor::geo::Coord32{9, 4}};
    auto touch = x1.intersection(x2);
    if (touch.has_value()) return 150;  // touching should NOT intersect

    // --- Intersection at single-cell overlap ---
    landor::geo::Area32 y1{landor::geo::Coord32{0, 0}, landor::geo::Coord32{4, 4}};
    // Shares a single corner cell.
    landor::geo::Area32 y2{landor::geo::Coord32{4, 4}, landor::geo::Coord32{8, 8}};
    auto corner = y1.intersection(y2);
    if (!corner.has_value()) return 160;
    if (corner->min() != landor::geo::Coord32{4, 4}) return 161;
    if (corner->max() != landor::geo::Coord32{4, 4}) return 162;
    if (corner->width() != 1 || corner->height() != 1) return 163;

    // --- Union of adjacent areas ---
    landor::geo::Area32 adj1{landor::geo::Coord32{0, 0}, landor::geo::Coord32{4, 4}};
    landor::geo::Area32 adj2{landor::geo::Coord32{5, 0}, landor::geo::Coord32{9, 4}};
    auto adj_union = adj1.union_area(adj2);
    if (adj_union.min() != landor::geo::Coord32{0, 0}) return 170;
    if (adj_union.max() != landor::geo::Coord32{9, 4}) return 171;

    std::cout << "All tests passed.\n";
    return 0;
}
