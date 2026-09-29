// Sigil Rev A: fit-test tray (OpenSCAD).
//
// A quick FDM print to prove the carrier board, DevKit socket rows and USB
// opening fit before the real 45-degree wedge case is designed. It has no lid,
// display window or joystick hole yet.
//
// All board dimensions come from Sigil_EInk.kicad_pcb (Edge.Cuts 100..146 x
// 99.5..176, four M3 holes) and the DevKit footprint in Sigil.pretty. Positions
// below are board-local: origin at the board's bottom-left corner, viewed from
// above with the USB end at the top, the same view KiCad shows. KiCad's y axis
// points down, so y_local = 76.5 - (y_kicad - 99.5).
//
// Everything marked "VERIFY" is a guess to be checked with calipers against the
// real parts, not a measurement.

$fn = 48;

// ---- Board (from KiCad) ----------------------------------------------------
pcb_w = 46;          // Edge.Cuts x 100..146
pcb_l = 76.5;        // Edge.Cuts y 99.5..176
pcb_t = 1.6;         // VERIFY: ordered board thickness
hole_d = 3.2;        // MountingHole_3.2mm_M3
holes_kicad = [[104, 104], [142, 104], [104, 172], [142, 172]];

// ---- DevKit (owner's ruler measurements, 2026-09-28) -------------------------
// Fab outline in KiCad: x 109.98..135.38, y 100.0..153.98 (25.4 x 53.975 mm).
kit_x0 = 109.98 - 100;
kit_w  = 25.4;
kit_y_usb = 76.5 - (100.0 - 99.5);   // USB end, local y
kit_l  = 53.975;
socket_h = 8.5;      // VERIFY: female header body height
kit_pcb_t = 1.6;     // VERIFY
usb_w = 8.0;         // VERIFY: micro-USB shell width
usb_h = 3.2;         // VERIFY: micro-USB shell height
usb_x_local = kit_x0 + kit_w / 2;    // centred on the DevKit

// ---- Case ------------------------------------------------------------------
clear = 0.4;         // gap around the board; FDM, tune per printer
wall = 2.4;
floor_t = 2.0;
post_h = 4.0;        // lifts the board so solder tails (about 2.5 mm) clear the floor
post_d = 7.0;
screw_pilot_d = 2.5; // self-tapping M3 into PLA/PETG; use 4.0 for heat-set inserts
wall_h = 20;         // tall enough to show the DevKit and USB jack inside
usb_slot_margin = 1.5;   // extra room around the USB shell in the wall opening

show_ghosts = true;  // translucent board/DevKit for preview only; not printed

inner_w = pcb_w + 2 * clear;
inner_l = pcb_l + 2 * clear;
outer_w = inner_w + 2 * wall;
outer_l = inner_l + 2 * wall;

function to_local(p) = [p[0] - 100, 76.5 - (p[1] - 99.5)];

board_z = floor_t + post_h;
kit_bottom_z = board_z + pcb_t + socket_h;
usb_center_z = kit_bottom_z + kit_pcb_t + usb_h / 2;

module tray() {
    difference() {
        union() {
            // Shell, outer origin at (-clear - wall, -clear - wall) so the board
            // sits at local (0,0).
            translate([-clear - wall, -clear - wall, 0])
                cube([outer_w, outer_l, floor_t + wall_h]);
        }
        // Cavity
        translate([-clear, -clear, floor_t])
            cube([inner_w, inner_l, wall_h + 1]);
        // USB opening in the top (USB-end) wall
        translate([usb_x_local - (usb_w + 2 * usb_slot_margin) / 2,
                   pcb_l + clear - 0.01,
                   usb_center_z - (usb_h + 2 * usb_slot_margin) / 2])
            cube([usb_w + 2 * usb_slot_margin, wall + 0.02, usb_h + 2 * usb_slot_margin]);
    }
    // Screw posts
    for (h = holes_kicad) {
        p = to_local(h);
        translate([p[0], p[1], floor_t - 0.01])
            difference() {
                cylinder(d = post_d, h = post_h + 0.01);
                translate([0, 0, 0.5]) cylinder(d = screw_pilot_d, h = post_h + 1);
            }
    }
}

module ghosts() {
    if (show_ghosts) {
        // Carrier PCB
        %translate([0, 0, board_z]) cube([pcb_w, pcb_l, pcb_t]);
        // DevKit, raised on the sockets
        %translate([kit_x0, kit_y_usb - kit_l, kit_bottom_z]) cube([kit_w, kit_l, kit_pcb_t + 2]);
        // USB shell
        %translate([usb_x_local - usb_w / 2, kit_y_usb - 1, kit_bottom_z + kit_pcb_t])
            cube([usb_w, 5, usb_h]);
    }
}

tray();
ghosts();
