//=============================================================================
// Battle Tanks: Host-Based C Software Unit Test Suite
// Verifies all core game mechanics, physics, and MMIO drivers natively
//=============================================================================

#define HOST_TEST 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "game.h"
#include "physics.h"
#include "mmio.h"

// Define mock MMIO memory space
uint32_t g_mock_mmio_space[1024];

// Test statistics
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        printf("  [FAIL] Line %d: %s\n", __LINE__, msg); \
    } \
} while(0)

#define RUN_SUITE(suite_fn, name) do { \
    printf("\n=== Running Suite: %s ===\n", name); \
    suite_fn(); \
} while(0)

static void reset_mock_mmio(void) {
    memset(g_mock_mmio_space, 0, sizeof(g_mock_mmio_space));
    // Default timers ready: P1 shoot, P2 shoot, P1 spec, P2 spec
    REG_TIMER_STATUS = TMR_P1_SHOOT | TMR_P2_SHOOT | TMR_P1_SPECIAL | TMR_P2_SPECIAL;
}

//=============================================================================
// Suite 1: Trigonometric Tables & Fixed-Point Conversion
//=============================================================================
static void test_suite_trig_and_fixed_point(void) {
    // 1. Check cardinal angles
    // Angle 0: East (cos=256, sin=0)
    TEST_ASSERT(g_cos_fp[0] == 256, "cos(0 deg) == 256");
    TEST_ASSERT(g_sin_fp[0] == 0,   "sin(0 deg) == 0");

    // Angle 4: South (cos=0, sin=256)
    TEST_ASSERT(g_cos_fp[4] == 0,   "cos(90 deg) == 0");
    TEST_ASSERT(g_sin_fp[4] == 256, "sin(90 deg) == 256");

    // Angle 8: West (cos=-256, sin=0)
    TEST_ASSERT(g_cos_fp[8] == -256, "cos(180 deg) == -256");
    TEST_ASSERT(g_sin_fp[8] == 0,    "sin(180 deg) == 0");

    // Angle 12: North (cos=0, sin=-256)
    TEST_ASSERT(g_cos_fp[12] == 0,    "cos(270 deg) == 0");
    TEST_ASSERT(g_sin_fp[12] == -256, "sin(270 deg) == -256");

    // 2. Pythagorean theorem test for all 16 angles: cos^2 + sin^2 ~= 256^2 (65536)
    for (int i = 0; i < 16; i++) {
        int32_t c = g_cos_fp[i];
        int32_t s = g_sin_fp[i];
        int32_t mag_sq = c * c + s * s;
        // Check within 2% margin due to integer rounding
        int diff = abs(mag_sq - 65536);
        TEST_ASSERT(diff < 2000, "cos^2 + sin^2 approximately equals 256^2");
    }

    // 3. Fixed point conversions
    int val = 42;
    int32_t fp = TO_FP(val);
    TEST_ASSERT(fp == (42 << 8), "TO_FP(42) shifts left by 8");
    TEST_ASSERT(FROM_FP(fp) == 42, "FROM_FP round-trip equals 42");

    int negative_val = -15;
    int32_t neg_fp = TO_FP(negative_val);
    TEST_ASSERT(FROM_FP(neg_fp) == -15, "Negative FP round-trip equals -15");
}

//=============================================================================
// Suite 2: Game Initialization
//=============================================================================
static void test_suite_game_init(void) {
    reset_mock_mmio();
    game_init();

    // Verify Player 1 starting values
    TEST_ASSERT(FROM_FP(g_p1.x_fp) == 48, "P1 initial X is 48");
    TEST_ASSERT(FROM_FP(g_p1.y_fp) == 48, "P1 initial Y is 48");
    TEST_ASSERT(g_p1.angle == 0,          "P1 initial angle is 0 (East)");
    TEST_ASSERT(g_p1.hp == 20,            "P1 initial HP is 20");
    TEST_ASSERT(g_p1.score == 0,         "P1 initial score is 0");

    // Verify Player 2 starting values
    TEST_ASSERT(FROM_FP(g_p2.x_fp) == 410, "P2 initial X is 410");
    TEST_ASSERT(FROM_FP(g_p2.y_fp) == 410, "P2 initial Y is 410");
    TEST_ASSERT(g_p2.angle == 8,           "P2 initial angle is 8 (West)");
    TEST_ASSERT(g_p2.hp == 20,             "P2 initial HP is 20");
    TEST_ASSERT(g_p2.score == 0,          "P2 initial score is 0");

    // Verify all 8 bullets inactive
    for (int i = 0; i < MAX_BULLETS; i++) {
        TEST_ASSERT(g_bullets[i].active == 0, "Bullet is initially inactive");
    }

    // Verify MMIO registers
    TEST_ASSERT(REG_HUD_HEALTH_1 == 20, "HUD Health 1 register initialized to 20");
    TEST_ASSERT(REG_HUD_HEALTH_2 == 20, "HUD Health 2 register initialized to 20");
}

