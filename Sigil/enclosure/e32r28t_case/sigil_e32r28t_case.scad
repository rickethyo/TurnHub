// TurnHub Sigil stand-in: phone-style case for the 2.8-inch E32R28T board
// (the old Gen 1 Atlas board), in the same style as the Atlas S3 case in
// Atlas/enclosure/s3_phone_case. Portrait, rounded, flat or gently tilted.
//
// Two printed parts held together by four M3 screws from the back that pass
// through the board's own mounting holes, plus an optional clear light bar:
//   tub   - back (flat or wedge), walls, standoffs, side gutters for plugs
//   bezel - front frame with the display window
//   pipe  - clear light bar (print in clear PETG/PLA) that carries the
//           board's back-facing RGB LED to a glowing slot under USB-C
//
// Board numbers come from the LCDWIKI E32R28T outline drawing V1.0
// (2024-08-31): PCB 50 x 86 x 1.6 mm, R3.5 corners, four 3.2 mm holes 4 mm
// in from the edges (42 x 78 apart), 4.0 mm of display and resistive touch
// above the PCB, back parts up to 5.09 mm. *Experimental*, not yet printed.
//
// Coordinates: origin at the board centre, z = 0 on the board's back
// (component) face, +z towards the screen, +Y towards the top (away from
// USB-C), +X to the right when looking at the screen. Features read off the
// drawing's "Back" view (xb from its left, yb from its top) map with
// X = W/2 - xb, Y = L/2 - yb.
//
// Render one part: openscad -D 'part="tub"' -D tilt=5 -o tub.stl sigil_e32r28t_case.scad

part = "assembly"; // "tub", "bezel", "pipe", "assembly"
tilt = 0;          // degrees; > 0 makes the back a wedge, thick end at the top

/* [Board, from the vendor drawing] */
W = 50.0;
L = 86.0;
PCB_T = 1.6;
PCB_R = 3.5;
HOLE_DX = 42.0 / 2;
HOLE_DY = 78.0 / 2;
FRONT_STACK = 4.0;  // tape 0.5 + LCD 2.3 + resistive touch 1.2
BACK_PARTS = 5.09;

/* [Case] */
clr = 0.4;
clr_usb = 1.0;
gutter = 4.5;       // room beside each long edge for the side-entry plugs
wall = 2.0;
back_t = 1.6;
back_clear = 7.6;   // back parts 5.09 + the light bar under them
lip_t = 1.2;
glass_gap = 0.4;    // resistive touch: the bezel must never press on it
R_out = 7;
R_edge = 2.5;
R_in = 4;

/* [Screws: M3 x 10 self-tapping, or M3 with heat-set inserts] */
screw_clear = 3.4;
screw_pilot = 2.5;  // use 4.0 for M3 x 5.7 heat-set inserts
head_d = 6.2;
head_seat_z = -4.2; // M3 x 10 stops 1.4 mm short of the front
standoff_d = 9.0;
boss_d = 5.5;

/* [Openings] */
// Display window, from the top of the board (front view): touch VA is
// 45.2 wide, 10.45 to 69.9 mm from the top.
win_w = 46.0;
win_from_top = 9.9;
win_to = 70.5;
win_chamfer = 0.8;
usb_cut = [13.0, 7.0];
usb_z = -1.6;
buttons = [[W / 2 - 13.43, -L / 2 + 3.26], [-W / 2 + 13.43, -L / 2 + 3.26]]; // RESET, BOOT
button_hole = 2.2;
sd_y = L / 2 - 35.5;         // microSD slot centre, on the -X wall
sd_cut = [13.0, 2.8];
sd_z_top = 0.4;
sd_recess_len = 24;         // fingertip recess around the slot
sd_recess_depth = 1.0;

