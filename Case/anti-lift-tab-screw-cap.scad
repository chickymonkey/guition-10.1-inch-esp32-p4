// Tiny removable cap to hide the anti-lift tab and its screw.
// Designed for the tab in 10.1_mount-landscape-with-anti-lift-tab.scad:
// tab size 10 x 5.5 x 2 mm, with a 1.5 mm screw clearance hole.
// Print in black filament. Open underside slides over the tab after fastening.

$fn = 48;

// Fit allowances are intentionally modest; adjust if your printer runs tight.
cap_width  = 13.0;   // X overall
cap_depth  = 8.5;    // Y overall
cap_height = 5.4;    // Z overall
wall       = 1.2;
roof       = 0;
corner_r   = 0.8;

inner_width  = 10.4; // 0.2 mm clearance per side around 10 mm tab
inner_depth  = 7.3;  // 0.2 mm clearance side around 5.5 mm tab
inner_height = 7; // open bottom; cavity extends to underside of roof

module rounded_box(x, y, z, r) {
    // Rounded outer corners with flat top and bottom.
    hull() {
        for (px = [-x/2+r, x/2-r])
            for (py = [-y/2+r, y/2-r])
                translate([px, py, 0]) cylinder(h=z, r=r);
    }
}

color("black")
difference() {
    rounded_box(cap_width, cap_depth, cap_height, corner_r);
    // Open underside; cavity is centred and leaves a 1.2 mm roof.
    translate([0, 1.2, -0.1])
        cube([inner_width, inner_depth, inner_height + 0.1], center=true);
}