//=============================================================================
// Suite 3: Tank Steering & Rotation
//=============================================================================
static void test_suite_tank_steering(void) {
    reset_mock_mmio();
    game_init();

    // Player 1 turns Right (+1)
    REG_P1_KEYS = BTN_RIGHT;
    game_update_tanks();
    TEST_ASSERT(g_p1.angle == 1, "P1 angle rotates right: 0 -> 1");

    // Turn Right again
    game_update_tanks();
    TEST_ASSERT(g_p1.angle == 2, "P1 angle rotates right: 1 -> 2");

    // Player 1 turns Left (-1)
    REG_P1_KEYS = BTN_LEFT;
    game_update_tanks();
    TEST_ASSERT(g_p1.angle == 1, "P1 angle rotates left: 2 -> 1");

    // Player 1 turns Left past 0 (wrap-around to 15)
    game_update_tanks(); // 1 -> 0
    game_update_tanks(); // 0 -> 15
    TEST_ASSERT(g_p1.angle == 15, "P1 angle wraps cleanly: 0 -> 15");

    // Full 360 degree rotation (16 steps)
    for (int i = 0; i < 16; i++) {
        game_update_tanks();
    }
    TEST_ASSERT(g_p1.angle == 15, "P1 full 360-degree rotation returns to same angle");

    // Player 2 rotation test
    REG_P1_KEYS = 0;
    REG_P2_KEYS = BTN_RIGHT;
    game_update_tanks();
    TEST_ASSERT(g_p2.angle == 9, "P2 angle rotates right: 8 -> 9");
}

//=============================================================================
// Suite 4: Tank Movement & Arena Bounds Clamping
//=============================================================================
static void test_suite_tank_movement_and_bounds(void) {
    reset_mock_mmio();
    game_init();

    // Facing East (angle 0): cos=256, sin=0 -> moves in +X
    int initial_x = FROM_FP(g_p1.x_fp);
    int initial_y = FROM_FP(g_p1.y_fp);

    REG_P1_KEYS = BTN_FWD;
    game_update_tanks();

    int moved_x = FROM_FP(g_p1.x_fp);
    int moved_y = FROM_FP(g_p1.y_fp);
    TEST_ASSERT(moved_x > initial_x, "P1 moves forward in +X when facing East");
    TEST_ASSERT(moved_y == initial_y, "P1 Y unchanged when moving East");

    // Moving backward in reverse
    REG_P1_KEYS = BTN_BWD;
    game_update_tanks();
    TEST_ASSERT(FROM_FP(g_p1.x_fp) == initial_x, "P1 backward movement reverses X");

    // Test arena wall clamping: Drive backwards towards left boundary (x < 24)
    g_p1.x_fp = TO_FP(25);
    g_p1.angle = 8; // Facing West: forward goes towards x=0
    REG_P1_KEYS = BTN_FWD;
    for (int step = 0; step < 10; step++) {
        game_update_tanks();
    }
    TEST_ASSERT(FROM_FP(g_p1.x_fp) >= ARENA_MIN_X, "P1 X clamped at ARENA_MIN_X (24)");

    // Test arena max clamping: Drive towards right boundary
    g_p1.x_fp = TO_FP(430);
    g_p1.angle = 0; // Facing East
    REG_P1_KEYS = BTN_FWD;
    for (int step = 0; step < 20; step++) {
        game_update_tanks();
    }
    TEST_ASSERT(FROM_FP(g_p1.x_fp) <= (ARENA_MAX_X - TANK_SIZE), "P1 X clamped at ARENA_MAX_X - TANK_SIZE (436)");
}