/* [Light bar] */
// The only RGB LED faces the back, about mid-width and 41.5 mm from the USB
// end (from Ricky's photo, *Needs verification*). A clear bar lies on the
// floor: a post rises to just under the LED, a 45-degree face turns the light
// toward USB-C, and the bar ends flush in a slot below the USB-C opening.
led_c = [0, -L / 2 + 41.5];
pipe_w = 4.0;
pipe_h = 1.8;
pipe_post_d = 3.2;
pipe_post_gap = 1.0;  // stays this far below the board's back face
pipe_clr = 0.25;

$fn = 48;

z_split = PCB_T;
z_floor = -back_clear;
z_bot = z_floor - back_t;    // at the bottom (thin) end
z_pocket = PCB_T + FRONT_STACK + glass_gap;
z_top = z_pocket + lip_t;
in_x = W / 2 + clr + gutter;
in_top = L / 2 + clr;
in_bot = -(L / 2 + clr_usb);
out_x = in_x + wall;
out_top = in_top + wall;
out_bot = in_bot - wall;
eps = 0.01;

pipe_y1 = led_c[1] + pipe_post_d / 2;  // far end, with the 45-degree face
pipe_y0 = out_bot;                      // flush with the outside of the bottom wall

function back_z(y) = z_bot - tan(tilt) * (y - out_bot); // underside of the wedge

module rrect(x0, y0, x1, y1, r) {
    hull() for (x = [x0 + r, x1 - r], y = [y0 + r, y1 - r])
        translate([x, y]) circle(r = r);
}

module torus(R, r) {
    rotate_extrude($fn = 40) translate([R, 0]) circle(r = r, $fn = 24);
}

// Rounded slab; with tilt the back corners drop along the wedge.
module body() {
    hull() for (x = [-out_x + R_out, out_x - R_out], y = [out_bot + R_out, out_top - R_out]) {
        translate([x, y, z_top - R_edge]) torus(R_out - R_edge, R_edge);
        translate([x, y, back_z(y) + R_edge]) torus(R_out - R_edge, R_edge);
    }
}

module slab(z0, z1) {
    translate([-100, -100, z0]) cube([200, 200, z1 - z0]);
}

module holes_at() {
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * HOLE_DX, sy * HOLE_DY]) children();
}

module usb_opening() {
    translate([0, out_bot - 1, usb_z]) rotate([-90, 0, 0])
        linear_extrude(wall + clr_usb + 2)
            rrect(-usb_cut[0] / 2, -usb_cut[1] / 2, usb_cut[0] / 2, usb_cut[1] / 2, usb_cut[1] / 2 - eps);
}

// Stadium recess around the microSD slot, as on the Gen 1 Atlas shell.
module sd_recess() {
    hull() for (dy = [-sd_recess_len / 2 + 4, sd_recess_len / 2 - 4])
        translate([-out_x - 1, sd_y + dy, sd_z_top - sd_cut[1] / 2]) rotate([0, 90, 0])
            cylinder(d = 8, h = 1 + sd_recess_depth);
}

module tub() {
    difference() {
        union() {
            difference() {
                intersection() { body(); slab(-60, z_split); }
                translate([0, 0, z_floor]) linear_extrude(z_split - z_floor + 1)
                    rrect(-in_x, in_bot, in_x, in_top, R_in);
            }
            holes_at() translate([0, 0, z_floor - eps]) cylinder(d = standoff_d, h = -z_floor + eps);
            // Shelf in the gutter so a microSD card slides straight into the socket.
            translate([-in_x - eps, sd_y - sd_cut[0] / 2, z_floor - eps])
                cube([gutter + eps, sd_cut[0], sd_z_top - sd_cut[1] - z_floor + eps]);
            // Low rails that keep the light bar on its line.
            for (sx = [-1, 1])
                translate([sx * (pipe_w / 2 + pipe_clr + 0.6) - 0.6, in_bot, z_floor - eps])
                    cube([1.2, pipe_y1 - in_bot, 1.0 + eps]);
        }
        // Slot in the bottom wall where the light bar shows, under USB-C.
        translate([-pipe_w / 2 - pipe_clr, out_bot - 1, z_floor - eps])
            cube([pipe_w + 2 * pipe_clr, wall + clr_usb + 2, pipe_h + pipe_clr]);
        holes_at() {
            translate([0, 0, -60]) cylinder(d = screw_clear, h = 80);
            translate([0, 0, -60]) cylinder(d = head_d, h = head_seat_z + 60);
        }
        usb_opening();
        translate([-out_x - 1, sd_y - sd_cut[0] / 2, sd_z_top - sd_cut[1]])
            cube([wall + 2, sd_cut[0], sd_cut[1]]);
        sd_recess();
        for (b = buttons) translate([b[0], b[1], -60]) cylinder(d = button_hole, h = 60 + z_floor + eps);
    }
}

