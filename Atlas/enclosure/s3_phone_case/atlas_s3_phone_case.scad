// TurnHub Atlas S3: flat, smartphone-style case for the 4-inch ES3C40P board.
//
// Two printed parts held together by four M3 screws from the back that pass
// through the board's own mounting holes:
//   tub   - back plate, walls, standoffs, side gutters for edge connectors
//   bezel - front frame with the display window and microphone port
// The kit's 40 x 28 x 9.7 mm speaker lies face down on the back plate, in the
// component-free area next to its socket (its lead is only 50 mm), so the
// cavity behind the board is as deep as the speaker.
//
// Board numbers come from the LCDWIKI ES3C40P Specification V1.0, figure 5.1
// (outline drawing, 2026-04-27) and were checked against Ricky's calipers
// (112.2 x 61.0 mm, hole spacing 104 x 53.1 mm). *Experimental*: the glass
// position along the board disagrees between the drawing and the photos by
// about 3 mm, so the bezel tolerates either (see README.md).
//
// Coordinates: origin at the board centre, z = 0 on the board's back
// (component) face, +z towards the screen, +Y towards the microphone end
// (portrait top), +X to the right when looking at the screen. Features read
// off the drawing's "Back" view (xb from its left, yb from its top) map with
// X = W/2 - xb, Y = L/2 - yb.
//
// Render one part: openscad -D 'part="tub"' -o tub.stl atlas_s3_phone_case.scad

part = "assembly"; // "tub", "bezel", "assembly"

/* [Board, from the vendor drawing] */
W = 61.04;          // PCB width
L = 112.21;         // PCB length
PCB_T = 1.6;
PCB_R = 3.5;        // PCB corner radius
HOLE_DX = 53.04 / 2;
HOLE_DY = 104.21 / 2;
FRONT_STACK = 4.35; // LCD 2.5 + tape 0.5 + touch glass 1.35 above the PCB
BACK_PARTS = 5.5;   // tallest back-side part (11.45 total - 1.6 - 4.35)

/* [Case] */
clr = 0.4;          // board to wall, sides and top
clr_usb = 1.0;      // USB-C receptacle sits ~0.8 mm past the board edge
gutter = 4.5;       // room beside each long edge for the side-entry plugs
wall = 2.0;
back_t = 1.6;
back_clear = 10.0;  // speaker 9.7 + 0.3; components alone need 5.5
lip_t = 1.2;        // bezel thickness over the glass
glass_gap = 0.3;    // the bezel never presses on the glass
R_out = 7;          // corner radius seen from the front
R_edge = 2.5;       // front and back edge roundover
R_in = 4;

/* [Screws: M3 x 10 self-tapping, or M3 with heat-set inserts] */
screw_clear = 3.4;
screw_pilot = 2.5;  // use 4.0 for M3 x 5.7 heat-set inserts
head_d = 6.2;
head_seat_z = -4.1;  // screw heads sit this deep, so M3 x 10 still reaches
standoff_d = 9.0;
boss_d = 5.5;
boss_clip = 5.6;    // bosses stop this far from the board ends (glass edge)

/* [Openings] */
// Display window, measured from the microphone end of the board (front view).
win_w = 58.0;
win_from_top = 8.3;
win_to = 102.5;
win_chamfer = 0.8;
// Microphone port: drawing puts it 14.13 mm from the right edge and 6.04 mm
// from the top, front view; slotted along Y to absorb the drawing/photo gap.
mic_x = W / 2 - 14.13;
mic_y = L / 2 - 6.0;
mic_slot = [1.6, 2.6];
usb_cut = [13.0, 7.0];       // fits most moulded USB-C plugs
usb_z = -1.65;               // receptacle centre below the board
buttons = [[W / 2 - 16.52, L / 2 - 108.1], [W / 2 - 43.52, L / 2 - 108.1]];
button_hole = 2.2;           // paper-clip access to BOOT and RESET
sd_y = L / 2 - 28.96;        // microSD slot centre, on the -X wall
sd_cut = [13.0, 2.8];        // width along Y, height in z
sd_z_top = 0.4;

/* [Speaker: the board kit's oval box speaker, measured] */
spk = [40.1, 28.3, 9.7];     // across, along the case, thick
spk_clr = 0.5;
// Centre of the clear PCB area below the display connector (drawing back view
// x 3..36, y 63..95), pushed into the +X gutter so 40 mm fits.
spk_c = [15.2, -23.3];
slot_w = 1.4;                // back grille slots
slot_len = 26;
slot_n = 7;

$fn = 48;

// Derived heights and footprints.
z_split = PCB_T;                                  // tub / bezel parting line
z_floor = -back_clear;
z_bot = z_floor - back_t;
z_pocket = PCB_T + FRONT_STACK + glass_gap;       // underside of the lip
z_top = z_pocket + lip_t;
in_x = W / 2 + clr + gutter;                      // tub cavity half-width
in_top = L / 2 + clr;
in_bot = -(L / 2 + clr_usb);
out_x = in_x + wall;
out_top = in_top + wall;
out_bot = in_bot - wall;
eps = 0.01;

module rrect(x0, y0, x1, y1, r) {
    hull() for (x = [x0 + r, x1 - r], y = [y0 + r, y1 - r])
        translate([x, y]) circle(r = r);
}

module torus(R, r) {
    rotate_extrude($fn = 40) translate([R, 0]) circle(r = r, $fn = 24);
}