//=============================================================================
// Suite 5: Bullet Spawning & Capacity Limits
//=============================================================================
static void test_suite_bullet_spawning(void) {
    reset_mock_mmio();
    game_init();

    // 1. Primary Fire
    REG_P1_KEYS = BTN_SHOOT;
    game_update_tanks();

    // Slot 0 should be active
    TEST_ASSERT(g_bullets[0].active == 1, "P1 primary fire spawns bullet in slot 0");
    TEST_ASSERT(g_bullets[0].owner == 1,  "Bullet 0 owner is Player 1");
    TEST_ASSERT(g_bullets[0].is_big == 0, "Bullet 0 is normal projectile");
    TEST_ASSERT(g_bullets[0].vx_fp > 0,   "Bullet 0 velocity is positive X (facing East)");

    // Trigger cooldown register must be written
    TEST_ASSERT((REG_TIMER_TRIGGER & TMR_P1_SHOOT) != 0, "P1 shoot cooldown triggered in MMIO");

    // 2. Cooldown blocking test: If timer status reports cooling down (0), cannot shoot again
    REG_TIMER_STATUS &= ~TMR_P1_SHOOT; // Simulate timer busy
    REG_P1_KEYS = BTN_SHOOT;
    game_update_tanks();
    TEST_ASSERT(g_bullets[1].active == 0, "P1 cannot shoot while cooldown timer active");

    // Re-enable timer
    REG_TIMER_STATUS |= TMR_P1_SHOOT;

    // 3. Special Attack Fire
    REG_TIMER_STATUS |= TMR_P1_SPECIAL;
    REG_P1_KEYS = BTN_SPECIAL;
    game_update_tanks();

    TEST_ASSERT(g_bullets[1].active == 1, "P1 special attack spawns bullet in slot 1");
    TEST_ASSERT(g_bullets[1].is_big == 1, "Bullet 1 is a BIG projectile");
    TEST_ASSERT(g_bullets[1].owner == 1,  "Bullet 1 owner is Player 1");
    TEST_ASSERT((REG_TIMER_TRIGGER & TMR_P1_SPECIAL) != 0, "P1 special cooldown triggered in MMIO");

    // 4. Player 2 Bullet Pool Independence
    REG_P1_KEYS = 0;
    REG_P2_KEYS = BTN_SHOOT;
    REG_TIMER_STATUS |= TMR_P2_SHOOT;
    game_update_tanks();

    TEST_ASSERT(g_bullets[4].active == 1, "P2 shoot spawns into dedicated slot 4 (P2 pool)");
    TEST_ASSERT(g_bullets[4].owner == 2,  "Bullet 4 owner is Player 2");
    TEST_ASSERT(g_bullets[4].vx_fp < 0,   "Bullet 4 velocity is negative X (facing West)");
}

//=============================================================================
// Suite 6: 2D Wall Reflection Physics & Ricochets
//=============================================================================
static void test_suite_reflection_physics(void) {
    reset_mock_mmio();
    game_init();

    Bullet b;
    b.active = 1;
    b.is_big = 0;
    b.bounces = 0;
    b.owner = 1;
    b.vx_fp = TO_FP(6);
    b.vy_fp = TO_FP(3);

    // Bounce off right arena wall (new_x >= 456)
    int ref = physics_reflect_bullet(&b, 450, 100, 458, 103);
    TEST_ASSERT(ref == 1, "physics_reflect_bullet detected arena boundary hit");
    TEST_ASSERT(b.vx_fp == -TO_FP(6), "Horizontal velocity inverted on vertical wall hit");
    TEST_ASSERT(b.vy_fp == TO_FP(3),  "Vertical velocity unchanged on vertical wall hit");
    TEST_ASSERT(b.bounces == 1,       "Bounce count incremented to 1");

    // Bounce off bottom arena wall (new_y >= 456)
    ref = physics_reflect_bullet(&b, 300, 450, 294, 458);
    TEST_ASSERT(ref == 1, "physics_reflect_bullet detected bottom boundary hit");
    TEST_ASSERT(b.vy_fp == -TO_FP(3), "Vertical velocity inverted on horizontal wall hit");
    TEST_ASSERT(b.bounces == 2,       "Bounce count incremented to 2");

    // Exceeding MAX_BOUNCES (4 bounces allowed)
    b.bounces = MAX_BOUNCES; // 4
    ref = physics_reflect_bullet(&b, 20, 100, 18, 100);
    TEST_ASSERT(b.bounces == 5,   "Bounce count reaches 5");
    TEST_ASSERT(b.active == 0,    "Bullet deactivates after exceeding MAX_BOUNCES");
}