module bezel() {
    win_y1 = L / 2 - win_from_top;
    win_y0 = L / 2 - win_to;
    difference() {
        union() {
            difference() {
                intersection() { body(); slab(z_split, z_top + 1); }
                translate([0, 0, z_split - 1]) linear_extrude(z_pocket - z_split + 1)
                    rrect(-W / 2 - clr, in_bot, W / 2 + clr, in_top, PCB_R + clr);
            }
            holes_at() translate([0, 0, z_split]) cylinder(d = boss_d, h = z_pocket - z_split + eps);
        }
        hull() {
            translate([0, 0, z_pocket - 1]) linear_extrude(1 + lip_t - win_chamfer)
                rrect(-win_w / 2, win_y0, win_w / 2, win_y1, 2);
            translate([0, 0, z_top - eps]) linear_extrude(1)
                rrect(-win_w / 2 - win_chamfer, win_y0 - win_chamfer,
                      win_w / 2 + win_chamfer, win_y1 + win_chamfer, 2 + win_chamfer);
        }
        holes_at() translate([0, 0, z_split - 1]) cylinder(d = screw_pilot, h = z_top - 1 - z_split);
        usb_opening();
        sd_recess();
    }
}

// In place in the case: bar on the floor, post up to the LED.
module light_pipe() {
    post_top = -pipe_post_gap;
    difference() {
        union() {
            translate([-pipe_w / 2, pipe_y0, z_floor]) cube([pipe_w, pipe_y1 - pipe_y0, pipe_h]);
            translate([led_c[0], led_c[1], z_floor]) cylinder(d = pipe_post_d, h = post_top - z_floor);
        }
        // 45-degree face under the post bounces the light toward USB-C.
        translate([0, led_c[1] - pipe_post_d / 2, z_floor]) rotate([45, 0, 0])
            translate([-5, 0, -20]) cube([10, 30, 20]);
    }
}

module board() {
    color("darkgreen") linear_extrude(PCB_T) rrect(-W / 2, -L / 2, W / 2, L / 2, PCB_R);
    color("black") translate([0, 0, PCB_T]) linear_extrude(FRONT_STACK)
        rrect(-49.6 / 2, L / 2 - 77.4, 49.6 / 2, L / 2 - 8.6, 0.5);
    color("silver") translate([-4.5, -L / 2 - 0.8, -3.2]) cube([9, 7.3, 3.2]);
}

// The tub prints on its back: a tilted one is rotated so the wedge lies flat.
if (part == "tub") rotate([tilt, 0, 0]) tub();
else if (part == "bezel") translate([0, 0, z_top]) rotate([180, 0, 0]) bezel();
else if (part == "pipe") rotate([0, 90, 0]) translate([pipe_w / 2, 0, 0]) light_pipe(); // on its side
else {
    color("dimgray") tub();
    color("white", 0.7) light_pipe();
    board();
    color("gray", 0.9) translate([0, 0, 25]) bezel();
}

echo(str("Outside: ", 2 * out_x, " x ", out_top - out_bot, " mm; ",
         z_top - z_bot, " mm thick at the bottom, ", z_top - back_z(out_top), " at the top"));
echo(str("Screw: M3 reaching ", z_top - 1 - head_seat_z, " mm max from the head seat"));