// Slab with rounded corners and rounded front/back edges.
module body() {
    hull() for (x = [-out_x + R_out, out_x - R_out],
                y = [out_bot + R_out, out_top - R_out],
                z = [z_bot + R_edge, z_top - R_edge])
        translate([x, y, z]) torus(R_out - R_edge, R_edge);
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

module tub() {
    difference() {
        union() {
            difference() {
                intersection() { body(); slab(z_bot - 1, z_split); }
                translate([0, 0, z_floor]) linear_extrude(z_split - z_floor + 1)
                    rrect(-in_x, in_bot, in_x, in_top, R_in);
            }
            // Standoffs under the four board holes.
            holes_at() translate([0, 0, z_floor - eps]) cylinder(d = standoff_d, h = -z_floor + eps);
            speaker_cradle();
            // Shelf in the gutter so a microSD card slides straight into the socket.
            translate([-in_x - eps, sd_y - sd_cut[0] / 2, z_floor - eps])
                cube([gutter + eps, sd_cut[0], sd_z_top - sd_cut[1] - z_floor + eps]);
        }
        holes_at() {
            translate([0, 0, z_bot - 1]) cylinder(d = screw_clear, h = 20);
            translate([0, 0, z_bot - 1]) cylinder(d = head_d, h = head_seat_z - z_bot + 1);
        }
        usb_opening();
        // microSD slot through the -X wall.
        translate([-out_x - 1, sd_y - sd_cut[0] / 2, sd_z_top - sd_cut[1]])
            cube([wall + 2, sd_cut[0], sd_cut[1]]);
        // Fingernail scoop around the slot.
        translate([-out_x - 3.2, sd_y, sd_z_top - sd_cut[1] / 2])
            scale([1, 1.6, 0.9]) sphere(r = 4);
        for (i = [0:slot_n - 1])
            translate([spk_c[0], spk_c[1] + (i - (slot_n - 1) / 2) * 2 * slot_w, z_bot - 1])
                linear_extrude(back_t + 2)
                    rrect(-slot_len / 2, -slot_w / 2, slot_len / 2, slot_w / 2, slot_w / 2 - eps);
        for (b = buttons) translate([b[0], b[1], z_bot - 1]) cylinder(d = button_hole, h = back_t + 2);
    }
}

// Low rim that locates the speaker face down on the back plate; open at the
// +X end for the lead, towards its socket.
module speaker_cradle() {
    sx = spk[0] / 2 + spk_clr;
    sy = spk[1] / 2 + spk_clr;
    difference() {
        translate([spk_c[0], spk_c[1], z_floor - eps]) linear_extrude(3 + eps)
            difference() {
                rrect(-sx - 1.2, -sy - 1.2, sx + 1.2, sy + 1.2, 4);
                rrect(-sx, -sy, sx, sy, 3);
            }
        translate([spk_c[0] + sx, spk_c[1], z_floor]) cube([6, 10, 20], center = true);
    }
}

module bezel() {
    win_y1 = L / 2 - win_from_top;
    win_y0 = L / 2 - win_to;
    difference() {
        union() {
            difference() {
                intersection() { body(); slab(z_split, z_top + 1); }
                // Pocket for the board, display and glass.
                translate([0, 0, z_split - 1]) linear_extrude(z_pocket - z_split + 1)
                    rrect(-W / 2 - clr, in_bot, W / 2 + clr, in_top, PCB_R + clr);
            }
            // Bosses that clamp the board against the standoffs; clipped so they
            // stay clear of the glass at the board ends.
            intersection() {
                holes_at() translate([0, 0, z_split]) cylinder(d = boss_d, h = z_pocket - z_split + eps);
                union() for (sy = [-1, 1])
                    translate([-50, sy > 0 ? L / 2 - boss_clip : -L / 2 - 5, z_split - 1])
                        cube([100, boss_clip + 5, 20]);
            }
        }
        // Display window with a chamfered front edge.
        hull() {
            translate([0, 0, z_pocket - 1]) linear_extrude(1 + lip_t - win_chamfer)
                rrect(-win_w / 2, win_y0, win_w / 2, win_y1, 2);
            translate([0, 0, z_top - eps]) linear_extrude(1)
                rrect(-win_w / 2 - win_chamfer, win_y0 - win_chamfer,
                      win_w / 2 + win_chamfer, win_y1 + win_chamfer, 2 + win_chamfer);
        }
        translate([mic_x, mic_y, z_pocket - 1]) linear_extrude(lip_t + 2)
            rrect(-mic_slot[0] / 2, -mic_slot[1] / 2, mic_slot[0] / 2, mic_slot[1] / 2, mic_slot[0] / 2 - eps);
        holes_at() translate([0, 0, z_split - 1]) cylinder(d = screw_pilot, h = z_top - 1 - z_split);
        usb_opening();
    }
}

// Rough stand-in for the board, for the assembly view only.
module board() {
    color("darkgreen") linear_extrude(PCB_T) rrect(-W / 2, -L / 2, W / 2, L / 2, PCB_R);
    color("black") translate([0, 0, PCB_T]) linear_extrude(FRONT_STACK)
        rrect(-60.88 / 2, -L / 2 + 8.6, 60.88 / 2, L / 2 - 9.0, 0.5);
    color("silver") translate([-4.5, -L / 2 - 0.8, -3.2]) cube([9, 7.3, 3.2]);
    color("black") translate([spk_c[0], spk_c[1], z_floor]) linear_extrude(spk[2])
        rrect(-spk[0] / 2, -spk[1] / 2, spk[0] / 2, spk[1] / 2, 4);
}

if (part == "tub") tub();
else if (part == "bezel") translate([0, 0, z_top]) rotate([180, 0, 0]) bezel(); // print face down
else {
    color("dimgray") tub();
    board();
    color("gray", 0.9) translate([0, 0, 25]) bezel();
}

echo(str("Outside: ", 2 * out_x, " x ", out_top - out_bot, " x ", z_top - z_bot, " mm"));
echo(str("Screw: M3 reaching ", z_pocket + lip_t - 1 - head_seat_z, " mm max from the head seat"));