//=============================================================================
// Suite 7: Dynamic Maze Destruction
//=============================================================================
static void test_suite_maze_destruction(void) {
    reset_mock_mmio();

    // Place a destructible brick at tile row=5, col=5 (pixel coords: 5*24 = 120)
    MAZE_TILE(5, 5) = TILE_BRICK_DESTRUCTIBLE;
    TEST_ASSERT(MAZE_TILE(5, 5) == TILE_BRICK_DESTRUCTIBLE, "Maze tile (5,5) set to DESTRUCTIBLE");

    // Check collision with projectile at (125, 125)
    int hit = physics_check_and_destroy_tile(125, 125, 0);
    TEST_ASSERT(hit == 1, "Hit detected on destructible brick");
    TEST_ASSERT(MAZE_TILE(5, 5) == TILE_EMPTY, "Destructible brick shattered to TILE_EMPTY in MMIO RAM!");

    // Empty space check
    int empty_hit = physics_check_and_destroy_tile(125, 125, 0);
    TEST_ASSERT(empty_hit == 0, "No hit on now-empty floor tile");

    // Indestructible steel wall check
    MAZE_TILE(6, 6) = TILE_STEEL_SOLID;
    int steel_hit = physics_check_and_destroy_tile(6*24 + 10, 6*24 + 10, 0);
    TEST_ASSERT(steel_hit == 1, "Hit detected on steel wall");
    TEST_ASSERT(MAZE_TILE(6, 6) == TILE_STEEL_SOLID, "Steel wall remains intact (not destroyed)");
}

//=============================================================================
// Suite 8: Collision Resolution & Combat Damage
//=============================================================================
static void test_suite_collision_and_damage(void) {
    reset_mock_mmio();
    game_init();

    // Setup active bullets
    g_bullets[0].active = 1;
    g_bullets[0].is_big = 0; // Normal bullet: deals 2 damage

    g_bullets[1].active = 1;
    g_bullets[1].is_big = 1; // Big bullet: deals 6 damage

    // 1. Simulate hardware collision reporting: Bullet 0 hit Player 2
    // REG_COLLISION_TANK bits: [7:0] for P1, [15:8] for P2
    REG_COLLISION_TANK = (1U << 8); // Bullet 0 hit P2

    game_process_collisions();

    TEST_ASSERT(g_p2.hp == 18,    "P2 took 2 damage from normal bullet (20 -> 18)");
    TEST_ASSERT(g_bullets[0].active == 0, "Bullet 0 deactivated upon hitting tank");

    // 2. Big bullet hit on Player 1
    REG_COLLISION_TANK = (1U << 1); // Bullet 1 hit P1
    game_process_collisions();

    TEST_ASSERT(g_p1.hp == 14,    "P1 took 6 damage from big special bullet (20 -> 14)");
    TEST_ASSERT(g_bullets[1].active == 0, "Big bullet deactivated upon hitting tank");

    // 3. Damage saturation at zero
    g_p1.hp = 3;
    g_bullets[2].active = 1;
    g_bullets[2].is_big = 1; // 6 damage on 3 HP
    REG_COLLISION_TANK = (1U << 2);
    game_process_collisions();
    TEST_ASSERT(g_p1.hp == 0, "P1 HP saturated at 0 without underflow");

    // 4. Bullet-to-Bullet mutual annihilation
    g_bullets[0].active = 1;
    g_bullets[1].active = 1;
    // Pair (0, 1) is cell index 0 in REG_COLLISION_BULLET
    REG_COLLISION_BULLET = (1U << 0);
    game_process_collisions();
    TEST_ASSERT(g_bullets[0].active == 0, "Bullet 0 annihilated in mutual collision");
    TEST_ASSERT(g_bullets[1].active == 0, "Bullet 1 annihilated in mutual collision");
}

//=============================================================================
// Suite 9: MMIO Display Synchronization
//=============================================================================
static void test_suite_display_sync(void) {
    reset_mock_mmio();
    game_init();

    g_p1.x_fp = TO_FP(120);
    g_p1.y_fp = TO_FP(85);
    g_p1.angle = 7;
    g_p1.hp = 17;

    g_p2.x_fp = TO_FP(300);
    g_p2.y_fp = TO_FP(250);
    g_p2.angle = 14;
    g_p2.hp = 9;

    g_bullets[2].active = 1;
    g_bullets[2].is_big = 1;
    g_bullets[2].x_fp = TO_FP(200);
    g_bullets[2].y_fp = TO_FP(150);

    game_sync_display();

    // Verify Tank 1 packed sprite register: {angle[3:0], y[7:0], x[7:0]}
    uint32_t expected_p1_reg = (7U << 16) | (85U << 8) | 120U;
    TEST_ASSERT(REG_SPRITE_P1_TANK == expected_p1_reg, "REG_SPRITE_P1_TANK correctly packed");

    // Verify Tank 2 packed sprite register
    uint32_t expected_p2_reg = (14U << 16) | (250U << 8) | (300U & 0xFFU);
    TEST_ASSERT(REG_SPRITE_P2_TANK == expected_p2_reg, "REG_SPRITE_P2_TANK correctly packed");

    // Verify Bullet registers
    TEST_ASSERT(REG_SPRITE_BULLET_X(2) == 200, "Bullet 2 X coordinate written to MMIO");
    TEST_ASSERT(REG_SPRITE_BULLET_Y(2) == 150, "Bullet 2 Y coordinate written to MMIO");
    TEST_ASSERT((REG_SPRITE_BULLET_ACT & (1U << 2)) != 0, "Bullet 2 active bit set in mask");
    TEST_ASSERT((REG_SPRITE_BULLET_BIG & (1U << 2)) != 0, "Bullet 2 big bit set in mask");

    // Verify HUD Health
    TEST_ASSERT(REG_HUD_HEALTH_1 == 17, "REG_HUD_HEALTH_1 matches P1 HP");
    TEST_ASSERT(REG_HUD_HEALTH_2 == 9,  "REG_HUD_HEALTH_2 matches P2 HP");
}

//=============================================================================
// Main Test Runner Entrypoint
//=============================================================================
int main(void) {
    printf("===================================================================\n");
    printf("     BATTLE TANKS: C GAME SOFTWARE UNIT TEST SUITE (HOST GCC)      \n");
    printf("===================================================================\n");

    RUN_SUITE(test_suite_trig_and_fixed_point, "1. Trigonometry & Fixed-Point Math");
    RUN_SUITE(test_suite_game_init,            "2. Game Initialization & Hardware Reset");
    RUN_SUITE(test_suite_tank_steering,        "3. Tank Steering, Rotation & 360-Wrap");
    RUN_SUITE(test_suite_tank_movement_and_bounds, "4. Tank Movement & Arena Bounds Clamping");
    RUN_SUITE(test_suite_bullet_spawning,      "5. Bullet Spawning & Cooldown Invalidation");
    RUN_SUITE(test_suite_reflection_physics,   "6. 2D Angular Reflection & Bounce Limits");
    RUN_SUITE(test_suite_maze_destruction,     "7. Dynamic Maze Destruction in Mirrored BRAM");
    RUN_SUITE(test_suite_collision_and_damage, "8. Collision Engine Handshake & Combat Damage");
    RUN_SUITE(test_suite_display_sync,         "9. MMIO Display & HUD Synchronization");

    printf("\n===================================================================\n");
    printf("  TEST EXECUTION SUMMARY: %d RUN | %d PASSED | %d FAILED\n",
           g_tests_run, g_tests_passed, g_tests_failed);
    printf("===================================================================\n");

    if (g_tests_failed == 0) {
        printf(">> ALL C SOFTWARE TESTS PASSED SUCCESSFULLY! <<\n\n");
        return 0;
    } else {
        printf(">> WARNING: %d TESTS FAILED! <<\n\n", g_tests_failed);
        return 1;
    }
}
